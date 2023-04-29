#include <eosio/hotstuff/test_pacemaker.hpp>
#include <iostream>

namespace eosio { namespace hotstuff {

   void test_pacemaker::set_proposer(name proposer) {
      _proposer = proposer;
   };

   void test_pacemaker::set_leader(name leader) {
      _leader = leader;
   };

   void test_pacemaker::set_next_leader(name next_leader) {
      _next_leader = next_leader;
   };

   void test_pacemaker::set_finalizers(std::vector<name> finalizers) {
      _finalizers = finalizers;
   };

   void test_pacemaker::set_current_block_id(block_id_type id) {
      _current_block_id = id;
   };

   void test_pacemaker::set_quorum_threshold(uint32_t threshold) {
      _quorum_threshold = threshold;
   }

   void test_pacemaker::add_message_to_queue(hotstuff_message msg) {
      _pending_message_queue.push_back(msg);
   }

   void test_pacemaker::pipe(std::vector<test_pacemaker::hotstuff_message> messages) {
      auto itr = messages.begin();
      while (itr != messages.end()) {
         _pending_message_queue.push_back(*itr);
         itr++;
      }
   }

   void test_pacemaker::dispatch(std::string memo, int count) {
      for (int i = 0 ; i < count ; i++) {
         this->dispatch(memo);
      }
   }

   std::vector<test_pacemaker::hotstuff_message> test_pacemaker::dispatch(std::string memo) {

      std::vector<test_pacemaker::hotstuff_message> dispatched_messages = _pending_message_queue;
      _message_queue = _pending_message_queue;

      _pending_message_queue.clear();

      size_t proposals_count = 0;
      size_t votes_count = 0;
      size_t new_blocks_count = 0;
      size_t new_views_count = 0;

      auto msg_itr = _message_queue.begin();
      while (msg_itr!=_message_queue.end()) {

         size_t v_index = msg_itr->second.index();

         if (v_index==0)
            ++proposals_count;
         else if (v_index==1)
            ++votes_count;
         else if (v_index==2)
            ++new_blocks_count;
         else if (v_index==3)
            ++new_views_count;
         else
            throw std::runtime_error("unknown message variant");

         if (msg_itr->second.index() == 0)
            on_hs_proposal_msg(std::get<hs_proposal_message>(msg_itr->second), msg_itr->first);
         else if (msg_itr->second.index() == 1)
            on_hs_vote_msg(std::get<hs_vote_message>(msg_itr->second), msg_itr->first);
         else if (msg_itr->second.index() == 2)
            on_hs_new_block_msg(std::get<hs_new_block_message>(msg_itr->second), msg_itr->first);
         else if (msg_itr->second.index() == 3)
            on_hs_new_view_msg(std::get<hs_new_view_message>(msg_itr->second), msg_itr->first);
         else
            throw std::runtime_error("unknown message variant");

         ++msg_itr;
      }

      _message_queue.clear();

      if (memo != "") {
         ilog(" === ${memo} : ", ("memo", memo));
      }

      //ilog(" === pacemaker dispatched ${proposals} proposals, ${votes} votes, ${new_blocks} new_blocks, ${new_views} new_views",
      //     ("proposals", proposals_count)
      //     ("votes", votes_count)
      //     ("new_blocks", new_blocks_count)
      //     ("new_views", new_views_count));

      return dispatched_messages;
   }

   void test_pacemaker::activate(name replica) {
      auto qc_itr = _qcc_store.find( replica );
      if (qc_itr == _qcc_store.end())
         throw std::runtime_error("replica not found");

      _qcc_deactivated.erase(replica);
   }

   void test_pacemaker::deactivate(name replica) {
      auto qc_itr = _qcc_store.find( replica );
      if (qc_itr == _qcc_store.end())
         throw std::runtime_error("replica not found");

      _qcc_deactivated.insert(replica);
   }

   name test_pacemaker::get_proposer() {
      return _proposer;
   };

   name test_pacemaker::get_leader() {
      return _leader;
   };

   name test_pacemaker::get_next_leader() {
      return _next_leader;
   };

   std::vector<name> test_pacemaker::get_finalizers() {
      return _finalizers;
   };

   block_id_type test_pacemaker::get_current_block_id() {
      return _current_block_id;
   };

   uint32_t test_pacemaker::get_quorum_threshold() {
      return _quorum_threshold;
   };

   void test_pacemaker::beat() {
      auto itr = _qcc_store.find( _proposer );
      if (itr == _qcc_store.end())
         throw std::runtime_error("proposer not found");
      std::shared_ptr<qc_chain> & qcc_ptr = itr->second;
      qcc_ptr->on_beat();
   };

   void test_pacemaker::register_qc_chain(name name, std::shared_ptr<qc_chain> qcc_ptr) {
      auto itr = _qcc_store.find( name );
      if (itr != _qcc_store.end())
         throw std::runtime_error("duplicate qc chain");
      else
         _qcc_store.emplace( name, qcc_ptr );
   };

   void test_pacemaker::send_hs_proposal_msg(const hs_proposal_message & msg, name id) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_vote_msg(const hs_vote_message & msg, name id) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_new_block_msg(const hs_new_block_message & msg, name id) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_new_view_msg(const hs_new_view_message & msg, name id) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::on_hs_proposal_msg(const hs_proposal_message & msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()){
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->_id != id && is_qc_chain_active(qcc_name) )
            qcc_ptr->on_hs_proposal_msg(msg);
         qc_itr++;
      }
   }

   void test_pacemaker::on_hs_vote_msg(const hs_vote_message & msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()) {
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->_id != id && is_qc_chain_active(qcc_name) )
            qcc_ptr->on_hs_vote_msg(msg);
         qc_itr++;
      }
   }

   void test_pacemaker::on_hs_new_block_msg(const hs_new_block_message & msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()) {
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->_id != id && is_qc_chain_active(qcc_name) )
            qcc_ptr->on_hs_new_block_msg(msg);
         qc_itr++;
      }
   }

   void test_pacemaker::on_hs_new_view_msg(const hs_new_view_message & msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()){
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->_id != id && is_qc_chain_active(qcc_name) )
            qcc_ptr->on_hs_new_view_msg(msg);
         qc_itr++;
      }
   }

}}
