#include <eosio/hotstuff/chain_pacemaker.hpp>
#include <iostream>

namespace eosio { namespace hotstuff {


	void chain_pacemaker::init(controller* chain){
		_chain = chain;
	}

   	name chain_pacemaker::get_proposer(){
   		
   		const block_state_ptr& hbs = _chain->head_block_state();
	   	
		return hbs->header.producer;

   	};

   	name chain_pacemaker::get_leader(){

   		const block_state_ptr& hbs = _chain->head_block_state();
	   	
		return hbs->header.producer;

   	};

   	name chain_pacemaker::get_next_leader(){

   		const block_state_ptr& hbs = _chain->head_block_state();
	   	
	   	block_timestamp_type next_block_time = hbs->header.timestamp.next();

	   	producer_authority p_auth = hbs->get_scheduled_producer(next_block_time);

		return p_auth.producer_name;

   	};
       	
   	std::vector<name> chain_pacemaker::get_finalizers(){
   		
   		const block_state_ptr& hbs = _chain->head_block_state();
    
	    std::vector<producer_authority> pa_list = hbs->active_schedule.producers;

	    std::vector<name> pn_list;
	    std::transform(pa_list.begin(), pa_list.end(),
	                   std::back_inserter(pn_list),
	                   [](const producer_authority& p) { return p.producer_name; });

		return pn_list;

   	};

    block_id_type chain_pacemaker::get_current_block_id(){

		block_header header = _chain->head_block_state()->header;

		block_id_type block_id = header.calculate_id();

		return block_id;

    }
	
	uint32_t chain_pacemaker::get_quorum_threshold(){
		return _quorum_threshold;
    };

	void chain_pacemaker::beat(){

   		std::lock_guard g( this-> _hotstuff_state_mutex );

   		_qc_chain->on_beat();

   	};

    void chain_pacemaker::assign_qc_chain(name name, qc_chain& qcc){
    	_qc_chain = &qcc;

    };
    
    void chain_pacemaker::send_hs_proposal_msg(name id, hs_proposal_message msg){

 		hs_proposal_message_ptr msg_ptr = std::make_shared<hs_proposal_message>(msg);

   		_chain->commit_hs_proposal_msg(msg_ptr);

   	};

	void chain_pacemaker::send_hs_vote_msg(name id, hs_vote_message msg){

 		hs_vote_message_ptr msg_ptr = std::make_shared<hs_vote_message>(msg);

   		_chain->commit_hs_vote_msg(msg_ptr);

   	};

	void chain_pacemaker::send_hs_new_block_msg(name id, hs_new_block_message msg){

 		hs_new_block_message_ptr msg_ptr = std::make_shared<hs_new_block_message>(msg);

   		_chain->commit_hs_new_block_msg(msg_ptr);

   	};

	void chain_pacemaker::send_hs_new_view_msg(name id, hs_new_view_message msg){

 		hs_new_view_message_ptr msg_ptr = std::make_shared<hs_new_view_message>(msg);

   		_chain->commit_hs_new_view_msg(msg_ptr);

   	};

	void chain_pacemaker::on_hs_proposal_msg(name id, hs_proposal_message msg){
		
		std::lock_guard g( this-> _hotstuff_state_mutex );

		_qc_chain->on_hs_proposal_msg(msg);
	}

	void chain_pacemaker::on_hs_vote_msg(name id, hs_vote_message msg){

		std::lock_guard g( this-> _hotstuff_state_mutex );

		_qc_chain->on_hs_vote_msg(msg);
	}

	void chain_pacemaker::on_hs_new_block_msg(name id, hs_new_block_message msg){

		std::lock_guard g( this-> _hotstuff_state_mutex );

		_qc_chain->on_hs_new_block_msg(msg);
	}

	void chain_pacemaker::on_hs_new_view_msg(name id, hs_new_view_message msg){

		std::lock_guard g( this-> _hotstuff_state_mutex );

		_qc_chain->on_hs_new_view_msg(msg);
	}

}}