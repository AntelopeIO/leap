#pragma once
#include <eosio/chain/hotstuff.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio { namespace chain {
   
        const uint32_t INTERUPT_TIMEOUT = 6; //sufficient timeout for new leader to be selected

        class qc_chain {
          public:

                qc_chain( ){};
                ~qc_chain(){};

                name get_proposer();
                name get_leader();
                name get_incoming_leader();

                bool is_quorum_met(eosio::chain::quorum_certificate qc, extended_schedule schedule, hs_proposal_message proposal);

                std::vector<producer_authority> get_finalizers();

                hs_proposal_message new_proposal_candidate(block_id_type block_id, uint8_t phase_counter);
                hs_new_block_message new_block_candidate(block_id_type block_id);

                void init(chain_plugin& chain_plug, std::set<chain::account_name> my_producers);

                block_header_state_ptr get_block_header( const block_id_type& id );

                bool am_i_proposer();
                bool am_i_leader();
                bool am_i_finalizer();

                void process_proposal(hs_proposal_message msg);
                void process_vote(hs_vote_message msg);
                void process_new_view(hs_new_view_message msg);
                void process_new_block(hs_new_block_message msg);

                void broadcast_hs_proposal(hs_proposal_message msg);
                void broadcast_hs_vote(hs_vote_message msg);
                void broadcast_hs_new_view(hs_new_view_message msg);
                void broadcast_hs_new_block(hs_new_block_message msg);

                bool extends(fc::sha256 descendant, fc::sha256 ancestor);

                void on_beat(block_state& hbs);

                void update_high_qc(eosio::chain::quorum_certificate high_qc);

                void on_leader_rotate();

                bool is_node_safe(hs_proposal_message proposal);

                std::vector<hs_proposal_message> get_qc_chain(fc::sha256 proposal_id);
                
                void on_hs_vote_msg(hs_vote_message msg); //confirmation msg event handler
                void on_hs_proposal_msg(hs_proposal_message msg); //consensus msg event handler
                void on_hs_new_view_msg(hs_new_view_message msg); //new view msg event handler
                void on_hs_new_block_msg(hs_new_block_message msg); //new block msg event handler

                void update(hs_proposal_message proposal);
                void commit(hs_proposal_message proposal);

                void clear_old_data(uint64_t cutoff);

                std::mutex      _hotstuff_state_mutex;

        };
}} /// eosio::qc_chain