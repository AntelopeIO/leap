#pragma once
#include <eosio/chain/hotstuff.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/hotstuff/base_pacemaker.hpp>

#include <fc/crypto/bls_utils.hpp>
#include <fc/crypto/sha256.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/composite_key.hpp>

#include <boost/dynamic_bitset.hpp>

#include <exception>
#include <stdexcept>

// Enable this to swap the multi-index proposal store with std::map
//#define QC_CHAIN_SIMPLE_PROPOSAL_STORE

namespace eosio::hotstuff {

   using boost::multi_index_container;
   using namespace boost::multi_index;
   using namespace eosio::chain;

   class quorum_certificate {
   public:
      explicit quorum_certificate(size_t finalizer_size = 0) {
         active_finalizers.resize(finalizer_size);
      }

      explicit quorum_certificate(const quorum_certificate_message& msg)
              : proposal_id(msg.proposal_id)
              , active_finalizers(msg.active_finalizers.cbegin(), msg.active_finalizers.cend())
              , active_agg_sig(msg.active_agg_sig) {
      }

      quorum_certificate_message to_msg() const {
         return {.proposal_id = proposal_id,
                 .active_finalizers = [this]() {
                           std::vector<unsigned_int> r;
                           r.resize(active_finalizers.num_blocks());
                           boost::to_block_range(active_finalizers, r.begin());
                           return r;
                        }(),
                 .active_agg_sig = active_agg_sig};
      }

      void reset(const fc::sha256& proposal, size_t finalizer_size) {
         proposal_id = proposal;
         active_finalizers = hs_bitset{finalizer_size};
         active_agg_sig = fc::crypto::blslib::bls_signature();
         quorum_met = false;
      }

      const hs_bitset& get_active_finalizers() const {
         assert(!active_finalizers.empty());
         return active_finalizers;
      }
      void set_active_finalizers(const hs_bitset& bs) {
         assert(!bs.empty());
         active_finalizers = bs;
      }
      std::string get_active_finalizers_string() const {
         std::string r;
         boost::to_string(active_finalizers, r);
         return r;
      }

      const fc::sha256& get_proposal_id() const { return proposal_id; }
      const fc::crypto::blslib::bls_signature& get_active_agg_sig() const { return active_agg_sig; }
      void set_active_agg_sig( const fc::crypto::blslib::bls_signature& sig) { active_agg_sig = sig; }
      bool is_quorum_met() const { return quorum_met; }
      void set_quorum_met() { quorum_met = true; }

   private:
      friend struct fc::reflector<quorum_certificate>;
      fc::sha256                          proposal_id;
      hs_bitset                           active_finalizers; //bitset encoding, following canonical order
      fc::crypto::blslib::bls_signature   active_agg_sig;
      bool                                quorum_met = false; // not serialized across network
   };

   struct seen_votes {
      fc::sha256       proposal_id; // id of proposal being voted on
      uint64_t         height;      // height of the proposal (for GC)
      std::set<name>   finalizers;  // finalizers that have voted on the proposal
   };

   // Concurrency note: qc_chain is a single-threaded and lock-free decision engine.
   //                   All thread synchronization, if any, is external.
   class qc_chain {
   public:

      qc_chain() = delete;

      qc_chain(name id, base_pacemaker* pacemaker,
               std::set<name> my_producers,
               chain::bls_key_map_t finalizer_keys,
               fc::logger& logger);

      uint64_t get_state_version() const { return _state_version; } // no lock required

      name get_id_i() const { return _id; } // only for testing

      // Calls to the following methods should be thread-synchronized externally:

      void get_state(finalizer_state& fs) const;

      void on_beat();

      void on_hs_vote_msg(const uint32_t connection_id, const hs_vote_message& msg);
      void on_hs_proposal_msg(const uint32_t connection_id, const hs_proposal_message& msg);
      void on_hs_new_view_msg(const uint32_t connection_id, const hs_new_view_message& msg);

      // NOTE: The hotstuff New Block message is not ever propagated (multi-hop) by this method.
      //       Unit tests do not use network topology emulation for this message.
      //       The live network does not actually dispatch this message to the wire; this is a local callback.
      void on_hs_new_block_msg(const uint32_t connection_id, const hs_new_block_message& msg);

   private:

      const hs_proposal_message* get_proposal(const fc::sha256& proposal_id); // returns nullptr if not found

      // returns false if proposal with that same ID already exists at the store of its height
      bool insert_proposal(const hs_proposal_message& proposal);

      uint32_t positive_bits_count(const hs_bitset& finalizers);

      hs_bitset update_bitset(const hs_bitset& finalizer_set, name finalizer);

      //get digest to sign from proposal data
      digest_type get_digest_to_sign(const block_id_type& block_id, uint8_t phase_counter, const fc::sha256& final_on_qc);

      void reset_qc(const fc::sha256& proposal_id);

      //evaluate quorum for a proposal
      bool evaluate_quorum(const extended_schedule& es, const hs_bitset& finalizers, const fc::crypto::blslib::bls_signature& agg_sig, const hs_proposal_message& proposal);

      //check if quorum has been met over a proposal
      bool is_quorum_met(const quorum_certificate& qc, const extended_schedule& schedule, const hs_proposal_message& proposal);

      hs_proposal_message new_proposal_candidate(const block_id_type& block_id, uint8_t phase_counter);
      hs_new_block_message new_block_candidate(const block_id_type& block_id);

      bool am_i_proposer();
      bool am_i_leader();
      bool am_i_finalizer();

      // connection_id.has_value() when processing a non-loopback message
      void process_proposal(const std::optional<uint32_t>& connection_id, const hs_proposal_message& msg);
      void process_vote(const std::optional<uint32_t>& connection_id, const hs_vote_message& msg);
      void process_new_view(const std::optional<uint32_t>& connection_id, const hs_new_view_message& msg);
      void process_new_block(const std::optional<uint32_t>& connection_id, const hs_new_block_message& msg);

      hs_vote_message sign_proposal(const hs_proposal_message& proposal, name finalizer);

      //verify that a proposal descends from another
      bool extends(const fc::sha256& descendant, const fc::sha256& ancestor);

      //update high qc if required
      bool update_high_qc(const quorum_certificate& high_qc);

      //rotate leader if required
      void leader_rotation_check();

      //verify if a proposal should be signed
      bool is_node_safe(const hs_proposal_message& proposal);

      //get 3-phase proposal justification
      std::vector<hs_proposal_message> get_qc_chain(const fc::sha256& proposal_id);

      // connection_id.has_value() when just propagating a received message
      void send_hs_proposal_msg(const std::optional<uint32_t>& connection_id, const hs_proposal_message& msg);
      void send_hs_vote_msg(const std::optional<uint32_t>& connection_id, const hs_vote_message& msg);
      void send_hs_new_view_msg(const std::optional<uint32_t>& connection_id, const hs_new_view_message& msg);
      void send_hs_new_block_msg(const std::optional<uint32_t>& connection_id, const hs_new_block_message& msg);

      void send_hs_message_warning(const std::optional<uint32_t>& connection_id, const chain::hs_message_warning code);

      void update(const hs_proposal_message& proposal);
      void commit(const hs_proposal_message& proposal);

      void gc_proposals(uint64_t cutoff);

#warning remove. bls12-381 key used for testing purposes
      //todo : remove. bls12-381 key used for testing purposes
      std::vector<uint8_t> _seed =
         {  0,  50, 6,  244, 24,  199, 1,  25,  52,  88,  192,
            19, 18, 12, 89,  6,   220, 18, 102, 58,  209, 82,
            12, 62, 89, 110, 182, 9,   44, 20,  254, 22 };

      fc::crypto::blslib::bls_private_key _private_key = fc::crypto::blslib::bls_private_key(_seed);

      enum msg_type {
         new_view = 1,
         new_block = 2,
         qc = 3,
         vote = 4
      };

      bool _chained_mode = false;
      block_id_type _block_exec;
      block_id_type _pending_proposal_block;
      fc::sha256 _b_leaf;
      fc::sha256 _b_lock;
      fc::sha256 _b_exec;
      fc::sha256 _b_finality_violation;
      quorum_certificate _high_qc;
      quorum_certificate _current_qc;
      uint32_t _v_height = 0;
      eosio::chain::extended_schedule _schedule;
      base_pacemaker* _pacemaker = nullptr;
      std::set<name> _my_producers;
      chain::bls_key_map_t _my_finalizer_keys;
      name _id;

      mutable std::atomic<uint64_t> _state_version = 1;

      fc::logger&            _logger;

#ifdef QC_CHAIN_SIMPLE_PROPOSAL_STORE
      // keep one proposal store (id -> proposal) by each height (height -> proposal store)
      typedef map<fc::sha256, hs_proposal_message> proposal_store;
      typedef map<fc::sha256, hs_proposal_message>::iterator ps_iterator;
      typedef map<uint64_t, proposal_store>::iterator ps_height_iterator;
      map<uint64_t, proposal_store> _proposal_stores_by_height;

      // get the height of a given proposal id
      typedef map<fc::sha256, uint64_t>::iterator ph_iterator;
      map<fc::sha256, uint64_t> _proposal_height;
#else
      struct by_proposal_id{};
      struct by_proposal_height{};

      typedef multi_index_container<
         hs_proposal_message,
         indexed_by<
            hashed_unique<
               tag<by_proposal_id>,
               BOOST_MULTI_INDEX_MEMBER(hs_proposal_message,fc::sha256,proposal_id)
               >,
            ordered_non_unique<
               tag<by_proposal_height>,
               BOOST_MULTI_INDEX_CONST_MEM_FUN(hs_proposal_message,uint64_t,get_height)
               >
            >
         > proposal_store_type;

      proposal_store_type _proposal_store;  //internal proposals store
#endif

      // Possible optimization: merge _proposal_store and _seen_votes_store.
      // Store a struct { set<name> seen_votes; hs_proposal_message p; } in the (now single) multi-index.
      struct by_seen_votes_proposal_id{};
      struct by_seen_votes_proposal_height{};
      typedef multi_index_container<
         seen_votes,
         indexed_by<
            hashed_unique<
               tag<by_seen_votes_proposal_id>,
               BOOST_MULTI_INDEX_MEMBER(seen_votes,fc::sha256,proposal_id)
               >,
            ordered_non_unique<
               tag<by_seen_votes_proposal_height>,
               BOOST_MULTI_INDEX_MEMBER(seen_votes,uint64_t,height)
               >
            >
         > seen_votes_store_type;

      // given a height, store a map of proposal IDs at that height and the seen votes for it
      seen_votes_store_type _seen_votes_store;
   };

} /// eosio::hotstuff
