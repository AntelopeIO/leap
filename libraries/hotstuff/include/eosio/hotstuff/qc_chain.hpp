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

                qc_chain(){};
                ~qc_chain(){

/*                  if (_pacemaker == NULL) delete _pacemaker;

                  _pacemaker = 0;*/
                  
                };

                //todo : remove. bls12-381 key used for testing purposes
                std::vector<uint8_t> _seed = {  0,  50, 6,  244, 24,  199, 1,  25,  52,  88,  192,
                                            19, 18, 12, 89,  6,   220, 18, 102, 58,  209, 82,
                                            12, 62, 89, 110, 182, 9,   44, 20,  254, 22};
                
                fc::crypto::blslib::bls_private_key _private_key = fc::crypto::blslib::bls_private_key(_seed);

                enum msg_type {
                  new_view = 1,
                  new_block = 2,
                  qc = 3,
                  vote = 4
                };

                 uint32_t _v_height;

                bool _chained_mode = false ;

                const block_id_type NULL_BLOCK_ID = block_id_type("00");
                const fc::sha256 NULL_PROPOSAL_ID = fc::sha256("00");

                fc::sha256 _b_leaf = NULL_PROPOSAL_ID;
                fc::sha256 _b_lock = NULL_PROPOSAL_ID;
                fc::sha256 _b_exec = NULL_PROPOSAL_ID;
                
                block_id_type _block_exec = NULL_BLOCK_ID;

                eosio::chain::quorum_certificate _high_qc;
                eosio::chain::quorum_certificate _current_qc;

                eosio::chain::extended_schedule _schedule;

                std::set<name> _my_producers;

                block_id_type _pending_proposal_block = NULL_BLOCK_ID;

                struct by_proposal_id{};
                struct by_proposal_height{};

                typedef multi_index_container<
                  hs_proposal_message, 
                  indexed_by<
                      hashed_unique<
                        tag<by_proposal_id>,
                        BOOST_MULTI_INDEX_MEMBER(hs_proposal_message,fc::sha256,proposal_id)
                      >,
                      ordered_unique<
                        tag<by_proposal_height>,
                        BOOST_MULTI_INDEX_CONST_MEM_FUN(hs_proposal_message,uint64_t,get_height)
                      >
                    >
                > proposal_store_type;

                proposal_store_type _proposal_store;

                //uint32_t _threshold = 15;

                digest_type get_digest_to_sign(block_id_type block_id, uint8_t phase_counter, fc::sha256 final_on_qc);
                
                void reset_qc(fc::sha256 proposal_id);

                bool evaluate_quorum(extended_schedule es, vector<name> finalizers, fc::crypto::blslib::bls_signature agg_sig, hs_proposal_message proposal);

                name get_proposer();
                name get_leader();
                name get_incoming_leader();

                bool is_quorum_met(eosio::chain::quorum_certificate qc, extended_schedule schedule, hs_proposal_message proposal);

                std::vector<name> get_finalizers();

                hs_proposal_message new_proposal_candidate(block_id_type block_id, uint8_t phase_counter);
                hs_new_block_message new_block_candidate(block_id_type block_id);

                void init(name id, base_pacemaker& pacemaker, std::set<chain::account_name> my_producers);

                bool am_i_proposer();
                bool am_i_leader();
                bool am_i_finalizer();

                void process_proposal(hs_proposal_message msg);
                void process_vote(hs_vote_message msg);
                void process_new_view(hs_new_view_message msg);
                void process_new_block(hs_new_block_message msg);

                bool extends(fc::sha256 descendant, fc::sha256 ancestor);

                void on_beat();

                void update_high_qc(eosio::chain::quorum_certificate high_qc);

                void on_leader_rotate();

                bool is_node_safe(hs_proposal_message proposal);

                std::vector<hs_proposal_message> get_qc_chain(fc::sha256 proposal_id);
                
                void send_hs_proposal_msg(hs_proposal_message msg);
                void send_hs_vote_msg(hs_vote_message msg);
                void send_hs_new_view_msg(hs_new_view_message msg);
                void send_hs_new_block_msg(hs_new_block_message msg);

                void on_hs_vote_msg(hs_vote_message msg); //confirmation msg event handler
                void on_hs_proposal_msg(hs_proposal_message msg); //consensus msg event handler
                void on_hs_new_view_msg(hs_new_view_message msg); //new view msg event handler
                void on_hs_new_block_msg(hs_new_block_message msg); //new block msg event handler

                void update(hs_proposal_message proposal);
                void commit(hs_proposal_message proposal);

                void gc_proposals(uint64_t cutoff);


        private : 

                name _id;

                base_pacemaker* _pacemaker = NULL;

        };
}} /// eosio::qc_chain