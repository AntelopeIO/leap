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

   void test_pacemaker::set_finalizer_set(const eosio::chain::finalizer_set& finalizer_set) {
      _finalizer_set = finalizer_set;
   };

   void test_pacemaker::set_current_block_id(block_id_type id) {
      _current_block_id = id;
   };

   void test_pacemaker::set_quorum_threshold(uint32_t threshold) {
      _quorum_threshold = threshold;
   }

   void test_pacemaker::add_message_to_queue(const hotstuff_message& msg) {
      _pending_message_queue.push_back(msg);
   }

   void test_pacemaker::connect(const std::vector<std::string>& nodes) {
      for (auto it1 = nodes.begin(); it1 != nodes.end(); ++it1) {
         for (auto it2 = std::next(it1); it2 != nodes.end(); ++it2) {
            _net[*it1].insert(*it2);
            _net[*it2].insert(*it1);
         }
      }
   }

   void test_pacemaker::disconnect(const std::vector<std::string>& nodes) {
      for (auto it1 = nodes.begin(); it1 != nodes.end(); ++it1) {
         for (auto it2 = std::next(it1); it2 != nodes.end(); ++it2) {
            _net[*it1].erase(*it2);
            _net[*it2].erase(*it1);
         }
      }
   }

   bool test_pacemaker::is_connected(std::string node1, std::string node2) {
      auto it = _net.find(node1);
      if (it == _net.end())
         return false;
      return it->second.count(node2) > 0;
   }

   void test_pacemaker::pipe(const std::vector<test_pacemaker::hotstuff_message>& messages) {
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

   void test_pacemaker::duplicate(hotstuff_message_index msg_type) {
      std::vector<test_pacemaker::hotstuff_message> dup;
      for (const auto& msg_pair : _pending_message_queue) {
         const auto& [sender_id, msg] = msg_pair;
         size_t v_index = msg.index();
         dup.push_back(msg_pair);
         if (v_index == msg_type)
            dup.push_back(msg_pair);
      }
      _pending_message_queue = std::move(dup);
   }

   std::vector<test_pacemaker::hotstuff_message> test_pacemaker::dispatch(std::string memo, hotstuff_message_index msg_type) {

      std::vector<test_pacemaker::hotstuff_message> dispatched_messages;
      std::vector<test_pacemaker::hotstuff_message> kept_messages;

      std::vector<test_pacemaker::hotstuff_message> message_queue = _pending_message_queue;

      // Need to clear the persisted message queue here because new ones are inserted in
      //   the loop below as a side-effect of the on_hs...() calls. Messages that are not
      //   propagated in the loop go into kept_messages and are reinserted after the loop.
      _pending_message_queue.clear();

      size_t votes_count = 0;
      size_t new_views_count = 0;

      for (const auto& msg_pair : message_queue) {
         const auto& [sender_id, msg] = msg_pair;
         size_t v_index = msg.index();

         if (msg_type == hs_all_messages || msg_type == v_index) {

            if (v_index == hs_vote) {
               ++votes_count;
               on_hs_vote_msg(std::get<hs_vote_message>(msg), sender_id);
            } else if (v_index == hs_new_view) {
               ++new_views_count;
               on_hs_new_view_msg(std::get<hs_new_view_message>(msg), sender_id);
            } else {
               throw std::runtime_error("unknown message variant");
            }

            dispatched_messages.push_back(msg_pair);
         } else {
            kept_messages.push_back(msg_pair);
         }
      }

      _pending_message_queue.insert(_pending_message_queue.end(), kept_messages.begin(), kept_messages.end());

      if (memo != "") {
         ilog(" === ${memo} : ", ("memo", memo));
      }

      ilog(" === pacemaker dispatched ${votes} votes, ${new_views} new_views",
           ("votes", votes_count)
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

   const finalizer_set& test_pacemaker::get_finalizer_set() {
      return _finalizer_set;
   };

   block_id_type test_pacemaker::get_current_block_id() {
      return _current_block_id;
   };

   uint32_t test_pacemaker::get_quorum_threshold() {
      return _quorum_threshold;
   };

   void test_pacemaker::beat() {

      // find the proposer-leader
      auto itr = _qcc_store.find( _proposer );
      if (itr == _qcc_store.end())
         throw std::runtime_error("proposer not found");
      std::shared_ptr<qc_chain> & proposer_qcc_ptr = itr->second;

      // create a proposal using the qc_chain unit testing interface (and receive on self)
      block_id_type current_block_id = get_current_block_id();
      hs_proposal proposal = proposer_qcc_ptr->test_create_proposal(current_block_id);
      proposer_qcc_ptr->test_receive_proposal(proposal);
      std::string proposer_id = proposer_qcc_ptr->get_id_i();

      // receive the proposal on all other qc_chains using the qc_chain unit testing interface
      for (const auto& [qcc_name, qcc_ptr] : _qcc_store) {
         if (qcc_ptr->get_id_i() != proposer_id && is_qc_chain_active(qcc_name))
            qcc_ptr->test_receive_proposal(proposal);
      }
   };

   void test_pacemaker::register_qc_chain(name name, std::shared_ptr<qc_chain> qcc_ptr) {
      auto itr = _qcc_store.find( name );
      if (itr != _qcc_store.end())
         throw std::runtime_error("duplicate qc chain");
      else
         _qcc_store.emplace( name, qcc_ptr );
   };

   void test_pacemaker::send_hs_vote_msg(const hs_vote_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_new_view_msg(const hs_new_view_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer) {
      _pending_message_queue.push_back(std::make_pair(id, msg));
   };

   void test_pacemaker::send_hs_message_warning(const uint32_t sender_peer, const chain::hs_message_warning code) { }

   void test_pacemaker::on_hs_vote_msg(const hs_vote_message& msg, const std::string& id) {
      for (const auto& [qcc_name, qcc_ptr] : _qcc_store) {
         if (qcc_ptr->get_id_i() != id && is_qc_chain_active(qcc_name) && is_connected(id, qcc_ptr->get_id_i()))
            qcc_ptr->on_hs_vote_msg(0, msg);
      }
   }

   void test_pacemaker::on_hs_new_view_msg(const hs_new_view_message& msg, const std::string& id) {
      for (const auto& [qcc_name, qcc_ptr] : _qcc_store) {
         if (qcc_ptr->get_id_i() != id && is_qc_chain_active(qcc_name) && is_connected(id, qcc_ptr->get_id_i()))
            qcc_ptr->on_hs_new_view_msg(0, msg);
      }
   }

} // namespace eosio::hotstuff
