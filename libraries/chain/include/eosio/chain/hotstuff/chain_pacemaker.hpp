#pragma once

#include <eosio/chain/hotstuff/base_pacemaker.hpp>
#include <eosio/chain/hotstuff/qc_chain.hpp>

#include <boost/signals2/connection.hpp>

#include <shared_mutex>

namespace eosio::chain {

   class controller;

   class chain_pacemaker : public base_pacemaker {
   public:

      //class-specific functions

      chain_pacemaker(controller* chain,
                      std::set<account_name> my_producers,
                      bls_pub_priv_key_map_t finalizer_keys,
                      fc::logger& logger);
      void register_bcast_function(std::function<void(const std::optional<uint32_t>&, const hs_message&)> broadcast_hs_message);
      void register_warn_function(std::function<void(uint32_t, const hs_message_warning&)> warning_hs_message);

      void beat();

      void on_hs_msg(const uint32_t connection_id, const hs_message& msg);

      void get_state(finalizer_state& fs) const;

      //base_pacemaker interface functions

      name get_proposer() final;
      name get_leader() final;
      name get_next_leader() final;
      const finalizer_set&  get_finalizer_set() final;

      block_id_type get_current_block_id() final;

      uint32_t get_quorum_threshold() final;

      void send_hs_proposal_msg(const hs_proposal_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) final;
      void send_hs_vote_msg(const hs_vote_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) final;
      void send_hs_new_view_msg(const hs_new_view_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) final;

      void send_hs_message_warning(uint32_t sender_peer, hs_message_warning code) final;

   private:
      void on_accepted_block( const block_state_ptr& blk );
      void on_irreversible_block( const block_state_ptr& blk );

      void on_hs_proposal_msg(const uint32_t connection_id, const hs_proposal_message& msg); //consensus msg event handler
      void on_hs_vote_msg(const uint32_t connection_id, const hs_vote_message& msg); //confirmation msg event handler
      void on_hs_new_view_msg(const uint32_t connection_id, const hs_new_view_message& msg); //new view msg event handler
   private:

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

      chain::controller*                 _chain = nullptr; // TODO will not be needed once this is merged with PR#1559

      mutable std::mutex                 _chain_state_mutex;
      block_state_ptr                    _head_block_state;
      finalizer_set                      _active_finalizer_set;

      boost::signals2::scoped_connection _accepted_block_connection;
      boost::signals2::scoped_connection _irreversible_block_connection;

      qc_chain                _qc_chain;
      std::function<void(const std::optional<uint32_t>&, const hs_message&)> bcast_hs_message = [](const std::optional<uint32_t>&, const hs_message&){};
      std::function<void(uint32_t, const hs_message_warning&)> warn_hs_message = [](uint32_t, const hs_message_warning&){};

      uint32_t                _quorum_threshold = 15; //FIXME/TODO: calculate from schedule
      fc::logger&             _logger;
   };

} // namespace eosio::hotstuff