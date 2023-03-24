#include <eosio/hotstuff/test_pacemaker.hpp>
#include <iostream>

namespace eosio { namespace hotstuff {

/*   	void test_pacemaker::init(std::vector<name> unique_replicas){

	    for (name r : unique_replicas){  
		 
			std::set<name> mp{r};

			register_listener(r, qcc);

	    }

   		_unique_replicas = unique_replicas;

   	};*/

   	void test_pacemaker::set_proposer(name proposer){
   		_proposer = proposer;
   	};

   	void test_pacemaker::set_leader(name leader){
   		_leader = leader;
   	};
   	
   	void test_pacemaker::set_next_leader(name next_leader){
   		_next_leader = next_leader;
   	};

   	void test_pacemaker::set_finalizers(std::vector<name> finalizers){
   		_finalizers = finalizers;
   	};

    void test_pacemaker::set_current_block_id(block_id_type id){
    	_current_block_id = id;
    };
       
    void test_pacemaker::set_quorum_threshold(uint32_t threshold){
    	_quorum_threshold = threshold;
    }

    void test_pacemaker::add_message_to_queue(hotstuff_message msg){
    	_pending_message_queue.push_back(msg);
    }

    void test_pacemaker::propagate(){

    	int count = 1;

    	//ilog(" === propagate ${count} messages", ("count", _pending_message_queue.size()));

    	_message_queue = _pending_message_queue;

    	while (_pending_message_queue.begin()!=_pending_message_queue.end()){
    		
    		auto itr = _pending_message_queue.end();
    		itr--;

            _pending_message_queue.erase(itr);

    	}

    	//ilog(" === propagate ${count} messages", ("count", _message_queue.size()));

    	auto msg_itr = _message_queue.begin();

    	while (msg_itr!=_message_queue.end()){

    	//ilog(" === propagating message ${count} : type : ${index}", ("count", count) ("index", msg_itr->index()));

    		if (msg_itr->second.index() == 0) on_hs_proposal_msg(msg_itr->first, std::get<hs_proposal_message>(msg_itr->second));
    		else if (msg_itr->second.index() == 1) on_hs_vote_msg(msg_itr->first, std::get<hs_vote_message>(msg_itr->second));
    		else if (msg_itr->second.index() == 2) on_hs_new_block_msg(msg_itr->first, std::get<hs_new_block_message>(msg_itr->second));
    		else if (msg_itr->second.index() == 3) on_hs_new_view_msg(msg_itr->first, std::get<hs_new_view_message>(msg_itr->second));
 

    		msg_itr++;

    	//ilog(" === after erase");

    		count++;

    	}

    	//ilog(" === erase");

    	while (_message_queue.begin()!=_message_queue.end()){
    		
    		auto itr = _message_queue.end();
    		itr--;

            _message_queue.erase(itr);

    	}

    	//ilog(" === after erase");

    	//ilog(" === end propagate");

    }

   	name test_pacemaker::get_proposer(){
   		return _proposer;
   	};

   	name test_pacemaker::get_leader(){
   		return _leader;
   	};
   	
   	name test_pacemaker::get_next_leader(){
   		return _next_leader;
   	};

   	std::vector<name> test_pacemaker::get_finalizers(){
   		return _finalizers;
   	};

    block_id_type test_pacemaker::get_current_block_id(){
   		return _current_block_id;
    };
    
    uint32_t test_pacemaker::get_quorum_threshold(){
    	return _quorum_threshold;
    };

	void test_pacemaker::beat(){
			
    	auto itr = _qcc_store.get<by_name_id>().find( _proposer.to_uint64_t() );

    	if (itr==_qcc_store.end()) throw std::runtime_error("proposer not found"); 

    	itr->_qc_chain->on_beat();

   	};

    void test_pacemaker::register_listener(name name, qc_chain& qcc){

    	//ilog("reg listener");

    	auto itr = _qcc_store.get<by_name_id>().find( name.to_uint64_t() );

    	//ilog("got itr");

    	if (itr!=_qcc_store.end()){

    		_qcc_store.modify(itr, [&]( auto& qcc ){
    			qcc._active = true;
	      	});

			throw std::runtime_error("duplicate qc chain"); 

    	}
    	else {

    		//ilog("new listener ${name}", ("name", name));

    		//_unique_replicas.push_back(name);

    		indexed_qc_chain iqcc;

			iqcc._name = name;	
			iqcc._active = true;
			iqcc._qc_chain = &qcc;

		//ilog(" === register_listener 1 ${my_producers}", ("my_producers", iqcc._qc_chain->_my_producers));

		_qcc_store.insert(iqcc);

		//ilog("aaadddd");

    	//auto itr = _qcc_store.get<by_name_id>().find( name.to_uint64_t() );

		//ilog(" === register_listener 2 ${my_producers}", ("my_producers", itr->_qc_chain->_my_producers));


    	} 
		

    };

    void test_pacemaker::unregister_listener(name name){

    	auto itr = _qcc_store.get<by_name_id>().find( name.to_uint64_t() );

    	if (itr!= _qcc_store.end()) {
    	
    		_qcc_store.modify(itr, [&]( auto& qcc ){
    			qcc._active = false;
	      	});
    	
    	}
    	else throw std::runtime_error("qc chain not found"); 

    };

	void test_pacemaker::send_hs_proposal_msg(name id, hs_proposal_message msg){

		//ilog("queuing hs_proposal_message : ${proposal_id} ", ("proposal_id", msg.proposal_id) );

		_pending_message_queue.push_back(std::make_pair(id, msg));

   	};

	void test_pacemaker::send_hs_vote_msg(name id, hs_vote_message msg){

		//ilog("queuing hs_vote_message : ${proposal_id} ", ("proposal_id", msg.proposal_id) );

		_pending_message_queue.push_back(std::make_pair(id, msg));

   	};

	void test_pacemaker::send_hs_new_block_msg(name id, hs_new_block_message msg){
		
		_pending_message_queue.push_back(std::make_pair(id, msg));

   	};

	void test_pacemaker::send_hs_new_view_msg(name id, hs_new_view_message msg){
   			
   		_pending_message_queue.push_back(std::make_pair(id, msg));

   	};

	void test_pacemaker::on_hs_proposal_msg(name id, hs_proposal_message msg){

    	//ilog(" === on_hs_proposal_msg");
		auto qc_itr = _qcc_store.begin();

		while (qc_itr!=_qcc_store.end()){

			//ilog("name : ${name}, active : ${active}", ("name", qc_itr->_name)("active", qc_itr->_active));

			if (qc_itr->_qc_chain == NULL) throw std::runtime_error("ptr is null");

			if (qc_itr->_qc_chain->_id != id && qc_itr->_active) qc_itr->_qc_chain->on_hs_proposal_msg(msg);

			qc_itr++;

		}

    	//ilog(" === end on_hs_proposal_msg");

	}

	void test_pacemaker::on_hs_vote_msg(name id, hs_vote_message msg){
		
    	//ilog(" === on_hs_vote_msg");
		auto qc_itr = _qcc_store.begin();

		while (qc_itr!=_qcc_store.end()){

			//ilog("name : ${name}, active : ${active}", ("name", qc_itr->_name)("active", qc_itr->_active));

			if (qc_itr->_qc_chain == NULL) throw std::runtime_error("ptr is null");

			if (qc_itr->_qc_chain->_id != id && qc_itr->_active) qc_itr->_qc_chain->on_hs_vote_msg(msg);

			qc_itr++;
		}

    	//ilog(" === end on_hs_vote_msg");

	}

	void test_pacemaker::on_hs_new_block_msg(name id, hs_new_block_message msg){
		
    	//ilog(" === on_hs_new_block_msg");
		auto qc_itr = _qcc_store.begin();

		while (qc_itr!=_qcc_store.end()){

			//ilog("name : ${name}, active : ${active}", ("name", qc_itr->_name)("active", qc_itr->_active));

			if (qc_itr->_qc_chain == NULL) throw std::runtime_error("ptr is null");

			if (qc_itr->_qc_chain->_id != id && qc_itr->_active) qc_itr->_qc_chain->on_hs_new_block_msg(msg);

			qc_itr++;
		}

    	//ilog(" === end on_hs_new_block_msg");

	}

	void test_pacemaker::on_hs_new_view_msg(name id, hs_new_view_message msg){
		
    	//ilog(" === on_hs_new_view_msg");
		auto qc_itr = _qcc_store.begin();

		while (qc_itr!=_qcc_store.end()){

			//ilog("name : ${name}, active : ${active}", ("name", qc_itr->_name)("active", qc_itr->_active));

			if (qc_itr->_qc_chain == NULL) throw std::runtime_error("ptr is null");

			if (qc_itr->_qc_chain->_id != id && qc_itr->_active) qc_itr->_qc_chain->on_hs_new_view_msg(msg);

			qc_itr++;
		}

    	//ilog(" === end on_hs_new_view_msg");

	}

}}