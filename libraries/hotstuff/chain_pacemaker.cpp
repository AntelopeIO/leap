#include <eosio/hotstuff/chain_pacemaker.hpp>
#include <eosio/chain/finalizer_authority.hpp>
#include <iostream>

// comment this out to remove the core profiler
#define HS_CORE_PROFILER

namespace eosio { namespace hotstuff {

// ======================== Core profiling instrumentation =========================
#ifdef HS_CORE_PROFILER
      std::mutex        csc_mutex;
      bool              csc_started = false;
      fc::microseconds  csc_total;            // total time spent by all net threads waiting on the core lock
      fc::time_point    csc_first_time;       // first time the core has received a request
      fc::time_point    csc_last_report_time; // last time a core timing report was printed to the log
      int64_t           csc_reqs;             // total number of times the core has been entered by a net thread
      struct reqstat {      // per-core-request-type stats
         fc::microseconds   total_us;  // total time spent in this request type
         fc::microseconds   max_us;    // maximum time ever spent inside a request of this type
         int64_t            count = 0; // total requests of this type made
      };
      std::map<std::string, reqstat> reqs;
      class csc {
      public:
         fc::time_point start;       // time lock request was made
         fc::time_point start_core;  // time the core has been entered
         std::string name;
         csc(const std::string & entrypoint_name) :
            start(fc::time_point::now()), name(entrypoint_name) { }
         void core_in() {
            start_core = fc::time_point::now();
            std::lock_guard g( csc_mutex );
            ++csc_reqs;   // update total core requests
            csc_total += start_core - start; // update total core synchronization contention time
            if (! csc_started) { // one-time initialization
               csc_started = true;
               csc_first_time = start_core;
               csc_last_report_time = start_core;
            }
         }
         void core_out() {
            fc::time_point end = fc::time_point::now();
            std::lock_guard g( csc_mutex );

            // update per-request metrics
            {
               auto it = reqs.find(name);
               if (it == reqs.end()) {
                  reqs.insert({name, reqstat()});
                  it = reqs.find(name);
               }
               reqstat &req = it->second;
               ++req.count;
               fc::microseconds exectime = end - start_core;
               req.total_us += exectime;
               if (exectime > req.max_us) {
                  req.max_us = exectime;
               }
            }

            // emit full report every 10s
            fc::microseconds elapsed = end - csc_last_report_time;
            if (elapsed.count() > 10000000) { // 10-second intervals to print the report
               fc::microseconds total_us = end - csc_first_time; // total testing walltime so far since 1st request seen
               int64_t total_secs = total_us.count() / 1000000; // never zero if report interval large enough
               int64_t avgs = csc_total.count() / total_secs;
               int64_t avgr = csc_total.count() / csc_reqs;
               // core contention report
               ilog("HS-CORE: csc_total_us:${tot} csc_elapsed_s:${secs} csc_avg_us_per_s:${avgs} csc_reqs:${reqs} csc_avg_us_per_req:${avgr}", ("tot", csc_total)("secs",total_secs)("avgs", avgs)("reqs", csc_reqs)("avgr", avgr));
               fc::microseconds req_total_us; // will compute global stats for all request types
               fc::microseconds req_max_us;
               int64_t          req_count = 0;
               auto it = reqs.begin();
               while (it != reqs.end()) {
                  const std::string & req_name = it->first;
                  reqstat &req = it->second;
                  int64_t avgr = req.total_us.count() / it->second.count;
                  // per-request-type performance report
                  ilog("HS-CORE: ${rn}_total_us:${tot} ${rn}_max_us:${max} ${rn}_reqs:${reqs} ${rn}_avg_us_per_req:${avgr}", ("rn",req_name)("tot", req.total_us)("max",req.max_us)("reqs", req.count)("avgr", avgr));
                  req_total_us += req.total_us;
                  if (req_max_us < req.max_us) {
                     req_max_us = req.max_us;
                  }
                  req_count += req.count;
                  ++it;
               }
               // combined performance report
               int64_t req_avgr = req_total_us.count() / req_count;
               ilog("HS-CORE: total_us:${tot} max_us:${max} reqs:${reqs} avg_us_per_req:${avgr}", ("tot", req_total_us)("max",req_max_us)("reqs", req_count)("avgr", req_avgr));
               csc_last_report_time = end;
            }
         }
      };
#else
      struct csc {  // dummy profiler
         csc(const string & s) { }
         void core_in() { }
         void core_out() { }
      }
#endif
//===============================================================================================

   chain_pacemaker::chain_pacemaker(controller* chain,
                                    std::set<account_name> my_producers,
                                    bls_key_map_t finalizer_keys,
                                    fc::logger& logger)
      : _chain(chain),
        _qc_chain(std::string("default"), this, std::move(my_producers), std::move(finalizer_keys), logger),
        _logger(logger)
   {
      _accepted_block_connection = chain->accepted_block.connect( [this]( const block_state_ptr& blk ) {
         on_accepted_block( blk );
      } );
      _irreversible_block_connection = chain->irreversible_block.connect( [this]( const block_state_ptr& blk ) {
         on_irreversible_block( blk );
      } );
      _head_block_state = chain->head_block_state();
   }

   void chain_pacemaker::register_bcast_function(std::function<void(const std::optional<uint32_t>&, const chain::hs_message&)> broadcast_hs_message) {
      FC_ASSERT(broadcast_hs_message, "on_hs_message must be provided");
      // no need to std::lock_guard g( _hotstuff_global_mutex ); here since pre-comm init
      bcast_hs_message = std::move(broadcast_hs_message);
   }

   void chain_pacemaker::register_warn_function(std::function<void(const uint32_t, const chain::hs_message_warning&)> warning_hs_message) {
      FC_ASSERT(warning_hs_message, "must provide callback");
      // no need to std::lock_guard g( _hotstuff_global_mutex ); here since pre-comm init
      warn_hs_message = std::move(warning_hs_message);
   }

   void chain_pacemaker::get_state(finalizer_state& fs) const {
      // lock-free state version check
      uint64_t current_state_version = _qc_chain.get_state_version();
      if (_state_cache_version != current_state_version) {
         finalizer_state current_state;
         {
            csc prof("stat");
            std::lock_guard g( _hotstuff_global_mutex ); // lock IF engine to read state
            prof.core_in();
            current_state_version = _qc_chain.get_state_version(); // get potentially fresher version
            if (_state_cache_version != current_state_version) 
               _qc_chain.get_state(current_state);
            prof.core_out();
         }
         if (_state_cache_version != current_state_version) {
            std::unique_lock ul(_state_cache_mutex); // lock cache for writing
            _state_cache = current_state;
            _state_cache_version = current_state_version;
         }
      }

      std::shared_lock sl(_state_cache_mutex); // lock cache for reading
      fs = _state_cache;
   }

   name chain_pacemaker::debug_leader_remap(name n) {
/*
      // FIXME/REMOVE: simple device to test proposer/leader
      //   separation using the net code.
      // Given the name of who's going to be the proposer
      //   (which is the head block's producer), we swap the
      //   leader name here for someone else.
      // This depends on your configuration files for the
      //   various nodeos instances you are using to test,
      //   specifically the producer names associated with
      //   each nodeos instance.
      // This works for a setup with 21 producer names
      //   interleaved between two nodeos test instances.
      //   i.e. nodeos #1 has bpa, bpc, bpe ...
      //        nodeos #2 has bpb, bpd, bpf ...
      if (n == "bpa"_n) {
         n = "bpb"_n;
      } else if (n == "bpb"_n) {
         n = "bpa"_n;
      } else if (n == "bpc"_n) {
         n = "bpd"_n;
      } else if (n == "bpd"_n) {
         n = "bpc"_n;
      } else if (n == "bpe"_n) {
         n = "bpf"_n;
      } else if (n == "bpf"_n) {
         n = "bpe"_n;
      } else if (n == "bpg"_n) {
         n = "bph"_n;
      } else if (n == "bph"_n) {
         n = "bpg"_n;
      } else if (n == "bpi"_n) {
         n = "bpj"_n;
      } else if (n == "bpj"_n) {
         n = "bpi"_n;
      } else if (n == "bpk"_n) {
         n = "bpl"_n;
      } else if (n == "bpl"_n) {
         n = "bpk"_n;
      } else if (n == "bpm"_n) {
         n = "bpn"_n;
      } else if (n == "bpn"_n) {
         n = "bpm"_n;
      } else if (n == "bpo"_n) {
         n = "bpp"_n;
      } else if (n == "bpp"_n) {
         n = "bpo"_n;
      } else if (n == "bpq"_n) {
         n = "bpr"_n;
      } else if (n == "bpr"_n) {
         n = "bpq"_n;
      } else if (n == "bps"_n) {
         n = "bpt"_n;
      } else if (n == "bpt"_n) {
         n = "bps"_n;
      } else if (n == "bpu"_n) {
         // odd one out; can be whomever that is not in the same nodeos (it does not
         //   actually matter; we just want to make sure we are stressing the system by
         //   never allowing the proposer and leader to fall on the same nodeos instance).
         n = "bpt"_n;
      }
*/
      return n;
   }

   // called from main thread
   void chain_pacemaker::on_accepted_block( const block_state_ptr& blk ) {
      std::scoped_lock g( _chain_state_mutex );
      _head_block_state = blk;
   }

   // called from main thread
   void chain_pacemaker::on_irreversible_block( const block_state_ptr& blk ) {
      if (!blk->block->header_extensions.empty()) {
         std::optional<block_header_extension> ext = blk->block->extract_header_extension(hs_finalizer_set_extension::extension_id());
         if (ext) {
            std::scoped_lock g( _chain_state_mutex );
            _active_finalizer_set = std::move(std::get<hs_finalizer_set_extension>(*ext));
         }
      }
   }

   name chain_pacemaker::get_proposer() {
      std::scoped_lock g( _chain_state_mutex );
      return _head_block_state->header.producer;
   }

   name chain_pacemaker::get_leader() {
      std::unique_lock g( _chain_state_mutex );
      name n = _head_block_state->header.producer;
      g.unlock();

      // FIXME/REMOVE: testing leader/proposer separation
      n = debug_leader_remap(n);

      return n;
   }

   name chain_pacemaker::get_next_leader() {
      std::unique_lock g( _chain_state_mutex );
      block_timestamp_type next_block_time = _head_block_state->header.timestamp.next();
      producer_authority p_auth = _head_block_state->get_scheduled_producer(next_block_time);
      g.unlock();
      name n = p_auth.producer_name;

      // FIXME/REMOVE: testing leader/proposer separation
      n = debug_leader_remap(n);

      return n;
   }

   const finalizer_set& chain_pacemaker::get_finalizer_set(){
      return _active_finalizer_set;
   }

   block_id_type chain_pacemaker::get_current_block_id() {
      std::scoped_lock g( _chain_state_mutex );
      return _head_block_state->id;
   }

   uint32_t chain_pacemaker::get_quorum_threshold() {
      return _quorum_threshold;
   }

   // called from the main application thread
   void chain_pacemaker::beat() {
      csc prof("beat");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_beat();
      prof.core_out();
   }

   void chain_pacemaker::send_hs_proposal_msg(const hs_proposal_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) {
      bcast_hs_message(exclude_peer, msg);
   }

   void chain_pacemaker::send_hs_vote_msg(const hs_vote_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) {
      bcast_hs_message(exclude_peer, msg);
   }

   void chain_pacemaker::send_hs_new_block_msg(const hs_new_block_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) {
      bcast_hs_message(exclude_peer, msg);
   }

   void chain_pacemaker::send_hs_new_view_msg(const hs_new_view_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) {
      bcast_hs_message(exclude_peer, msg);
   }

   void chain_pacemaker::send_hs_message_warning(const uint32_t sender_peer, const chain::hs_message_warning code) {
      warn_hs_message(sender_peer, code);

   }

   // called from net threads
   void chain_pacemaker::on_hs_msg(const uint32_t connection_id, const eosio::chain::hs_message &msg) {
      std::visit(overloaded{
            [this, connection_id](const hs_vote_message& m) { on_hs_vote_msg(connection_id, m); },
            [this, connection_id](const hs_proposal_message& m) { on_hs_proposal_msg(connection_id, m); },
            [this, connection_id](const hs_new_block_message& m) { on_hs_new_block_msg(connection_id, m); },
            [this, connection_id](const hs_new_view_message& m) { on_hs_new_view_msg(connection_id, m); },
      }, msg);
   }

   // called from net threads
   void chain_pacemaker::on_hs_proposal_msg(const uint32_t connection_id, const hs_proposal_message& msg) {
      csc prof("prop");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_proposal_msg(connection_id, msg);
      prof.core_out();
   }

   // called from net threads
   void chain_pacemaker::on_hs_vote_msg(const uint32_t connection_id, const hs_vote_message& msg) {
      csc prof("vote");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_vote_msg(connection_id, msg);
      prof.core_out();
   }

   // called from net threads
   void chain_pacemaker::on_hs_new_block_msg(const uint32_t connection_id, const hs_new_block_message& msg) {
      csc prof("nblk");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_new_block_msg(connection_id, msg);
      prof.core_out();
   }

   // called from net threads
   void chain_pacemaker::on_hs_new_view_msg(const uint32_t connection_id, const hs_new_view_message& msg) {
      csc prof("view");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_new_view_msg(connection_id, msg);
      prof.core_out();
   }

}}
