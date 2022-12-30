#pragma once
#include <eosio/chain/hotstuff.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio { namespace chain {
   
        const uint32_t INTERUPT_TIMEOUT = 6; //sufficient timeout for new leader to be selected

   	class qc_chain {
   	  public:

/*                const string msg_type_to_string(consensus_msg_type t) {
                    switch (t) {
                        case cm_new_view:   return "cm_new_view";
                        case cm_prepare:   return "cm_prepare";
                        case cm_pre_commit: return "cm_pre_commit";
                        case cm_commit: return "cm_commit";
                        case cm_decide: return "cm_decide";
                        default:      return "unknown";
                    }
                }

                enum qc_chain_state {
                        initializing = 1,
                        leading_view = 2, //only leader can lead view
                        processing_view = 3,
                        finished_view = 4
                };*/

                qc_chain( ){};
                ~qc_chain(){};
/*
                digest_type get_digest_to_sign(consensus_msg_type msg_type, uint32_t view_number, digest_type digest_to_sign);

                void init(chain_plugin* chain_plug, std::set<chain::account_name> my_producers); //begins or resume a new qc chain

                void create_new_view(block_state hbs); //begins a new view
                void request_new_view(); //request a new view from the leader
*/

                name get_proposer();
                name get_leader();
                name get_incoming_leader();

                bool is_quorum_met(eosio::chain::quorum_certificate qc, extended_schedule schedule, bool dual_set_mode);

                std::vector<producer_authority> get_finalizers();

                hs_proposal_message new_proposal_candidate(block_state& hbs);
                hs_new_block_message new_new_block_candidate(block_state& hbs);

                void init(chain_plugin& chain_plug, std::set<chain::account_name> my_producers);

                block_header_state_ptr get_block_header( const block_id_type& id );

                bool am_i_proposer();
                bool am_i_leader();
                bool am_i_incoming_leader();
                bool am_i_finalizer();

                void process_proposal(hs_proposal_message msg);
                void process_vote(hs_vote_message msg);
                void process_new_view(hs_new_view_message msg);
                void process_new_block(hs_new_block_message msg);

                void broadcast_hs_proposal(hs_proposal_message msg);
                void broadcast_hs_vote(hs_vote_message msg);
                void broadcast_hs_new_view(hs_new_view_message msg);
                void broadcast_hs_new_block(hs_new_block_message msg);

                bool extends(block_id_type descendant, block_id_type ancestor);

                void on_beat(block_state& hbs);

                void update_high_qc(eosio::chain::quorum_certificate high_qc);

                void on_leader_rotate(block_id_type block_id);

                bool is_node_safe(hs_proposal_message proposal);
                
                //eosio::chain::quorum_certificate get_updated_quorum(hs_vote_message vote);

                //eosio::chain::quorum_certificate create_or_get_qc(hs_vote_message proposal);

                //void add_to_qc(eosio::chain::quorum_certificate& qc, name finalizer, fc::crypto::blslib::bls_signature sig);

                void on_hs_vote_msg(hs_vote_message msg); //confirmation msg event handler
                void on_hs_proposal_msg(hs_proposal_message msg); //consensus msg event handler
                void on_hs_new_view_msg(hs_new_view_message msg); //new view msg event handler
                void on_hs_new_block_msg(hs_new_block_message msg); //new block msg event handler

                void update(hs_proposal_message proposal);
                void commit(block_header_state_ptr block);

                std::mutex      _proposal_mutex;
                std::mutex      _vote_mutex;
                std::mutex      _new_view_mutex;
                std::mutex      _new_block_mutex;



/*

                void process_confirmation_msg(confirmation_message msg, bool self_confirming); //process confirmation msg
                void process_consensus_msg(consensus_message msg, bool self_leading); //process consensus msg

                void emit_confirm(confirmation_message msg); //send confirmation message
                void emit_new_phase(consensus_message msg); //send consensus message

                void on_new_view_interrupt(); //

                void commit(block_header header);

                void print_state();

                std::mutex                              _confirmation_mutex;
                std::mutex                              _consensus_mutex;

        	chain_plugin*                           _chain_plug = nullptr;

                std::set<chain::account_name>           _my_producers;

                qc_chain_state                          _qc_chain_state;

                uint32_t                                _view_number;
                chain::account_name                     _view_leader;
                vector<producer_authority>              _view_finalizers;

                std::optional<quorum_certificate>       _prepareQC;
                std::optional<quorum_certificate>       _lockedQC;

                fc::crypto::blslib::bls_private_key     _private_key;

                quorum_certificate                      _currentQC;


                uint32_t                                _view_liveness_threshold;

                vector<confirmation_message>            _processed_confirmation_msgs;
                vector<consensus_message>               _processed_consensus_msgs;
*/

   	};
}} /// eosio::qc_chain