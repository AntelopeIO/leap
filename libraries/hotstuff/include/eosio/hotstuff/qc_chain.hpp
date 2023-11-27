#pragma once
#include <eosio/hotstuff/hotstuff.hpp>
#include <eosio/hotstuff/base_pacemaker.hpp>
#include <eosio/hotstuff/state.hpp>

#include <eosio/chain/controller.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/finalizer_set.hpp>
#include <eosio/chain/finalizer_authority.hpp>

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

#include <fc/io/cfile.hpp>

#include <exception>
#include <stdexcept>

namespace eosio::hotstuff {

   template<typename StateObjectType> class state_db_manager {
   public:
      static constexpr uint64_t magic = 0x0123456789abcdef;
      static bool write(fc::cfile& pfile, const StateObjectType& sobj) {
         if (!pfile.is_open())
            return false;
         pfile.seek(0);
         pfile.truncate();
         pfile.write((char*)(&magic), sizeof(magic));
         auto data = fc::raw::pack(sobj);
         pfile.write(data.data(), data.size());
         pfile.flush();
         return true;
      }
      static bool read(const std::string& file_path, StateObjectType& sobj) {
         if (!std::filesystem::exists(file_path))
            return false;
         fc::cfile pfile;
         pfile.set_file_path(file_path);
         pfile.open("rb");
         pfile.seek_end(0);
         if (pfile.tellp() <= 0)
            return false;
         pfile.seek(0);
         try {
            uint64_t read_magic;
            pfile.read((char*)(&read_magic), sizeof(read_magic));
            if (read_magic != magic)
               return false;
            auto datastream = pfile.create_datastream();
            StateObjectType read_sobj;
            fc::raw::unpack(datastream, read_sobj);
            sobj = std::move(read_sobj);
            return true;
         } catch (...) {
            return false;
         }
      }
      static bool write(const std::string& file_path, const StateObjectType& sobj) {
         fc::cfile pfile;
         pfile.set_file_path(file_path);
         pfile.open(fc::cfile::truncate_rw_mode);
         return write(pfile, sobj);
      }
   };

   using boost::multi_index_container;
   using namespace boost::multi_index;
   using namespace eosio::chain;
   
   using bls_public_key  = fc::crypto::blslib::bls_public_key;
   using bls_signature   = fc::crypto::blslib::bls_signature;
   using bls_private_key = fc::crypto::blslib::bls_private_key;

   inline std::string bitset_to_string(const hs_bitset& bs) { std::string r; boost::to_string(bs, r); return r; }
   inline hs_bitset   vector_to_bitset(const std::vector<uint32_t>& v) { return { v.cbegin(), v.cend() }; }
   inline std::vector<uint32_t> bitset_to_vector(const hs_bitset& bs) { 
      std::vector<uint32_t> r;
      r.resize(bs.num_blocks());
      boost::to_block_range(bs, r.begin());
      return r;
   }

   class pending_quorum_certificate {
   public:
      enum class state_t {
         unrestricted,  // No quorum reached yet, still possible to achieve any state.
         restricted,    // Enough `weak` votes received to know it is impossible to reach the `strong` state.
         weak_achieved, // Enough `weak` + `strong` votes for a valid `weak` QC, still possible to reach the `strong` state.
         weak_final,    // Enough `weak` + `strong` votes for a valid `weak` QC, `strong` not possible anymore.
         strong         // Enough `strong` votes to have a valid `strong` QC
      };

      struct votes_t {
         hs_bitset     _bitset;
         bls_signature _sig;

         void resize(size_t num_finalizers) { _bitset.resize(num_finalizers); }
         size_t count() const { return _bitset.count(); }

         bool add_vote(const std::vector<uint8_t>& proposal_digest,
                       size_t index,
                       const bls_public_key& pubkey,
                       const bls_signature& new_sig) {
            if (_bitset[index])
               return false; // shouldn't be already present
            if (!fc::crypto::blslib::verify(pubkey, proposal_digest, new_sig))
               return false; 
            _bitset.set(index);
            _sig = fc::crypto::blslib::aggregate({ _sig, new_sig }); // works even if _sig is default initialized (fp2::zero())
            return true;
         }

         void reset(size_t num_finalizers) {
            if (num_finalizers != _bitset.size())
               _bitset.resize(num_finalizers);
            _bitset.reset();
            _sig = bls_signature();
         }
      };

      pending_quorum_certificate() = default;

      explicit pending_quorum_certificate(size_t num_finalizers, size_t quorum) :
         _num_finalizers(num_finalizers),
         _quorum(quorum) {
         _weak_votes.resize(num_finalizers);
         _strong_votes.resize(num_finalizers);
      }

      explicit pending_quorum_certificate(const fc::sha256& proposal_id,
                                          const digest_type& proposal_digest,
                                          size_t num_finalizers,
                                          size_t quorum) :
         pending_quorum_certificate(num_finalizers, quorum) {         
         _proposal_id = proposal_id;
         _proposal_digest.assign(proposal_digest.data(), proposal_digest.data() + 32);
      }

      size_t num_weak()   const { return _weak_votes.count(); }
      size_t num_strong() const { return _strong_votes.count(); }

      bool   is_quorum_met() const {
         return _state == state_t::weak_achieved ||
                _state == state_t::weak_final ||
                _state == state_t::strong;
      }

      // ================== begin compatibility functions =======================
      // these assume *only* strong votes
      
      // this function is present just to make the tests still work
      // it will be removed, as well as the _proposal_id member of this class
      quorum_certificate_message to_msg() const {
         return {.proposal_id    = _proposal_id,
                 .strong_votes   = bitset_to_vector(_strong_votes._bitset),
                 .active_agg_sig = _strong_votes._sig};
      }

      const fc::sha256&    get_proposal_id() const { return _proposal_id; }
      std::string          get_votes_string() const {
         return std::string("strong(\"") + bitset_to_string(_strong_votes._bitset) + "\", weak(\"" +
            bitset_to_string(_weak_votes._bitset) + "\"";
      }
      // ================== end compatibility functions =======================

      void reset(const fc::sha256& proposal_id, const digest_type& proposal_digest, size_t num_finalizers, size_t quorum) {
         _proposal_id = proposal_id;
         _proposal_digest.assign(proposal_digest.data(), proposal_digest.data() + 32);
         _quorum = quorum;
         _strong_votes.reset(num_finalizers);
         _weak_votes.reset(num_finalizers);
         _num_finalizers = num_finalizers;
         _state = state_t::unrestricted;
      }

      bool add_strong_vote(const std::vector<uint8_t>& proposal_digest,
                           size_t index,
                           const bls_public_key& pubkey,
                           const bls_signature& sig) {
         assert(index < _num_finalizers);
         if (!_strong_votes.add_vote(proposal_digest, index, pubkey, sig))
            return false;
         size_t weak   = num_weak();
         size_t strong = num_strong();
         
         switch(_state) {
         case state_t::unrestricted:
         case state_t::restricted:
            if (strong >= _quorum) {
               assert(_state != state_t::restricted);
               _state = state_t::strong;
            } else if (weak + strong >= _quorum)
               _state = (_state == state_t::restricted) ? state_t::weak_final : state_t::weak_achieved;
            break;
            
         case state_t::weak_achieved:
            if (strong >= _quorum)
               _state = state_t::strong;
            break;
            
         case state_t::weak_final:
         case state_t::strong:
            // getting another strong vote...nothing to do
            break;
         }
         return true;
      }

      bool add_weak_vote(const std::vector<uint8_t>& proposal_digest,
                         size_t index,
                         const bls_public_key& pubkey,
                         const bls_signature& sig) {
         assert(index < _num_finalizers);
         if (!_weak_votes.add_vote(proposal_digest, index, pubkey, sig))
            return false;
         size_t weak   = num_weak();
         size_t strong = num_strong();
         
         switch(_state) {
         case state_t::unrestricted:
         case state_t::restricted:
            if (weak + strong >= _quorum)
               _state = state_t::weak_achieved;
            
            if (weak >= (_num_finalizers - _quorum)) {
               if (_state == state_t::weak_achieved)
                  _state = state_t::weak_final;
               else if (_state == state_t::unrestricted)
                  _state = state_t::restricted;
            }
            break;
            
         case state_t::weak_achieved:
            if (weak >= (_num_finalizers - _quorum))
               _state = state_t::weak_final;
            break;
            
         case state_t::weak_final:
         case state_t::strong:
            // getting another weak vote... nothing to do
            break;
         }
         return true;
      }

      bool add_vote(bool strong,
                    const std::vector<uint8_t>& proposal_digest,
                    size_t index,
                    const bls_public_key& pubkey,
                    const bls_signature& sig) {
         return strong ? add_strong_vote(proposal_digest, index, pubkey, sig) : add_weak_vote(proposal_digest, index, pubkey, sig);
      }
      
      friend struct fc::reflector<pending_quorum_certificate>;
      fc::sha256           _proposal_id; // only used in to_msg(). Remove eventually
      std::vector<uint8_t> _proposal_digest;
      state_t              _state { state_t::unrestricted };
      size_t               _num_finalizers {0};
      size_t               _quorum {0};
      votes_t              _weak_votes;
      votes_t              _strong_votes;
   };

   class valid_quorum_certificate {
   public:
      valid_quorum_certificate(const pending_quorum_certificate& qc) :
         _proposal_id(qc._proposal_id),
         _proposal_digest(qc._proposal_digest) {
         if (qc._state == pending_quorum_certificate::state_t::strong) {
            _strong_votes = qc._strong_votes._bitset;
            _sig = qc._strong_votes._sig;
         } else if (qc.is_quorum_met()) {
            _strong_votes = qc._strong_votes._bitset;
            _weak_votes   = qc._weak_votes._bitset;
            _sig = fc::crypto::blslib::aggregate({ qc._strong_votes._sig, qc._weak_votes._sig });
         } else
            assert(0); // this should be called only when we have a valid qc.
      }
      
      valid_quorum_certificate(const fc::sha256& proposal_id,
                               const std::vector<uint8_t>& proposal_digest,
                               const std::vector<uint32_t>& strong_votes, //bitset encoding, following canonical order
                               const std::vector<uint32_t>& weak_votes,   //bitset encoding, following canonical order
                               const bls_signature& sig) :
         _proposal_id(proposal_id),
         _proposal_digest(proposal_digest),
         _sig(sig)
         {
            if (!strong_votes.empty())
               _strong_votes = vector_to_bitset(strong_votes);
            if (!weak_votes.empty())
               _weak_votes = vector_to_bitset(weak_votes);
         }

      valid_quorum_certificate() = default;
      valid_quorum_certificate(const valid_quorum_certificate&) = default;

      bool is_weak()   const { return !!_weak_votes; }
      bool is_strong() const { return !_weak_votes; }

      // ================== begin compatibility functions =======================
      // these assume *only* strong votes
      
      // this function is present just to make the tests still work
      // it will be removed, as well as the _proposal_id member of this class
      quorum_certificate_message to_msg() const {
         return {.proposal_id = _proposal_id,
                 .strong_votes = _strong_votes ? bitset_to_vector(*_strong_votes) : std::vector<uint32_t>{1,0},
                 .active_agg_sig = _sig};
      }

      const fc::sha256&    get_proposal_id() const { return _proposal_id; }
      // ================== end compatibility functions =======================

      friend struct fc::reflector<valid_quorum_certificate>;
      fc::sha256               _proposal_id;     // [todo] remove
      std::vector<uint8_t>     _proposal_digest;
      std::optional<hs_bitset> _strong_votes;
      std::optional<hs_bitset> _weak_votes;
      bls_signature            _sig;
   };

   struct seen_votes {
      fc::sha256                 proposal_id; // id of proposal being voted on
      uint64_t                   height;      // height of the proposal (for GC)
      std::set<bls_public_key>   finalizers;  // finalizers that have voted on the proposal
   };

   using bls_pub_priv_key_map_t = std::map<std::string, std::string>;

   // Concurrency note: qc_chain is a single-threaded and lock-free decision engine.
   //                   All thread synchronization, if any, is external.
   class qc_chain {
   public:

      qc_chain() = delete;

      qc_chain(std::string id, base_pacemaker* pacemaker,
               std::set<name> my_producers,
               const bls_pub_priv_key_map_t& finalizer_keys,
               fc::logger& logger,
               const std::string& safety_state_file);

      uint64_t get_state_version() const { return _state_version; } // no lock required

      const std::string& get_id_i() const { return _id; } // so far, only ever relevant in a test environment and for logging (no sync)

      // Calls to the following methods should be thread-synchronized externally:

      void get_state(finalizer_state& fs) const;

      void on_beat();

      void on_hs_vote_msg(const uint32_t connection_id, const hs_vote_message& msg);
      void on_hs_proposal_msg(const uint32_t connection_id, const hs_proposal_message& msg);
      void on_hs_new_view_msg(const uint32_t connection_id, const hs_new_view_message& msg);

   private:

      void write_safety_state_file();

      const hs_proposal_message* get_proposal(const fc::sha256& proposal_id); // returns nullptr if not found

      // returns false if proposal with that same ID already exists at the store of its height
      bool insert_proposal(const hs_proposal_message& proposal);

      uint32_t positive_bits_count(const hs_bitset& finalizers);

      hs_bitset update_bitset(const hs_bitset& finalizer_set, const bls_public_key& finalizer_key);

      void reset_qc(const hs_proposal_message& proposal);

      hs_proposal_message new_proposal_candidate(const block_id_type& block_id, uint8_t phase_counter);

      bool am_i_proposer();
      bool am_i_leader();
      bool am_i_finalizer();

      // connection_id.has_value() when processing a non-loopback message
      void process_proposal(std::optional<uint32_t> connection_id, const hs_proposal_message& msg);
      void process_vote(std::optional<uint32_t> connection_id, const hs_vote_message& msg);
      void process_new_view(std::optional<uint32_t> connection_id, const hs_new_view_message& msg);

      void create_proposal(const block_id_type& block_id);

      hs_vote_message sign_proposal(const hs_proposal_message& proposal, bool strong, const bls_public_key& finalizer_pub_key, const bls_private_key& finalizer_priv_key);

      //verify that a proposal descends from another
      bool extends(const fc::sha256& descendant, const fc::sha256& ancestor);

      //update high qc if required
      bool update_high_qc(const valid_quorum_certificate& high_qc);

      //rotate leader if required
      void leader_rotation_check();

      //verify if a proposal should be signed
      bool is_node_safe(const hs_proposal_message& proposal);

      //get 3-phase proposal justification
      std::vector<hs_proposal_message> get_qc_chain(const fc::sha256& proposal_id);

      // connection_id.has_value() when just propagating a received message
      void send_hs_proposal_msg(std::optional<uint32_t> connection_id, const hs_proposal_message& msg);
      void send_hs_vote_msg(std::optional<uint32_t> connection_id, const hs_vote_message& msg);
      void send_hs_new_view_msg(std::optional<uint32_t> connection_id, const hs_new_view_message& msg);

      void send_hs_message_warning(std::optional<uint32_t> connection_id, const hs_message_warning code);

      void update(const hs_proposal_message& proposal);
      void commit(const hs_proposal_message& proposal);

      void gc_proposals(uint64_t cutoff);

      block_id_type _block_exec;
      block_id_type _pending_proposal_block;
      safety_state _safety_state;
      fc::sha256 _b_leaf;
      fc::sha256 _b_exec;
      fc::sha256 _b_finality_violation;
      valid_quorum_certificate   _high_qc;
      pending_quorum_certificate _current_qc;
      base_pacemaker* _pacemaker = nullptr;
      std::set<name> _my_producers;
      bls_key_map_t _my_finalizer_keys;
      std::string _id;

      std::string _safety_state_file; // if empty, safety state persistence is turned off
      fc::cfile _safety_state_file_handle;

      mutable std::atomic<uint64_t> _state_version = 1;

      fc::logger&            _logger;

      struct by_proposal_id{};
      struct by_proposal_height{};

      typedef multi_index_container<
         hs_proposal_message,
         indexed_by<
            hashed_unique<
               tag<by_proposal_id>,
               BOOST_MULTI_INDEX_MEMBER(hs_proposal_message, fc::sha256,proposal_id)
               >,
            ordered_non_unique<
               tag<by_proposal_height>,
               BOOST_MULTI_INDEX_CONST_MEM_FUN(hs_proposal_message, uint64_t, get_key)
               >
            >
         > proposal_store_type;

      proposal_store_type _proposal_store;  //internal proposals store

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
