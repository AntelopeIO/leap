#pragma once
#include <eosio/hotstuff/base_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

namespace eosio { namespace hotstuff {

   class test_pacemaker : public base_pacemaker {
   public:

      //class-specific functions

      bool is_qc_chain_active(const name & qcc_name) { return _qcc_deactivated.find(qcc_name) == _qcc_deactivated.end(); }

      using hotstuff_message = std::pair<name, std::variant<hs_proposal_message, hs_vote_message, hs_new_block_message, hs_new_view_message>>;

      void set_proposer(name proposer);

      void set_leader(name leader);

      void set_next_leader(name next_leader);

      void set_finalizers(std::vector<name> finalizers);

      void set_current_block_id(block_id_type id);

      void set_quorum_threshold(uint32_t threshold);

      void add_message_to_queue(hotstuff_message msg);

      void pipe(std::vector<test_pacemaker::hotstuff_message> messages);

      void dispatch(std::string memo, int count);

      std::vector<hotstuff_message> dispatch(std::string memo);

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

      void send_hs_proposal_msg(const hs_proposal_message & msg, name id);
      void send_hs_vote_msg(const hs_vote_message & msg, name id);
      void send_hs_new_block_msg(const hs_new_block_message & msg, name id);
      void send_hs_new_view_msg(const hs_new_view_message & msg, name id);

      std::vector<hotstuff_message> _pending_message_queue;

      // qc_chain id to qc_chain object
      map<name, std::shared_ptr<qc_chain>> _qcc_store;

      // qc_chain ids in this set are currently deactivated
      set<name>                            _qcc_deactivated;

   private:

      std::vector<hotstuff_message> _message_queue;

      name _proposer;
      name _leader;
      name _next_leader;

      std::vector<name> _finalizers;

      block_id_type _current_block_id;

      std::vector<name> _unique_replicas;

      uint32_t _quorum_threshold = 15; //todo : calculate from schedule
   };

}}
