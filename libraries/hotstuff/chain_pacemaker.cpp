#include <eosio/hotstuff/chain_pacemaker.hpp>
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

   chain_pacemaker::chain_pacemaker(controller* chain, std::set<name> my_producers, bool info_logging, bool error_logging)
      : _chain(chain),
        _qc_chain("default"_n, this, my_producers, info_logging, error_logging)
   {
   }

   // Called internally by the chain_pacemaker to decide whether it should do something or not, based on feature activation.
   // Only methods called by the outside need to call this; methods called by qc_chain only don't need to check for enable().
   bool chain_pacemaker::enabled() {
      return _chain->is_builtin_activated( builtin_protocol_feature_t::instant_finality );
   }

   void chain_pacemaker::get_state( finalizer_state & fs ) {
      if (enabled())
         _qc_chain.get_state( fs ); // get_state() takes scare of finer-grained synchronization internally
   }

   name chain_pacemaker::get_proposer(){
      const block_state_ptr& hbs = _chain->head_block_state();
      name n = hbs->header.producer;
      return n;
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

   name chain_pacemaker::get_leader(){
      const block_state_ptr& hbs = _chain->head_block_state();
      name n = hbs->header.producer;

      // FIXME/REMOVE: testing leader/proposer separation
      n = debug_leader_remap(n);

      return n;
   }

   name chain_pacemaker::get_next_leader(){
      const block_state_ptr& hbs = _chain->head_block_state();
      block_timestamp_type next_block_time = hbs->header.timestamp.next();
      producer_authority p_auth = hbs->get_scheduled_producer(next_block_time);
      name n = p_auth.producer_name;

      // FIXME/REMOVE: testing leader/proposer separation
      n = debug_leader_remap(n);

      return n;
   }

   std::vector<name> chain_pacemaker::get_finalizers(){
      const block_state_ptr& hbs = _chain->head_block_state();
      std::vector<producer_authority> pa_list = hbs->active_schedule.producers;
      std::vector<name> pn_list;
      std::transform(pa_list.begin(), pa_list.end(),
                     std::back_inserter(pn_list),
                     [](const producer_authority& p) { return p.producer_name; });
      return pn_list;
   }

   block_id_type chain_pacemaker::get_current_block_id(){
      block_header header = _chain->head_block_state()->header;
      block_id_type block_id = header.calculate_id();
      return block_id;
   }

   uint32_t chain_pacemaker::get_quorum_threshold(){
      return _quorum_threshold;
   }

   void chain_pacemaker::beat(){
      if (! enabled())
         return;

      csc prof("beat");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_beat();
      prof.core_out();
   }

   void chain_pacemaker::send_hs_proposal_msg(const hs_proposal_message & msg, name id){
      hs_proposal_message_ptr msg_ptr = std::make_shared<hs_proposal_message>(msg);
      _chain->commit_hs_proposal_msg(msg_ptr);
   }

   void chain_pacemaker::send_hs_vote_msg(const hs_vote_message & msg, name id){
      hs_vote_message_ptr msg_ptr = std::make_shared<hs_vote_message>(msg);
      _chain->commit_hs_vote_msg(msg_ptr);
   }

   void chain_pacemaker::send_hs_new_block_msg(const hs_new_block_message & msg, name id){
      hs_new_block_message_ptr msg_ptr = std::make_shared<hs_new_block_message>(msg);
      _chain->commit_hs_new_block_msg(msg_ptr);
   }

   void chain_pacemaker::send_hs_new_view_msg(const hs_new_view_message & msg, name id){
      hs_new_view_message_ptr msg_ptr = std::make_shared<hs_new_view_message>(msg);
      _chain->commit_hs_new_view_msg(msg_ptr);
   }

   void chain_pacemaker::on_hs_proposal_msg(const hs_proposal_message & msg){
      if (! enabled())
         return;

      csc prof("prop");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_proposal_msg(msg);
      prof.core_out();
   }

   void chain_pacemaker::on_hs_vote_msg(const hs_vote_message & msg){
      if (! enabled())
         return;

      csc prof("vote");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_vote_msg(msg);
      prof.core_out();
   }

   void chain_pacemaker::on_hs_new_block_msg(const hs_new_block_message & msg){
      if (! enabled())
         return;

      csc prof("nblk");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_new_block_msg(msg);
      prof.core_out();
   }

   void chain_pacemaker::on_hs_new_view_msg(const hs_new_view_message & msg){
      if (! enabled())
         return;

      csc prof("view");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_new_view_msg(msg);
      prof.core_out();
   }

}}
