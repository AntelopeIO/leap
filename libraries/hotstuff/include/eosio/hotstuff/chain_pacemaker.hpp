#pragma once

#include <eosio/hotstuff/base_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

#include <eosio/chain/finalizer_set.hpp>

#include <boost/signals2/connection.hpp>

#include <fc/mutex.hpp>
#include <shared_mutex>

namespace eosio::chain {
   class controller;
}

namespace eosio::hotstuff {

   class commitment_mgr_t {
   public:
      commitment_mgr_t(controller* chain) : _chain(chain) {
      }

      // called from net_plugin thread - must be synchronized
      void store_commitment(const hs_commitment& commitment) {
         fc::lock_guard g(_m);
         _new_commitments.push_back(commitment);
      }

      void get_new_commitments() {
         fc::lock_guard g(_m);
         for (auto& c : _new_commitments) {
            const hs_proposal_message& p = c.b;
            uint32_t block_num = block_header::num_from_id(p.block_id);
            _commitments[block_num] = std::move(c);
         }
         _new_commitments.clear();
      }
            
      // called from main thread
      void push_required_commitment(const block_state_ptr& blk) {
         uint32_t block_num = block_header::num_from_id(blk->id);
         bool success = push_commitment(block_num);
         assert(success);
      }
      
      // called from main thread
      void push_optional_commitment(const block_state_ptr& blk) {
         uint32_t block_num = block_header::num_from_id(blk->id);
         if (block_num - _last_pushed_commitment > 64)
            push_commitment(block_num);
      }

      // called from main thread
      void seen_commitment(const hs_commitment& commitment) {
         const hs_proposal_message& p = commitment.b;
         uint32_t block_num = block_header::num_from_id(p.block_id);

         // this commitment was included in an irreversible block
         // first, we can cleanup our store of commitments of this commitment and any older one
         _commitments.erase(_commitments.begin(), _commitments.upper_bound(block_num));

         // second, we need to update the vector of pending commitments stored in the controller, which the 
         // controller will append to every new block, as this commitment and any older one don't need to
         // be included anymore.
         auto& pending = _chain->get_hs_commitments();
         if (!pending.empty()) {
            assert(std::is_sorted(pending.begin(), pending.end(),
                                  [](const hs_commitment& a, const hs_commitment& b) {
                                     return block_header::num_from_id(a.b.block_id) < block_header::num_from_id(b.b.block_id);
                                  }));
            auto keep_from = std::upper_bound(pending.begin(), pending.end(), block_num,
                                              [](uint32_t a, const hs_commitment& b) {
                                                 return a < block_header::num_from_id(b.b.block_id);
                                              });
            pending.erase(pending.begin(), keep_from);
         }
      }
      
   private:
      bool push_commitment(uint32_t block_num) {
         if (auto it = _commitments.find(block_num); it != _commitments.end()) {
            _last_pushed_commitment = block_num;
            auto& pending = _chain->get_hs_commitments();
            auto upper = std::upper_bound(pending.begin(), pending.end(), block_num,
                                          [](uint32_t a, const hs_commitment& b) {
                                             return a < block_header::num_from_id(b.b.block_id);
                                          });
            pending.insert(upper, it->second); // most times `upper == pending.end()`
            return true;
         } 
         return false;
      }
         
      controller* _chain;
      
      fc::mutex   _m;
      std::vector<hs_commitment> _new_commitments GUARDED_BY(_m);

      uint32_t                          _last_pushed_commitment;
      std::map<uint32_t, hs_commitment> _commitments;
   };

   class chain_pacemaker : public base_pacemaker {
   public:

      //class-specific functions

      chain_pacemaker(controller* chain,
                      std::set<account_name> my_producers,
                      chain::bls_key_map_t finalizer_keys,
                      fc::logger& logger);
      void register_bcast_function(std::function<void(const chain::hs_message&)> broadcast_hs_message);

      void beat();

      void on_hs_msg(const hs_message& msg);

      void get_state(finalizer_state& fs) const;

      //base_pacemaker interface functions

      name get_proposer();
      name get_leader() ;
      name get_next_leader() ;
      std::vector<name> get_finalizers();

      block_id_type get_current_block_id();

      uint32_t get_quorum_threshold();

      void send_hs_msg(const hs_message& msg, name id);

   private:
      void on_accepted_block( const block_state_ptr& blk );
      void on_irreversible_block( const block_state_ptr& blk );
      
   private:

      //FIXME/REMOVE: for testing/debugging only
      name debug_leader_remap(name n);

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

      mutable std::mutex                 _chain_state_mutex;
      block_state_ptr                    _head_block_state;
      finalizer_set                      _active_finalizer_set;

      boost::signals2::scoped_connection _accepted_block_connection;
      boost::signals2::scoped_connection _irreversible_block_connection;

      controller*             _chain;
      qc_chain                _qc_chain;
      std::function<void(const chain::hs_message&)> bcast_hs_message;

      uint32_t                _quorum_threshold = 15; //FIXME/TODO: calculate from schedule
      fc::logger&             _logger;

      commitment_mgr_t        _commitment_mgr;
   };

} // namespace eosio::hotstuff
