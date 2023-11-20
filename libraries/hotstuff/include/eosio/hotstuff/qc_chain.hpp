#pragma once
#include <eosio/chain/hotstuff.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/hotstuff/base_pacemaker.hpp>
#include <eosio/hotstuff/state.hpp>

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
         hs_bitset     bitset;
         bls_signature sig;

         void resize(size_t num_finalizers) { bitset.resize(num_finalizers); }
         size_t count() const { return bitset.count(); }

         bool   add_vote(size_t index, const bls_signature& new_sig) {
            if (bitset[index])
               return false; // shouldn't be already present
            bitset.set(index);
            sig = fc::crypto::blslib::aggregate({ sig, new_sig });
            return true;
         }
      };

      pending_quorum_certificate() = default;

      explicit pending_quorum_certificate(size_t num_finalizers) :
         num_finalizers(num_finalizers) {
         weak_votes.resize(num_finalizers);
         strong_votes.resize(num_finalizers);
      }

      size_t num_weak()   const { return weak_votes.count(); }
      size_t num_strong() const { return strong_votes.count(); }

      bool   valid() const { return state >= state_t::weak_achieved; }

      bool add_strong_vote(size_t index, const bls_signature& sig) {
         assert(index < num_finalizers);
         if (!strong_votes.add_vote(index, sig))
            return false;
         size_t weak = num_weak();
         size_t strong = num_strong();
         
         switch(state) {
         case state_t::unrestricted:
         case state_t::restricted:
            if (strong >= quorum)
                state = state_t::strong;
            else if (weak + strong >= quorum)
               state = state_t::weak_achieved;
            break;
            
         case state_t::weak_achieved:
            if (strong >= quorum)
               state = state_t::strong;
            break;
            
         case state_t::weak_final:
         case state_t::strong:
            // nothing to do
            break;
         }
         return true;
      }

      bool add_weak_vote(size_t index, const bls_signature& sig) {
         assert(index < num_finalizers);
         if (!weak_votes.add_vote(index, sig))
            return false;
         size_t weak = num_weak();
         size_t strong = num_strong();
         
         switch(state) {
         case state_t::unrestricted:
         case state_t::restricted:
            if (weak + strong >= quorum)
               state = state_t::weak_achieved;
            
            if (weak >= (num_finalizers - quorum)) {
               if (state == state_t::weak_achieved)
                  state = state_t::weak_final;
               else if (state == state_t::unrestricted)
                  state = state_t::restricted;
            }
            break;
            
         case state_t::weak_achieved:
            if (weak >= (num_finalizers - quorum))
               state = state_t::weak_final;
            break;
            
         case state_t::weak_final:
         case state_t::strong:
            // nothing to do
            break;
         }
         return true;
      }

      state_t   state { state_t::unrestricted };
      size_t    num_finalizers {0};
      size_t    quorum {0};
      votes_t   weak_votes;
      votes_t   strong_votes;
   };

   class valid_quorum_certificate {
   public:
      valid_quorum_certificate(const pending_quorum_certificate& qc) {
         if (qc.state == pending_quorum_certificate::state_t::strong) {
            strong_votes = qc.strong_votes.bitset;
            sig = qc.strong_votes.sig;
         } if (qc.state > pending_quorum_certificate::state_t::weak_achieved) {
            strong_votes = qc.strong_votes.bitset;
            weak_votes   = qc.weak_votes.bitset;
            sig = fc::crypto::blslib::aggregate({ qc.strong_votes.sig, qc.weak_votes.sig });
         } else
            assert(0); // this should be called only when we have a valid qc.
      }

      bool is_weak()   const { return !!weak_votes; }
      bool is_strong() const { return !weak_votes; }
      
      std::optional<hs_bitset> strong_votes;
      std::optional<hs_bitset> weak_votes;
      bls_signature            sig;
   };
   
   class quorum_certificate {
   public:
      explicit quorum_certificate(size_t finalizer_size = 0) {
         active_finalizers.resize(finalizer_size);
      }

      explicit quorum_certificate(const quorum_certificate_message& msg, size_t finalizer_count)
              : proposal_id(msg.proposal_id)
              , active_finalizers(msg.active_finalizers.cbegin(), msg.active_finalizers.cend())
              , active_agg_sig(msg.active_agg_sig) {
               active_finalizers.resize(finalizer_count);
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
         active_agg_sig = bls_signature();
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
      const bls_signature& get_active_agg_sig() const { return active_agg_sig; }
      void set_active_agg_sig( const bls_signature& sig) { active_agg_sig = sig; }
      bool is_quorum_met() const { return quorum_met; }
      void set_quorum_met() { quorum_met = true; }


   private:
      friend struct fc::reflector<quorum_certificate>;
      fc::sha256      proposal_id;
      hs_bitset       active_finalizers; //bitset encoding, following canonical order
      bls_signature   active_agg_sig;
      bool            quorum_met = false; // not serialized across network
   };

   struct seen_votes {
      fc::sha256                 proposal_id; // id of proposal being voted on
      uint64_t                   height;      // height of the proposal (for GC)
      std::set<bls_public_key>   finalizers;  // finalizers that have voted on the proposal
   };

   // Concurrency note: qc_chain is a single-threaded and lock-free decision engine.
   //                   All thread synchronization, if any, is external.
   class qc_chain {
   public:

      qc_chain() = delete;

      qc_chain(std::string id, base_pacemaker* pacemaker,
               std::set<name> my_producers,
               chain::bls_key_map_t finalizer_keys,
               fc::logger& logger,
               std::string safety_state_file);

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

      void reset_qc(const fc::sha256& proposal_id);

      bool evaluate_quorum(const hs_bitset& finalizers, const bls_signature& agg_sig, const hs_proposal_message& proposal); //evaluate quorum for a proposal

      // qc.quorum_met has to be updated by the caller (if it wants to) based on the return value of this method
      bool is_quorum_met(const quorum_certificate& qc, const hs_proposal_message& proposal);  //check if quorum has been met over a proposal

      hs_proposal_message new_proposal_candidate(const block_id_type& block_id, uint8_t phase_counter);

      bool am_i_proposer();
      bool am_i_leader();
      bool am_i_finalizer();

      // connection_id.has_value() when processing a non-loopback message
      void process_proposal(const std::optional<uint32_t>& connection_id, const hs_proposal_message& msg);
      void process_vote(const std::optional<uint32_t>& connection_id, const hs_vote_message& msg);
      void process_new_view(const std::optional<uint32_t>& connection_id, const hs_new_view_message& msg);

      void create_proposal(const block_id_type& block_id);

      hs_vote_message sign_proposal(const hs_proposal_message& proposal, const bls_public_key& finalizer_pub_key, const bls_private_key& finalizer_priv_key);

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

      void send_hs_message_warning(const std::optional<uint32_t>& connection_id, const chain::hs_message_warning code);

      void update(const hs_proposal_message& proposal);
      void commit(const hs_proposal_message& proposal);

      void gc_proposals(uint64_t cutoff);

      bool _chained_mode = false;

      block_id_type _block_exec;
      block_id_type _pending_proposal_block;
      safety_state _safety_state;
      fc::sha256 _b_leaf;
      fc::sha256 _b_exec;
      fc::sha256 _b_finality_violation;
      quorum_certificate _high_qc;
      quorum_certificate _current_qc;
      base_pacemaker* _pacemaker = nullptr;
      std::set<name> _my_producers;
      chain::bls_key_map_t _my_finalizer_keys;
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
