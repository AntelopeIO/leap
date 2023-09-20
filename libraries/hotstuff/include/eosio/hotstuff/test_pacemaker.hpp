#pragma once
#include <eosio/hotstuff/base_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

namespace eosio { namespace hotstuff {

   class test_pacemaker : public base_pacemaker {
   public:

      using hotstuff_message = std::pair<name, std::variant<hs_proposal_message, hs_vote_message, hs_new_block_message, hs_new_view_message>>;
      enum hotstuff_message_index {
         hs_proposal  = 0,
         hs_vote      = 1,
         hs_new_block = 2,
         hs_new_view  = 3,
         hs_all_messages
      };

      //class-specific functions

      bool is_qc_chain_active(const name & qcc_name) { return _qcc_deactivated.find(qcc_name) == _qcc_deactivated.end(); }

      void set_proposer(name proposer);

      void set_leader(name leader);

      void set_next_leader(name next_leader);

      void set_finalizers(std::vector<name> finalizers);

      void set_current_block_id(block_id_type id);

      void set_quorum_threshold(uint32_t threshold);

      void add_message_to_queue(hotstuff_message msg);

      void connect(const std::vector<name>& nodes);

      void disconnect(const std::vector<name>& nodes);

      bool is_connected(name node1, name node2);

      void pipe(const std::vector<test_pacemaker::hotstuff_message>& messages);

      void dispatch(std::string memo, int count, hotstuff_message_index msg_type = hs_all_messages);

      std::vector<hotstuff_message> dispatch(std::string memo, hotstuff_message_index msg_type = hs_all_messages);

      void activate(name replica);
      void deactivate(name replica);

      // must be called to register every qc_chain object created by the testcase
      void register_qc_chain(name name, std::shared_ptr<qc_chain> qcc_ptr);

      void beat();

      void on_hs_vote_msg(const hs_vote_message & msg, name id); //confirmation msg event handler
      void on_hs_proposal_msg(const hs_proposal_message & msg, name id); //consensus msg event handler
      void on_hs_new_view_msg(const hs_new_view_message & msg, name id); //new view msg event handler
      void on_hs_new_block_msg(const hs_new_block_message & msg, name id); //new block msg event handler

      //base_pacemaker interface functions

      name get_proposer();
      name get_leader();
      name get_next_leader();
      std::vector<name> get_finalizers();

      block_id_type get_current_block_id();

      uint32_t get_quorum_threshold();

      void send_hs_proposal_msg(const hs_proposal_message & msg, name id, const std::optional<uint32_t>& exclude_peer);
      void send_hs_vote_msg(const hs_vote_message & msg, name id, const std::optional<uint32_t>& exclude_peer);
      void send_hs_new_block_msg(const hs_new_block_message & msg, name id, const std::optional<uint32_t>& exclude_peer);
      void send_hs_new_view_msg(const hs_new_view_message & msg, name id, const std::optional<uint32_t>& exclude_peer);

      void send_hs_message_warning(const uint32_t sender_peer, const chain::hs_message_warning code);

      std::vector<hotstuff_message> _pending_message_queue;

      // qc_chain id to qc_chain object
      map<name, std::shared_ptr<qc_chain>> _qcc_store;

      // qc_chain ids in this set are currently deactivated
      set<name>                            _qcc_deactivated;

      // network topology: key (node name) is connected to all nodes in the mapped set.
      // double mapping, so if _net[a] yields b, then _net[b] yields a.
      // this is a filter; messages to self won't happen even if _net[x] yields x.
      map<name, std::set<name>>            _net;

   private:

      name _proposer;
      name _leader;
      name _next_leader;

      std::vector<name> _finalizers;

      block_id_type _current_block_id;

      std::vector<name> _unique_replicas;
#warning calculate from schedule
      uint32_t _quorum_threshold = 15; //todo : calculate from schedule
   };

}}
