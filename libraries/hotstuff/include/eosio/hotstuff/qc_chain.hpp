#pragma once
#include <eosio/chain/hotstuff.hpp>
//#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/hotstuff/base_pacemaker.hpp>
#include <fc/crypto/bls_utils.hpp>

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

#include <fc/crypto/sha256.hpp>

namespace eosio { namespace hotstuff {

   using boost::multi_index_container;
   using namespace boost::multi_index;

   using namespace eosio::chain;

   //const uint32_t INTERUPT_TIMEOUT = 6; //sufficient timeout for new leader to be selected

   class qc_chain {
   public:

      static void handle_eptr(std::exception_ptr eptr){
         try {
            if (eptr) {
               std::rethrow_exception(eptr);
            }
         } catch(const std::exception& e) {
            ilog("Caught exception ${ex}" , ("ex", e.what()));
            std::exit(0);
         }
      };

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

      bool _chained_mode = false ;

      fc::sha256 _b_leaf = NULL_PROPOSAL_ID;
      fc::sha256 _b_lock = NULL_PROPOSAL_ID;
      fc::sha256 _b_exec = NULL_PROPOSAL_ID;

      fc::sha256 _b_finality_violation = NULL_PROPOSAL_ID;

      block_id_type _block_exec = NULL_BLOCK_ID;

      block_id_type _pending_proposal_block = NULL_BLOCK_ID;

      uint32_t _v_height;

      bool _log = true;
      bool _errors = true;

      eosio::chain::quorum_certificate _high_qc;
      eosio::chain::quorum_certificate _current_qc;

      eosio::chain::extended_schedule _schedule;

      std::set<name> _my_producers;

      name _id;

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

      uint32_t positive_bits_count(fc::unsigned_int value);

      fc::unsigned_int update_bitset(fc::unsigned_int value, name finalizer);

      digest_type get_digest_to_sign(block_id_type block_id, uint8_t phase_counter, fc::sha256 final_on_qc); //get digest to sign from proposal data

      void reset_qc(fc::sha256 proposal_id); //reset current internal qc

      bool evaluate_quorum(const extended_schedule & es, fc::unsigned_int finalizers, const fc::crypto::blslib::bls_signature & agg_sig, const hs_proposal_message & proposal); //evaluate quorum for a proposal

/*                name get_proposer();
                  name get_leader();
                  name get_incoming_leader(); //get incoming leader*/

      // qc.quorum_met has to be updated by the caller (if it wants to) based on the return value of this method
      bool is_quorum_met(const eosio::chain::quorum_certificate & qc, const extended_schedule & schedule, const hs_proposal_message & proposal);  //check if quorum has been met over a proposal

      std::vector<name> get_finalizers(); //get current finalizers set

      hs_proposal_message new_proposal_candidate(block_id_type block_id, uint8_t phase_counter); //create new proposal message
      hs_new_block_message new_block_candidate(block_id_type block_id); //create new block message

      void init(name id, base_pacemaker& pacemaker, std::set<name> my_producers, bool info_logging, bool error_logging); //initialize qc object and add reference to the pacemaker

      bool am_i_proposer(); //check if I am the current proposer
      bool am_i_leader(); //check if I am the current leader
      bool am_i_finalizer(); //check if I am one of the current finalizers

      void process_proposal(const hs_proposal_message & msg); //handles proposal
      void process_vote(const hs_vote_message & msg); //handles vote
      void process_new_view(const hs_new_view_message & msg); //handles new view
      void process_new_block(const hs_new_block_message & msg); //handles new block

      hs_vote_message sign_proposal(const hs_proposal_message & proposal, name finalizer); //sign proposal

      bool extends(fc::sha256 descendant, fc::sha256 ancestor); //verify that a proposal descends from another

      void on_beat(); //handler for pacemaker beat()

      // returns true if quorum_met was recalculated in this call and it evaluated to true; returns false in all other cases
      bool update_high_qc(const eosio::chain::quorum_certificate & high_qc); //check if update to our high qc is required

      void leader_rotation_check(); //check if leader rotation is required

      bool is_node_safe(const hs_proposal_message & proposal); //verify if a proposal should be signed

      std::vector<hs_proposal_message> get_qc_chain(fc::sha256 proposal_id); //get 3-phase proposal justification

      void send_hs_proposal_msg(const hs_proposal_message & msg); //send vote msg
      void send_hs_vote_msg(const hs_vote_message & msg); //send proposal msg
      void send_hs_new_view_msg(const hs_new_view_message & msg); //send new view msg
      void send_hs_new_block_msg(const hs_new_block_message & msg); //send new block msg

      void on_hs_vote_msg(const hs_vote_message & msg); //vote msg event handler
      void on_hs_proposal_msg(const hs_proposal_message & msg); //proposal msg event handler
      void on_hs_new_view_msg(const hs_new_view_message & msg); //new view msg event handler
      void on_hs_new_block_msg(const hs_new_block_message & msg); //new block msg event handler

      void update(const hs_proposal_message & proposal); //update internal state
      void commit(const hs_proposal_message & proposal); //commit proposal (finality)

      void gc_proposals(uint64_t cutoff); //garbage collection of old proposals

      qc_chain(){
         //ilog("_high_qc : ${qc_id}", ("qc_id", _high_qc.proposal_id));
      };

   private:

      base_pacemaker* _pacemaker = nullptr;

   };
}} /// eosio::qc_chain
