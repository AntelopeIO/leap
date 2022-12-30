#include <eosio/producer_plugin/qc_chain.hpp>

namespace eosio { namespace chain {
	
	digest_type qc_chain::get_digest_to_sign(consensus_msg_type msg_type, uint32_t view_number, digest_type digest_to_sign){

		string s_cmt = msg_type_to_string(msg_type);
		string s_view_number = to_string(view_number);

		string s_c = s_cmt + s_view_number;

		digest_type h1 = digest_type::hash(s_c);
		digest_type h2 = digest_type::hash( std::make_pair( h1, digest_to_sign ) );

		return h2;

	}

	void qc_chain::init(chain_plugin* chain_plug, std::set<chain::account_name> my_producers){
		
		std::vector<uint8_t> seed_1 = {  0,  50, 6,  244, 24,  199, 1,  25,  52,  88,  192,
	                            19, 18, 12, 89,  6,   220, 18, 102, 58,  209, 82,
	                            12, 62, 89, 110, 182, 9,   44, 20,  254, 22};

		ilog("init qc chain");

		_qc_chain_state = initializing;
		_my_producers = my_producers;

		_chain_plug = chain_plug;

		_private_key = fc::crypto::blslib::bls_private_key(seed_1);

	}

	//create a new view based on the block we just produced
	void qc_chain::create_new_view(block_state hbs){
		
		_view_number++;
		_view_leader = hbs.header.producer;
		_view_finalizers = hbs.active_schedule.producers;

		_qc_chain_state = leading_view;

		digest_type previous_bmroot = hbs.blockroot_merkle.get_root();
		digest_type schedule_hash = hbs.pending_schedule.schedule_hash;

		digest_type header_bmroot = digest_type::hash(std::make_pair(hbs.header.digest(), previous_bmroot));
		digest_type digest_to_sign = digest_type::hash(std::make_pair(header_bmroot, schedule_hash));

		consensus_node cn = {hbs.header, previous_bmroot, schedule_hash, digest_to_sign};

		std::optional<quorum_certificate> qc;

		if (_prepareQC.has_value()) qc = _prepareQC.value();
		else qc = std::nullopt;

		consensus_message msg = {cm_prepare, _view_number, cn, qc} ;

		ilog("creating new view #${view_number} : leader : ${view_leader}", 
			("view_number", _view_number)("view_leader", _view_leader));

		vector<name> finalizers;

		_currentQC = {msg.msg_type, msg.view_number, msg.node, finalizers, fc::crypto::blslib::bls_signature("")};;

		emit_new_phase(msg);


	}

	void qc_chain::request_new_view(){
		
		//ilog("request new view");

		_view_number++;

		_qc_chain_state = processing_view;

		//consensus_node cn = _prepareQC.node;
		//consensus_message msg = {cm_new_view, _view_number, cn, std::nullopt};

		//emit_new_phase(msg);

	}

	//called from network thread
	void qc_chain::on_confirmation_msg(confirmation_message msg){
		
		std::lock_guard g( this->_confirmation_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_confirmation_msg(msg, false);

	}

	//called from network thread
	void qc_chain::on_consensus_msg(consensus_message msg){

		std::lock_guard g( this->_consensus_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_consensus_msg(msg, false);

	}

    void qc_chain::process_confirmation_msg(confirmation_message msg, bool self_confirming){

	    auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == _view_leader; });

	    if (prod_itr==_my_producers.end()) return; //if we're not producing, we can ignore any confirmation messages

/*   		ilog("got notified of confirmation message: ${msg_type} for view  ${view_number} ${self_confirming}", 
   			("msg_type", msg.msg_type)
   			("view_number", msg.view_number)
   			("self_confirming", self_confirming));*/

	    auto itr = std::find_if(_processed_confirmation_msgs.begin(), _processed_confirmation_msgs.end(), [ &msg](confirmation_message m){ 
	    	return 	m.msg_type == msg.msg_type &&  
	    			m.view_number == msg.view_number && 
	    			m.node.digest_to_sign == msg.node.digest_to_sign &&
	    			m.finalizer == msg.finalizer; 
	    });

	    if (itr!=_processed_confirmation_msgs.end()) {
	    	//ilog("WRONG already processed this message");
	    	return; //already processed
	    }
	    else{
	    	//ilog("new confirmation message. Processing...");
	    	_processed_confirmation_msgs.push_back(msg);
	    	
	    	if (_processed_confirmation_msgs.size()==100) _processed_confirmation_msgs.erase(_processed_confirmation_msgs.begin());

	    }

		if (_currentQC.msg_type == msg.msg_type && //check if confirmation message is for that QC
			_currentQC.view_number == msg.view_number){

			if (std::find(_currentQC.finalizers.begin(), _currentQC.finalizers.end(), msg.finalizer) == _currentQC.finalizers.end()){

				//ilog("new finalizer vote received for this QC");

				//verify signature
				fc::crypto::blslib::bls_public_key pk = _private_key.get_public_key();

				digest_type digest = get_digest_to_sign(msg.msg_type, msg.view_number, msg.node.digest_to_sign );

				std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

				bool ok = verify(pk, h, msg.sig);

/*				ilog("verification - key: ${pk} hash: ${h} sig: ${sig}", 
					("agg_pk", pk.to_string())
					("h", h)
					("sig", msg.sig.to_string()));*/

				if (ok==false){
					//ilog("WRONG signature invalid");
					return;
				}

				fc::crypto::blslib::bls_signature n_sig;

				if (_currentQC.finalizers.size() == 0) n_sig = msg.sig;
				else n_sig = fc::crypto::blslib::aggregate({_currentQC.sig,msg.sig});

/*				ilog("n_sig updated : ${n_sig}",
					("n_sig", n_sig.to_string()));*/

				_currentQC.sig = n_sig;
				_currentQC.finalizers.push_back(msg.finalizer);

				if (_currentQC.finalizers.size()==14){

					ilog("reached quorum on ${msg_type}, can proceed with next phase",
						("msg_type", msg.msg_type));

					//received enough confirmations to move to next phase
					consensus_msg_type next_phase;

					switch (_currentQC.msg_type) {
					  case cm_prepare:
					  	next_phase = cm_pre_commit;
					  	_prepareQC = _currentQC;
					    break;
					  case cm_pre_commit:
					  	next_phase = cm_commit;
					    break;
					  case cm_commit:
					  	next_phase = cm_decide;
					    break;
					}

					consensus_message n_msg = {next_phase, _currentQC.view_number, _currentQC.node, _currentQC};

					vector<name> finalizers;

					quorum_certificate qc = {next_phase, _currentQC.view_number, _currentQC.node, finalizers, fc::crypto::blslib::bls_signature("")};

					_currentQC = qc;

					emit_new_phase(n_msg);

					//ilog("sent next phase message");

					if (next_phase==cm_decide){

						uint32_t block_height = n_msg.node.header.block_num();

						chain::controller& chain = _chain_plug->chain();

					   	const auto& hbs = chain.head_block_state();
						
						uint32_t distance_from_head = hbs->header.block_num() - block_height;

						ilog("decide decision has been reached on view #${view_number}. Block #${block_height} can be commited safely. Distance from head : ${distance_from_head}",
							("view_number", msg.view_number)
							("block_height", block_height)
							("distance_from_head", distance_from_head));

						_qc_chain_state=finished_view;

					   	//if we're still producing, we can start a new view
						if (std::find(_my_producers.begin(), _my_producers.end(), hbs->header.producer) != _my_producers.end()){
							create_new_view(*hbs);
					   	}

					}

				}
				else {
					//uint32_t remaining = 14 - _currentQC.finalizers.size();

					//ilog("need ${remaining} more votes to move to next phase", ("remaining", remaining));
				}

			}
			else {
				//ilog("WRONG already received vote for finalizer on this QC ");


			}	

		}
		else {
			//confirmation applies to another message
			//ilog("WRONG QC");

		}

    }
    
    void qc_chain::process_consensus_msg(consensus_message msg, bool self_leading){

/*   		ilog("got notified of consensus message: ${msg_type} for view  ${view_number} ${self_leading}", 
   			("msg_type", msg.msg_type)
   			("view_number", msg.view_number)
   			("self_leading", self_leading));*/

	    auto itr = std::find_if(_processed_consensus_msgs.begin(), _processed_consensus_msgs.end(), [ &msg](consensus_message m){ 
	    	return 	m.msg_type == msg.msg_type &&  
	    			m.view_number == msg.view_number && 
	    			m.node.digest_to_sign == msg.node.digest_to_sign; 
	    });

	    if (itr!=_processed_consensus_msgs.end()){
	    	//ilog("WRONG already processed this message");
	    	return; //already processed
	    } 
	    else {
			//ilog("new consensus message. Processing...");
	    	_processed_consensus_msgs.push_back(msg);

	    	if (_processed_consensus_msgs.size()==100) _processed_consensus_msgs.erase(_processed_consensus_msgs.begin());

	    }

		//TODO validate message

		digest_type digest = get_digest_to_sign(msg.msg_type, msg.view_number, msg.node.digest_to_sign );

		std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

		//if we're leading the view, reject the consensus message
		//if (_qc_chain_state==leading_view) return;
		
 		if (msg.justify.has_value()) {

 			auto justify = msg.justify.value();

 			if (justify.finalizers.size() == 14){

	 			fc::crypto::blslib::bls_public_key agg_pk = _private_key.get_public_key();

	 			//verify QC
				for (size_t i = 1 ; i < justify.finalizers.size();i++){
					agg_pk = fc::crypto::blslib::aggregate({agg_pk,_private_key.get_public_key()});
				}

				digest_type digest_j = get_digest_to_sign(justify.msg_type, justify.view_number, justify.node.digest_to_sign );
				std::vector<uint8_t> hj = std::vector<uint8_t>(digest_j.data(), digest_j.data() + 32);

/*				ilog("agg verification - key: ${agg_pk} hash: ${hj} sig: ${sig}", 
					("agg_pk", agg_pk.to_string())
					("hj", hj)
					("sig", justify.sig.to_string()));*/

				bool ok = verify(agg_pk, hj, justify.sig);

				if (ok==false){
					//ilog("WRONG aggregate signature invalid");
					return;
				}

				_view_number = msg.view_number;

	 			if (justify.msg_type == cm_pre_commit){
	 				_prepareQC = justify;
	 			}
	 			else if (justify.msg_type == cm_pre_commit){
	 				_lockedQC = justify;
	 			}
 			}
 			else {
 				
 				//ilog("WRONG invalid consensus message justify argument");

 				return ;
 			}
 		}

		if (_qc_chain_state==initializing || _qc_chain_state==finished_view ) {
			_view_number = msg.view_number;
			_view_leader = msg.node.header.producer;

			chain::controller& chain = _chain_plug->chain();

		   	const auto& hbs = chain.head_block_state();
		   	
			_view_finalizers = hbs->active_schedule.producers;

			_qc_chain_state=processing_view;

		}

		//if we received a commit decision and we are not also leading this round
		if (msg.msg_type == cm_decide && self_leading == false){

			uint32_t block_height = msg.node.header.block_num();

			chain::controller& chain = _chain_plug->chain();

		   	const auto& hbs = chain.head_block_state();
			
			uint32_t distance_from_head = hbs->header.block_num() - block_height;

			ilog("decide decision has been reached on view #${view_number}. Block #${block_height} can be commited safely. Distance from head : ${distance_from_head}",
				("view_number", msg.view_number)
				("block_height", block_height)
				("distance_from_head", distance_from_head));

		   	//if current producer is not previous view leader, we must send a new_view message with our latest prepareQC
			if (hbs->header.producer != _view_leader){
				//_view_number++;
				_view_leader = hbs->header.producer;
				_qc_chain_state=finished_view;
		   	}

		   	return;

		}
		else {

			auto p_itr = _my_producers.begin();

			while(p_itr!= _my_producers.end()){

				chain::account_name finalizer = *p_itr;

				auto itr = std::find_if(_view_finalizers.begin(), _view_finalizers.end(), [&](const auto& asp){ return asp.producer_name == finalizer; });

				if (itr!= _view_finalizers.end()){

					//ilog("Signing confirmation...");

					fc::crypto::blslib::bls_signature sig = _private_key.sign(h);;

/*					ilog("signing confirmation message : ${h} - ${sig}",
						("h", h)
						("sig", sig.to_string()));*/
					
					confirmation_message n_msg = {msg.msg_type, msg.view_number, msg.node, finalizer, sig};

					//ilog("Sending confirmation message for ${finalizer}", ("finalizer", finalizer));

					emit_confirm(n_msg);

				}
				else {
					//finalizer not in view schedule
					//ilog("WRONG consensus ${finalizer}", ("finalizer", finalizer));

				}

				p_itr++;
			}
		
		}
    }

	void qc_chain::emit_confirm(confirmation_message msg){

 		chain::controller& chain = _chain_plug->chain();
 		
/* 		ilog("emit confirm ${msg_type}... view #${view_number} on block ${block_id}, digest to sign is : ${digest} ",
                 ("msg_type",msg.msg_type)
                 ("view_number",msg.view_number)
                 ("block_id",msg.node.header.calculate_id()) 
                 ("digest",msg.node.digest_to_sign) );*/

 		confirmation_message_ptr ptr = std::make_shared<confirmation_message>(msg);

 		chain.commit_confirmation_msg(ptr);

 		process_confirmation_msg(msg, true); //notify ourselves, in case we are also the view leader

	}

	void qc_chain::emit_new_phase(consensus_message msg){
		
 		chain::controller& chain = _chain_plug->chain();
 		
 		ilog("emit new phase ${msg_type}... view #${view_number} on block #${block_num}",
                 ("msg_type",msg.msg_type)
                 ("view_number",msg.view_number)
                 ("block_num",msg.node.header.block_num()) );
		

 		//if (msg.justify.has_value()){

 		//	auto justify = msg.justify.value();
 			
/* 			ilog("  justify : view #${view_number} on block ${block_id}, digest to sign is : ${digest} ",
                 ("msg_type",justify.msg_type)
                 ("view_number",justify.view_number)
                 ("block_id",justify.node.header.calculate_id()) 
                 ("digest",justify.node.digest_to_sign) );*/
		
 		//}

 		consensus_message_ptr ptr = std::make_shared<consensus_message>(msg);

 		chain.commit_consensus_msg(ptr);

 		process_consensus_msg(msg, true); //notify ourselves, in case we are also running finalizers

	}

	void qc_chain::on_new_view_interrupt(){
		
	}

	void qc_chain::commit(block_header header){
		
	}

	void qc_chain::print_state(){

		ilog("QC CHAIN STATE : ");

		ilog("  view number : ${view_number}, view leader : ${view_leader}",
			("view_number", _view_number)
			("view_leader", _view_leader));


		if (_prepareQC.has_value()){
			
			quorum_certificate prepareQC = _prepareQC.value();

			ilog("  prepareQC type: ${msg_type} view: #${view_number} block_num: ${block_num}",
				("msg_type", prepareQC.msg_type)
				("view_number", prepareQC.view_number)
				("block_num", prepareQC.node.header.block_num()));

			ilog("    finalizers : ");

			for (int i = 0 ; i < prepareQC.finalizers.size(); i++){
				ilog("  ${finalizer}",
					("finalizer", prepareQC.finalizers[i]));
			}

		}
		else {
			ilog("  no prepareQC");
		}


		if (_lockedQC.has_value()){

			quorum_certificate lockedQC = _lockedQC.value();

			ilog("  lockedQC type: ${msg_type} view: #${view_number} block_num: ${block_num}",
				("msg_type", lockedQC.msg_type)
				("view_number", lockedQC.view_number)
				("block_num", lockedQC.node.header.block_num()));

			ilog("    finalizers : ");
			
			for (int i = 0 ; i < lockedQC.finalizers.size(); i++){
				ilog("  ${finalizer}",
					("finalizer", lockedQC.finalizers[i]));
			}

		}
		else {
			ilog("  no _lockedQC");
		}

		ilog("  _currentQC type: ${msg_type} view: #${view_number} block_num: ${block_num}",
			("msg_type", _currentQC.msg_type)
			("view_number", _currentQC.view_number)
			("block_num", _currentQC.node.header.block_num()));

		ilog("    finalizers : ");
		
		for (int i = 0 ; i < _currentQC.finalizers.size(); i++){
			ilog("  ${finalizer}",
				("finalizer", _currentQC.finalizers[i]));
		}

		ilog("  _processed_confirmation_msgs count : ${count}",
			("count", _processed_confirmation_msgs.size()));

		ilog("  _processed_consensus_msgs count : ${count}",
			("count", _processed_consensus_msgs.size()));

/*             	

			   struct quorum_certificate {

			      consensus_msg_type      msg_type;
			      uint32_t                view_number;
			      consensus_node          node;

			      vector<name>            finalizers;
			      bls_signature_type      sig;

			   };

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

	}

}}