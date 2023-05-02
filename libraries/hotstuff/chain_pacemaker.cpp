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

   void chain_pacemaker::get_state( finalizer_state & fs ) {
      _qc_chain.get_state( fs ); // get_state() takes scare of finer-grained synchronization internally
   }

   name chain_pacemaker::get_proposer(){
      const block_state_ptr& hbs = _chain->head_block_state();
      return hbs->header.producer;
   }

   name chain_pacemaker::get_leader(){
      const block_state_ptr& hbs = _chain->head_block_state();
      return hbs->header.producer;
   }

   name chain_pacemaker::get_next_leader(){
      const block_state_ptr& hbs = _chain->head_block_state();
      block_timestamp_type next_block_time = hbs->header.timestamp.next();
      producer_authority p_auth = hbs->get_scheduled_producer(next_block_time);
      return p_auth.producer_name;
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
      csc prof("prop");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_proposal_msg(msg);
      prof.core_out();
   }

   void chain_pacemaker::on_hs_vote_msg(const hs_vote_message & msg){
      csc prof("vote");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_vote_msg(msg);
      prof.core_out();
   }

   void chain_pacemaker::on_hs_new_block_msg(const hs_new_block_message & msg){
      csc prof("nblk");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_new_block_msg(msg);
      prof.core_out();
   }

   void chain_pacemaker::on_hs_new_view_msg(const hs_new_view_message & msg){
      csc prof("view");
      std::lock_guard g( _hotstuff_global_mutex );
      prof.core_in();
      _qc_chain.on_hs_new_view_msg(msg);
      prof.core_out();
   }

}}
