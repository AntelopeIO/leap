#pragma once

#include <eosio/hotstuff/base_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

#include <shared_mutex>

namespace eosio::chain {
   class controller;
}

namespace eosio::hotstuff {

   const std::string DEFAULT_SAFETY_STATE_FILE = "hs_tm_safety_state"; //todo : reversible blocks folder
   const std::string DEFAULT_LIVENESS_STATE_FILE = "hs_tm_liveness_state"; //todo : reversible blocks folder

   class chain_pacemaker : public base_pacemaker {
   public:

      //class-specific functions
   
      chain_pacemaker(controller* chain, std::set<account_name> my_producers, fc::logger& logger);

      void beat();

      void on_hs_proposal_msg(const hs_proposal_message& msg); //consensus msg event handler
      void on_hs_vote_msg(const hs_vote_message& msg); //confirmation msg event handler
      void on_hs_new_view_msg(const hs_new_view_message& msg); //new view msg event handler
      void on_hs_new_block_msg(const hs_new_block_message& msg); //new block msg event handler

      void get_state(finalizer_state& fs) const;

      //base_pacemaker interface functions

      name get_proposer();
      name get_leader() ;
      name get_next_leader() ;
      std::vector<name> get_finalizers();

      block_id_type get_current_block_id();

      uint32_t get_quorum_threshold();

      void send_hs_proposal_msg(const hs_proposal_message& msg, name id);
      void send_hs_vote_msg(const hs_vote_message& msg, name id);
      void send_hs_new_view_msg(const hs_new_view_message& msg, name id);
      void send_hs_new_block_msg(const hs_new_block_message& msg, name id);

   private:

      //FIXME/REMOVE: for testing/debugging only
      name debug_leader_remap(name n);

      // Check if consensus upgrade feature is activated
      bool enabled() const;

      // This serializes all messages (high-level requests) to the qc_chain core.
      // For maximum safety, the qc_chain core will only process one request at a time.
      // These requests can come directly from the net threads, or indirectly from a
      //   dedicated finalizer thread (TODO: discuss).
#warning discuss
      mutable std::mutex      _hotstuff_global_mutex;

      // _state_cache_mutex provides a R/W lock over _state_cache and _state_cache_version,
      //   which implement a cache of the finalizer_state (_qc_chain::get_state()).
      mutable std::shared_mutex      _state_cache_mutex;
      mutable finalizer_state        _state_cache;
      mutable std::atomic<uint64_t>  _state_cache_version = 0;

      chain::controller*      _chain = nullptr;

      qc_chain                _qc_chain;

      uint32_t                _quorum_threshold = 15; //FIXME/TODO: calculate from schedule
      fc::logger&             _logger;

   };

} // namespace eosio::hotstuff
