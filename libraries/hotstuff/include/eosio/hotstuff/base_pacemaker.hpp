#pragma once

#include <eosio/chain/hotstuff.hpp>
//#include <eosio/hotstuff/qc_chain.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/producer_schedule.hpp>
#include <eosio/chain/block_state.hpp>

using namespace eosio::chain;

namespace eosio { namespace hotstuff {

	class qc_chain;

	class base_pacemaker{

	public:
        

		//configuration setting
        virtual uint32_t get_quorum_threshold() = 0;


        //polling calls 
        virtual name get_proposer() = 0;
        virtual name get_leader() = 0;
        virtual name get_next_leader() = 0;
        virtual std::vector<name> get_finalizers() = 0;

        virtual block_id_type get_current_block_id() = 0;











        //qc_chain event subscription
        virtual void register_listener(name name, qc_chain& qcc) = 0;
        virtual void unregister_listener(name name) = 0;




        //block / proposal API
        virtual void beat() = 0;




        //outbound communications
		virtual void send_hs_proposal_msg(name id, hs_proposal_message msg) = 0;
		virtual void send_hs_vote_msg(name id, hs_vote_message msg) = 0;
		virtual void send_hs_new_block_msg(name id, hs_new_block_message msg) = 0;
		virtual void send_hs_new_view_msg(name id, hs_new_view_message msg) = 0;

		//inbound communications
      	virtual void on_hs_vote_msg(name id, hs_vote_message msg) = 0; //confirmation msg event handler
      	virtual void on_hs_proposal_msg(name id, hs_proposal_message msg) = 0; //consensus msg event handler
      	virtual void on_hs_new_view_msg(name id, hs_new_view_message msg) = 0; //new view msg event handler
      	virtual void on_hs_new_block_msg(name id, hs_new_block_message msg) = 0; //new block msg event handler

	};

}}