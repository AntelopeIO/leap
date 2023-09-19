#include <eosio/hotstuff/test_pacemaker.hpp>
#include <iostream>

namespace eosio::hotstuff {

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

   void test_pacemaker::connect(const std::vector<name>& nodes) {
      for (auto it1 = nodes.begin(); it1 != nodes.end(); ++it1) {
         for (auto it2 = std::next(it1); it2 != nodes.end(); ++it2) {
            _net[*it1].insert(*it2);
            _net[*it2].insert(*it1);
         }
      }
   }

   void test_pacemaker::disconnect(const std::vector<name>& nodes) {
      for (auto it1 = nodes.begin(); it1 != nodes.end(); ++it1) {
         for (auto it2 = std::next(it1); it2 != nodes.end(); ++it2) {
            _net[*it1].erase(*it2);
            _net[*it2].erase(*it1);
         }
      }
   }

   bool test_pacemaker::is_connected(name node1, name node2) {
      auto it = _net.find(node1);
      if (it == _net.end())
         return false;
      return it->second.count(node2) > 0;
   }

   void test_pacemaker::pipe(std::vector<test_pacemaker::hotstuff_message> messages) {
      auto itr = messages.begin();
      while (itr != messages.end()) {
         _pending_message_queue.push_back(*itr);
         itr++;
      }
   }

   void test_pacemaker::dispatch(std::string memo, int count, hotstuff_message_index msg_type) {
      for (int i = 0 ; i < count ; i++) {
         this->dispatch(memo, msg_type);
      }
   }

   std::vector<test_pacemaker::hotstuff_message> test_pacemaker::dispatch(std::string memo, hotstuff_message_index msg_type) {

      std::vector<test_pacemaker::hotstuff_message> dispatched_messages;
      std::vector<test_pacemaker::hotstuff_message> kept_messages;

      std::vector<test_pacemaker::hotstuff_message> message_queue = _pending_message_queue;

      // Need to clear the persisted message queue here because new ones are inserted in
      //   the loop below as a side-effect of the on_hs...() calls. Messages that are not
      //   propagated in the loop go into kept_messages and are reinserted after the loop.
      _pending_message_queue.clear();

      size_t proposals_count = 0;
      size_t votes_count = 0;
      size_t new_blocks_count = 0;
      size_t new_views_count = 0;

      auto msg_itr = message_queue.begin();
      while (msg_itr != message_queue.end()) {

         name sender_id = msg_itr->first;
         size_t v_index = msg_itr->second.index();

         if (msg_type == hs_all_messages || msg_type == v_index) {

            if (v_index == hs_proposal) {
               ++proposals_count;
               on_hs_proposal_msg(std::get<hs_proposal_message>(msg_itr->second), sender_id);
            } else if (v_index == hs_vote) {
               ++votes_count;
               on_hs_vote_msg(std::get<hs_vote_message>(msg_itr->second), sender_id);
            } else if (v_index == hs_new_block) {
               ++new_blocks_count;
               on_hs_new_block_msg(std::get<hs_new_block_message>(msg_itr->second), sender_id);
            } else if (v_index == hs_new_view) {
               ++new_views_count;
               on_hs_new_view_msg(std::get<hs_new_view_message>(msg_itr->second), sender_id);
            } else {
               throw std::runtime_error("unknown message variant");
            }

            dispatched_messages.push_back(*msg_itr);
         } else {
            kept_messages.push_back(*msg_itr);
         }

         ++msg_itr;
      }

      _pending_message_queue.insert(_pending_message_queue.end(), kept_messages.begin(), kept_messages.end());

      if (memo != "") {
         ilog(" === ${memo} : ", ("memo", memo));
      }

      ilog(" === pacemaker dispatched ${proposals} proposals, ${votes} votes, ${new_blocks} new_blocks, ${new_views} new_views",
           ("proposals", proposals_count)
           ("votes", votes_count)
           ("new_blocks", new_blocks_count)
           ("new_views", new_views_count));

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

   void test_pacemaker::send_hs_proposal_msg(const hs_proposal_message& msg, name id, const std::optional<uint32_t>& exclude_peer) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_vote_msg(const hs_vote_message& msg, name id, const std::optional<uint32_t>& exclude_peer) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_new_block_msg(const hs_new_block_message& msg, name id, const std::optional<uint32_t>& exclude_peer) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_new_view_msg(const hs_new_view_message& msg, name id, const std::optional<uint32_t>& exclude_peer) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_message_warning(const uint32_t sender_peer, const chain::hs_message_warning code) { }

   void test_pacemaker::on_hs_proposal_msg(const hs_proposal_message& msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()){
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->get_id_i() != id && is_qc_chain_active(qcc_name) && is_connected(id, qcc_ptr->get_id_i()))
            qcc_ptr->on_hs_proposal_msg(0, msg);
         qc_itr++;
      }
   }

   void test_pacemaker::on_hs_vote_msg(const hs_vote_message& msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()) {
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->get_id_i() != id && is_qc_chain_active(qcc_name) && is_connected(id, qcc_ptr->get_id_i()))
            qcc_ptr->on_hs_vote_msg(0, msg);
         qc_itr++;
      }
   }

   void test_pacemaker::on_hs_new_block_msg(const hs_new_block_message& msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()) {
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->get_id_i() != id && is_qc_chain_active(qcc_name) && is_connected(id, qcc_ptr->get_id_i()))
            qcc_ptr->on_hs_new_block_msg(0, msg);
         qc_itr++;
      }
   }

   void test_pacemaker::on_hs_new_view_msg(const hs_new_view_message& msg, name id) {
      auto qc_itr = _qcc_store.begin();
      while (qc_itr != _qcc_store.end()){
         const name                & qcc_name = qc_itr->first;
         std::shared_ptr<qc_chain> & qcc_ptr  = qc_itr->second;
         if (qcc_ptr->get_id_i() != id && is_qc_chain_active(qcc_name) && is_connected(id, qcc_ptr->get_id_i()))
            qcc_ptr->on_hs_new_view_msg(0, msg);
         qc_itr++;
      }
   }

} // namespace eosio::hotstuff
