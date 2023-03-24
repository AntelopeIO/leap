#include <eosio/hotstuff/qc_chain.hpp>


//todo list / notes : 

/*



fork tests in unittests



network plugin versioning 

handshake_message.network_version

independant of protocol feature activation



separate library for hotstuff (look at SHIP libray used by state history plugin )


boost tests producer plugin test



regression tests python framework as a base



performance testing




*/



//
//	complete proposer / leader differentiation
// integration with new bls implementation
//
//	hotstuff as a library with its own tests (model on state history plugin + state_history library )
//
//	unit / integration tests -> producer_plugin + fork_tests tests as a model
//
//			test deterministic sequence
//			
//			test non-replica participation
// 		test finality vioaltion
// 		test loss of liveness
//
// 		test split chain
//
// integration with fork_db / LIB overhaul
//
// integration with performance testing
//
// regression testing ci/cd -> python regression tests
//
// add APIs for proof data
// 
//	add election proposal in block header
//
//	map proposers / finalizers / leader to new host functions
//
// support pause / resume producer
//
// keep track of proposals sent to peers
//
//	allow syncing of proposals 
//
// versioning of net protocol version
//
// protocol feature activation HOTSTUFF_CONSENSUS
//
// system contract update 1 -> allow BPs to register + prove their aggregate pub key. Allow existing BPs to unreg + reg without new aggregate key. Prevent new BPs from registering without proving aggregate pub key
//
//	system contract update 2 (once all or at least overwhelming majority of BPs added a bls key) -> skip BPs without a bls key in the selection, new host functions are available  
//
//


namespace eosio { namespace hotstuff {
   

	digest_type qc_chain::get_digest_to_sign(block_id_type block_id, uint8_t phase_counter, fc::sha256 final_on_qc){

      digest_type h1 = digest_type::hash( std::make_pair( block_id, phase_counter ) );
      digest_type h2 = digest_type::hash( std::make_pair( h1, final_on_qc ) );

      return h2;

	}

   std::vector<hs_proposal_message> qc_chain::get_qc_chain(fc::sha256 proposal_id){
   	
   	std::vector<hs_proposal_message> ret_arr;

		proposal_store_type::nth_index<0>::type::iterator b_2_itr = _proposal_store.get<by_proposal_id>().end();
		proposal_store_type::nth_index<0>::type::iterator b_1_itr = _proposal_store.get<by_proposal_id>().end();
		proposal_store_type::nth_index<0>::type::iterator b_itr = _proposal_store.get<by_proposal_id>().end();

		b_2_itr = _proposal_store.get<by_proposal_id>().find( proposal_id );
		if (b_2_itr->justify.proposal_id != NULL_PROPOSAL_ID) b_1_itr = _proposal_store.get<by_proposal_id>().find( b_2_itr->justify.proposal_id );
		if (b_1_itr->justify.proposal_id != NULL_PROPOSAL_ID) b_itr = _proposal_store.get<by_proposal_id>().find( b_1_itr->justify.proposal_id );

		if (b_2_itr!=_proposal_store.get<by_proposal_id>().end()) ret_arr.push_back(*b_2_itr);
		if (b_1_itr!=_proposal_store.get<by_proposal_id>().end()) ret_arr.push_back(*b_1_itr);
		if (b_itr!=_proposal_store.get<by_proposal_id>().end()) ret_arr.push_back(*b_itr);

		return ret_arr;

   }

	hs_proposal_message qc_chain::new_proposal_candidate(block_id_type block_id, uint8_t phase_counter) {
		
		hs_proposal_message b_new;

		b_new.block_id = block_id;
		b_new.parent_id =  _b_leaf;
		b_new.phase_counter = phase_counter;

		b_new.justify = _high_qc; //or null if no _high_qc upon activation or chain launch

		if (b_new.justify.proposal_id != NULL_PROPOSAL_ID){

			std::vector<hs_proposal_message> current_qc_chain = get_qc_chain(b_new.justify.proposal_id);

			size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

			if (chain_length>=2){

				auto itr = current_qc_chain.begin();
				
				hs_proposal_message b2 = *itr;
				itr++;
				hs_proposal_message b1 = *itr;

				if (b_new.parent_id == b2.proposal_id && b2.parent_id == b1.proposal_id) b_new.final_on_qc = b1.proposal_id;
				else {

					proposal_store_type::nth_index<0>::type::iterator p_itr;
					
					p_itr = _proposal_store.get<by_proposal_id>().find( b1.parent_id );

					b_new.final_on_qc = p_itr->final_on_qc;

				}

			}

		}

		b_new.proposal_id = get_digest_to_sign(b_new.block_id, b_new.phase_counter, b_new.final_on_qc);

	if (_log) ilog("=== ${id} creating new proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id} : parent_id ${parent_id} : justify ${justify}", 
			("id", _id)
			("block_num", b_new.block_num())
			("phase_counter", b_new.phase_counter)
			("proposal_id", b_new.proposal_id)
			("parent_id", b_new.parent_id)
			("justify", b_new.justify.proposal_id));

		return b_new;

	}

	void qc_chain::reset_qc(fc::sha256 proposal_id){

		if (_log) ilog(" === ${id} resetting qc : ${proposal_id}", ("proposal_id" , proposal_id)("id", _id));
		_current_qc.proposal_id = proposal_id;
		_current_qc.quorum_met = false;
		_current_qc.active_finalizers = {};
		_current_qc.active_agg_sig = fc::crypto::blslib::bls_signature();

	}

	hs_new_block_message qc_chain::new_block_candidate(block_id_type block_id) {
		
		hs_new_block_message b;

		b.block_id = block_id;
		b.justify = _high_qc; //or null if no _high_qc upon activation or chain launch

		return b;
	}

	bool qc_chain::evaluate_quorum(extended_schedule es, vector<name> finalizers, fc::crypto::blslib::bls_signature agg_sig, hs_proposal_message proposal){

      if (finalizers.size() < _pacemaker->get_quorum_threshold()){
         return false;
      }

      fc::crypto::blslib::bls_public_key agg_key;

      for (int i = 0; i < finalizers.size(); i++) {

      	//adding finalizer's key to the aggregate pub key
      	if (i==0) agg_key = _private_key.get_public_key(); 
         else agg_key = fc::crypto::blslib::aggregate({agg_key, _private_key.get_public_key() }); 

      }

      fc::crypto::blslib::bls_signature justification_agg_sig;

		if (proposal.justify.proposal_id != NULL_PROPOSAL_ID) justification_agg_sig = proposal.justify.active_agg_sig;

		digest_type digest = get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

		std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

      bool ok = fc::crypto::blslib::verify(agg_key, h, agg_sig);

      return ok;

   }

	bool qc_chain::is_quorum_met(eosio::chain::quorum_certificate qc, extended_schedule schedule, hs_proposal_message proposal){

		if (qc.quorum_met == true ) {
			return true; //skip evaluation if we've already verified quorum was met 
		}
		else {
			
			//ilog("qc : ${qc}", ("qc", qc));

			bool quorum_met = evaluate_quorum(schedule, qc.active_finalizers, qc.active_agg_sig, proposal);

			qc.quorum_met = quorum_met;

			return qc.quorum_met ;

		}

	}

	void qc_chain::init(name id, base_pacemaker& pacemaker, std::set<name> my_producers, bool logging_enabled){

		_id = id;
		_log = logging_enabled;

		_pacemaker = &pacemaker;
		
 		_my_producers = my_producers;

		_pacemaker->register_listener(id, *this);
 		
		if (_log) ilog(" === ${id} qc chain initialized ${my_producers}", ("my_producers", my_producers)("id", _id));

		//auto itr = _my_producers.begin();

		//ilog("bla");

		//ilog("name ${name}", ("name", *itr));

		//ilog("111");

	}

	bool qc_chain::am_i_proposer(){

		//ilog("am_i_proposer");

		name proposer = _pacemaker->get_proposer();

		//ilog("Proposer : ${proposer}", ("proposer", proposer));

	   auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == proposer; });

		//ilog("Found");

	  	if (prod_itr==_my_producers.end()) return false;
	   else return true;

	}

	bool qc_chain::am_i_leader(){

		//ilog("am_i_leader");

		name leader = _pacemaker->get_leader();

		//ilog("Leader : ${leader}", ("leader", leader));

	    auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == leader; });

	    if (prod_itr==_my_producers.end()) return false;
	    else return true;

	}

	bool qc_chain::am_i_finalizer(){

			//ilog("am_i_finalizer");

		std::vector<name> finalizers = _pacemaker->get_finalizers();

		auto mf_itr = _my_producers.begin();

		while(mf_itr!=_my_producers.end()){

			name n = *mf_itr;

			auto prod_itr = std::find_if(finalizers.begin(), finalizers.end(), [&](const auto& f){ return f == n; });

			if (prod_itr!=finalizers.end()) return true;

			mf_itr++;

		}
	   
	    return false;

	}

	hs_vote_message qc_chain::sign_proposal(hs_proposal_message proposal, name finalizer){

		_v_height = proposal.get_height();

		//fc::crypto::blslib::bls_signature agg_sig;

		//if (proposal.justify.proposal_id != NULL_PROPOSAL_ID) agg_sig = proposal.justify.active_agg_sig;

		digest_type digest = get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

		std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);


		fc::crypto::blslib::bls_signature sig = _private_key.sign(h); //todo : use appropriate private key for each producer

		hs_vote_message v_msg = {proposal.proposal_id, finalizer, sig};

		return v_msg;

		//ilog("signed proposal. Broadcasting for each of my producers");

/*		auto mf_itr = _my_producers.begin();

		while(mf_itr!=_my_producers.end()){

			auto prod_itr = std::find(finalizers.begin(), finalizers.end(), *mf_itr);

			if (prod_itr!=finalizers.end()) {

				fc::crypto::blslib::bls_signature sig = _private_key.sign(h); //todo : use appropriate private key for each producer

				name n = *prod_itr;

				hs_vote_message v_msg = {proposal.proposal_id, n, sig};

				send_hs_vote_msg(v_msg);

			};

			mf_itr++;

		}*/

	}

	void qc_chain::process_proposal(hs_proposal_message proposal){

		auto start = fc::time_point::now();

		auto pid_itr = _proposal_store.get<by_proposal_id>().find( proposal.proposal_id );

		if (pid_itr != _proposal_store.get<by_proposal_id>().end()) {
			ilog("*** ${id} proposal received twice : ${proposal_id}", ("id",_id)("proposal_id", proposal.proposal_id));
			return ; //already aware of proposal, nothing to do
		
		}

		auto hgt_itr = _proposal_store.get<by_proposal_height>().find( proposal.get_height() );

		if (hgt_itr != _proposal_store.get<by_proposal_height>().end()) {
			ilog("*** ${id} received two different proposals at the same height (${block_num}, ${phase_counter}) : Proposal #1 : ${proposal_id_1} Proposal #2 : ${proposal_id_2}",
				("id",_id)
				("block_num", hgt_itr->block_num())
				("phase_counter", hgt_itr->phase_counter)
				("proposal_id_1", hgt_itr->proposal_id)
				("proposal_id_2", proposal.proposal_id));

		}

	if (_log) ilog("=== ${id} received new proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id} : parent_id ${parent_id} justify ${justify}", 
			("id", _id)
			("block_num", proposal.block_num())
			("phase_counter", proposal.phase_counter)
			("proposal_id", proposal.proposal_id)
			("parent_id", proposal.parent_id)
			("justify", proposal.justify.proposal_id));

		_proposal_store.insert(proposal); //new proposal

		//if I am a finalizer for this proposal and the safenode predicate for a possible vote is true, sign
		bool am_finalizer = am_i_finalizer();
		bool node_safe = is_node_safe(proposal);

		bool signature_required = am_finalizer && node_safe;

		if (signature_required){
				
			//iterate over all my finalizers and sign / broadcast for each that is in the schedule
			std::vector<name> finalizers = _pacemaker->get_finalizers();

			auto mf_itr = _my_producers.begin();

			while(mf_itr!=_my_producers.end()){

				auto prod_itr = std::find(finalizers.begin(), finalizers.end(), *mf_itr);

				if (prod_itr!=finalizers.end()) {

					hs_vote_message v_msg = sign_proposal(proposal, *prod_itr);

					send_hs_vote_msg(v_msg);

				};

				mf_itr++;

			}

		/*	//ilog("signature required");

			_v_height = proposal.get_height();

			fc::crypto::blslib::bls_signature agg_sig;

			if (proposal.justify.proposal_id != NULL_PROPOSAL_ID) agg_sig = proposal.justify.active_agg_sig;

			digest_type digest = get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

			std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

			//iterate over all my finalizers and sign / broadcast for each that is in the schedule
			std::vector<name> finalizers = _pacemaker->get_finalizers();

			//ilog("signed proposal. Broadcasting for each of my producers");

			auto mf_itr = _my_producers.begin();

			while(mf_itr!=_my_producers.end()){

				auto prod_itr = std::find(finalizers.begin(), finalizers.end(), *mf_itr);

				if (prod_itr!=finalizers.end()) {

					fc::crypto::blslib::bls_signature sig = _private_key.sign(h); //todo : use appropriate private key for each producer

					name n = *prod_itr;

					hs_vote_message v_msg = {proposal.proposal_id, n, sig};

					send_hs_vote_msg(v_msg);

				};

				mf_itr++;

			}
*/

		}





		//update internal state
		update(proposal);

		//check for leader change
		leader_rotation_check();
	
		//ilog("process_proposal end");

		auto total_time = fc::time_point::now() - start;

		//if (_log) ilog(" ... process_proposal() total time : ${total_time}", ("total_time", total_time));

	}

	void qc_chain::process_vote(hs_vote_message vote){

		auto start = fc::time_point::now();

		//todo : check for duplicate or invalid vote. We will return in either case, but keep proposals for evidence of double signing

		bool am_leader = am_i_leader(); //am I leader?

		if(!am_leader) return;

		//ilog("=== Process vote from ${finalizer}", ("finalizer", vote.finalizer));

		//only leader need to take action on votes

		if (vote.proposal_id != _current_qc.proposal_id) return;

		proposal_store_type::nth_index<0>::type::iterator p_itr = _proposal_store.get<by_proposal_id>().find(vote.proposal_id  );

		if (p_itr==_proposal_store.get<by_proposal_id>().end()){
			ilog("*** ${id} couldn't find proposal", ("id",_id));

			ilog("*** ${id} vote : ${vote}", ("vote", vote)("id",_id));

			return;
		}

		bool quorum_met = _current_qc.quorum_met; //check if quorum already met
		
		if (!quorum_met){

			_current_qc.active_finalizers.push_back(vote.finalizer);

			if (_current_qc.active_finalizers.size()>1) _current_qc.active_agg_sig = fc::crypto::blslib::aggregate({_current_qc.active_agg_sig, vote.sig });
			else _current_qc.active_agg_sig = vote.sig;

         quorum_met = is_quorum_met(_current_qc, _schedule, *p_itr);

         if (quorum_met){

         	_current_qc.quorum_met = true;

				//ilog("=== Quorum met on #${block_num} ${proposal_id} ", ("block_num", p_itr->block_num())("proposal_id", vote.proposal_id));

				//ilog("=== update_high_qc : _current_qc ===");
				update_high_qc(_current_qc);

				//check for leader change
				leader_rotation_check();

				//if we're operating in event-driven mode and the proposal hasn't reached the decide phase yet
				if (_chained_mode==false && p_itr->phase_counter<3){

					if (_log) ilog(" === ${id} phase increment on proposal ${proposal_id}", ("proposal_id", vote.proposal_id)("id", _id));

					hs_proposal_message proposal_candidate;

					if (_pending_proposal_block == NULL_BLOCK_ID) proposal_candidate = new_proposal_candidate(p_itr->block_id, p_itr->phase_counter + 1 ); 
					else proposal_candidate = new_proposal_candidate(_pending_proposal_block, 0);
					
					reset_qc(proposal_candidate.proposal_id);
					
					if (_log) ilog(" === ${id} setting _pending_proposal_block to null (process_vote)", ("id", _id));
					_pending_proposal_block = NULL_BLOCK_ID;

					send_hs_proposal_msg(proposal_candidate);

					_b_leaf = proposal_candidate.proposal_id;

					if (_log) ilog(" === ${id} _b_leaf updated (process_vote): ${proposal_id}", ("proposal_id", proposal_candidate.proposal_id)("id", _id));

				}

         }
		
		}

		auto total_time = fc::time_point::now() - start;

		//if (_log) ilog(" ... process_vote() total time : ${total_time}", ("total_time", total_time));

	}
	
	void qc_chain::process_new_view(hs_new_view_message new_view){
		
		if (_log) ilog("=== ${id} process_new_view === ${qc}", ("qc", new_view.high_qc)("id", _id));
		update_high_qc(new_view.high_qc);

	}
	
	void qc_chain::process_new_block(hs_new_block_message msg){
		
		//ilog("=== Process new block ===");

	}

	void qc_chain::send_hs_proposal_msg(hs_proposal_message msg){

		//ilog("=== broadcast_hs_proposal ===");

 		//hs_proposal_message_ptr ptr = std::make_shared<hs_proposal_message>(msg);

 		_pacemaker->send_hs_proposal_msg(_id, msg);

		process_proposal(msg);

	}


	void qc_chain::send_hs_vote_msg(hs_vote_message msg){

		//ilog("=== broadcast_hs_vote ===");

 		//hs_vote_message_ptr ptr = std::make_shared<hs_vote_message>(msg);

 		_pacemaker->send_hs_vote_msg(_id, msg);

		process_vote(msg);

	}

	void qc_chain::send_hs_new_view_msg(hs_new_view_message msg){

		//ilog("=== broadcast_hs_new_view ===");

 		//hs_new_view_message_ptr ptr = std::make_shared<hs_new_view_message>(msg);

 		_pacemaker->send_hs_new_view_msg(_id, msg);

	}

	void qc_chain::send_hs_new_block_msg(hs_new_block_message msg){

		//ilog("=== broadcast_hs_new_block ===");

 		//hs_new_block_message_ptr ptr = std::make_shared<hs_new_block_message>(msg);

 		_pacemaker->send_hs_new_block_msg(_id, msg);

	}

	//extends predicate
	bool qc_chain::extends(fc::sha256 descendant, fc::sha256 ancestor){

		//todo : confirm the extends predicate never has to verify extension of irreversible blocks, otherwise this function needs to be modified

		proposal_store_type::nth_index<0>::type::iterator itr = _proposal_store.get<by_proposal_id>().find(descendant  );

		uint32_t counter = 0;

		while (itr!=_proposal_store.get<by_proposal_id>().end()){

			itr  = _proposal_store.get<by_proposal_id>().find(itr->parent_id  );

			if (itr->proposal_id == ancestor){
				if (counter>25) {
					ilog("*** ${id} took ${counter} iterations to find ancestor ", ("id",_id)("counter", counter));
				
				}
				return true;
			}

			counter++;

		}

		ilog(" *** ${id} extends returned false : could not find ${d_proposal_id} descending from ${a_proposal_id} ",
				("id",_id)
				("d_proposal_id", descendant)
				("a_proposal_id", ancestor));

		return false;

	}

	void qc_chain::on_beat(){
std::exception_ptr eptr;
try{
	
		auto start = fc::time_point::now();

		if (_log) ilog(" === ${id} on beat === ", ("id", _id));

		//std::lock_guard g( this-> _hotstuff_state_mutex );

		name current_producer = _pacemaker->get_leader();

		if (current_producer == "eosio"_n) return;

		//ilog("current_producer : ${current_producer}", ("current_producer", current_producer));

		block_id_type current_block_id = _pacemaker->get_current_block_id();

		//ilog("current_block_id : ${current_block_id}", ("current_block_id", current_block_id));

		//ilog(" === qc chain on_beat ${my_producers}", ("my_producers", _my_producers));

		//ilog("222");

		bool am_proposer = am_i_proposer();

		//ilog("am i proposer received");

		bool am_leader = am_i_leader();

		if (_log) ilog(" === ${id} am_proposer = ${am_proposer}", ("am_proposer", am_proposer)("id", _id));
		if (_log) ilog(" === ${id} am_leader = ${am_leader}", ("am_leader", am_leader)("id", _id));
		
		if (!am_proposer && !am_leader){

			return; //nothing to do

		}

		//if I am the leader
		if (am_leader){

			//if I'm not also the proposer, perform block validation as required
			if (!am_proposer){

				//todo : extra validation?

			}
			

			if (_current_qc.proposal_id != NULL_PROPOSAL_ID && _current_qc.quorum_met == false){

				if (_log) ilog(" === ${id} pending proposal found ${proposal_id} : quorum met ${quorum_met}",
					("id", _id) 
					("proposal_id", _current_qc.proposal_id)
					("quorum_met", _current_qc.quorum_met));

				if (_log) ilog(" === ${id} setting _pending_proposal_block to ${block_id} (on_beat)", ("id", _id)("block_id", current_block_id));
				_pending_proposal_block = current_block_id;

			}
			else {

				if (_log) ilog(" === ${id} preparing new proposal ${proposal_id} : quorum met ${quorum_met}", 
					("id", _id)
					("proposal_id", _current_qc.proposal_id)
					("quorum_met", _current_qc.quorum_met));

				hs_proposal_message proposal_candidate = new_proposal_candidate(current_block_id, 0 ); 

				reset_qc(proposal_candidate.proposal_id);

				if (_log) ilog(" === ${id} setting _pending_proposal_block to null (on_beat)", ("id", _id));
				_pending_proposal_block = NULL_BLOCK_ID;
				
				send_hs_proposal_msg(proposal_candidate);

				_b_leaf = proposal_candidate.proposal_id;

				if (_log) ilog(" === ${id} _b_leaf updated (on_beat): ${proposal_id}", ("proposal_id", proposal_candidate.proposal_id)("id", _id));

			}

		}
		else {

			//if I'm only a proposer and not the leader, I send a new block message

			hs_new_block_message block_candidate = new_block_candidate(current_block_id);
		
			//ilog("=== broadcasting new block = #${block_height} ${proposal_id}", ("proposal_id", block_candidate.block_id)("block_height",compute_block_num(block_candidate.block_id) ));

			send_hs_new_block_msg(block_candidate);

		}
		
		auto total_time = fc::time_point::now() - start;

		//if (_log) ilog(" ... on_beat() total time : ${total_time}", ("total_time", total_time));

		//ilog(" === end of on_beat");
}
catch (...){
	ilog("error during on_beat");
	eptr = std::current_exception(); // capture
}
handle_eptr(eptr);
	}

	void qc_chain::update_high_qc(eosio::chain::quorum_certificate high_qc){

		//ilog("=== check to update high qc ${proposal_id}", ("proposal_id", high_qc.proposal_id));

		// if new high QC is higher than current, update to new
		

		if (_high_qc.proposal_id == NULL_PROPOSAL_ID){

			_high_qc = high_qc;
			_b_leaf = _high_qc.proposal_id;

			if (_log) ilog(" === ${id} _b_leaf updated (update_high_qc) : ${proposal_id}", ("proposal_id", _high_qc.proposal_id)("id", _id));

		}
		else {

			proposal_store_type::nth_index<0>::type::iterator old_high_qc_prop;
			proposal_store_type::nth_index<0>::type::iterator new_high_qc_prop;

			old_high_qc_prop = _proposal_store.get<by_proposal_id>().find( _high_qc.proposal_id );
			new_high_qc_prop = _proposal_store.get<by_proposal_id>().find( high_qc.proposal_id );

			if (old_high_qc_prop == _proposal_store.get<by_proposal_id>().end()) return; //ilog(" *** CAN'T FIND OLD HIGH QC PROPOSAL");
			if (new_high_qc_prop == _proposal_store.get<by_proposal_id>().end()) return; //ilog(" *** CAN'T FIND NEW HIGH QC PROPOSAL");


			if (new_high_qc_prop->get_height()>old_high_qc_prop->get_height()){

				bool quorum_met = is_quorum_met(high_qc, _schedule, *new_high_qc_prop);

	         if (quorum_met){

	         	high_qc.quorum_met = true;

					//ilog("=== updated high qc, now is : #${get_height}  ${proposal_id}", ("get_height", new_high_qc_prop->get_height())("proposal_id", new_high_qc_prop->proposal_id));

					_high_qc = high_qc;
					_b_leaf = _high_qc.proposal_id;

					if (_log) ilog(" === ${id} _b_leaf updated (update_high_qc) : ${proposal_id}", ("proposal_id", _high_qc.proposal_id)("id", _id));

	         }

			}

		}

	}

	void qc_chain::leader_rotation_check(){

		//ilog("leader_rotation_check");

		//verify if leader changed

		name current_leader = _pacemaker->get_leader() ;
		name next_leader = _pacemaker->get_next_leader() ;

		if (current_leader != next_leader){

			if (_log) ilog(" /// ${id} rotating leader : ${old_leader} -> ${new_leader} ", 
						("id", _id)
						("old_leader", current_leader)
						("new_leader", next_leader));

			//leader changed, we send our new_view message

			reset_qc(NULL_PROPOSAL_ID);

			if (_log) ilog(" === ${id} setting _pending_proposal_block to null (leader_rotation_check)", ("id", _id));
			_pending_proposal_block = NULL_BLOCK_ID;

			hs_new_view_message new_view;

			new_view.high_qc = _high_qc;

			send_hs_new_view_msg(new_view);
		}


	}

	//safenode predicate
	bool qc_chain::is_node_safe(hs_proposal_message proposal){

		//ilog("=== is_node_safe ===");

		bool monotony_check = false;
		bool safety_check = false;
		bool liveness_check = false;
		bool final_on_qc_check = false;

		fc::sha256 upcoming_commit;

		if (proposal.justify.proposal_id == NULL_PROPOSAL_ID && _b_lock == NULL_PROPOSAL_ID) final_on_qc_check = true; //if chain just launched or feature just activated
		else {

			std::vector<hs_proposal_message> current_qc_chain = get_qc_chain(proposal.justify.proposal_id);

			size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

			if (chain_length>=2){

				auto itr = current_qc_chain.begin();
				
				hs_proposal_message b2 = *itr;
				itr++;
				hs_proposal_message b1 = *itr;

				if (proposal.parent_id == b2.proposal_id && b2.parent_id == b1.proposal_id) upcoming_commit = b1.proposal_id;
				else {

					proposal_store_type::nth_index<0>::type::iterator p_itr;
					
					p_itr = _proposal_store.get<by_proposal_id>().find( b1.parent_id );

					upcoming_commit = p_itr->final_on_qc;

				}

			}

			//abstracted [...]
			if (upcoming_commit == proposal.final_on_qc){
				final_on_qc_check = true;
			}

		}

		if (proposal.get_height() > _v_height){
			monotony_check = true;
		}
		
		if (_b_lock != NULL_PROPOSAL_ID){

			//Safety check : check if this proposal extends the chain I'm locked on
			if (extends(proposal.proposal_id, _b_lock)){
				safety_check = true;
			}

			//Liveness check : check if the height of this proposal's justification is higher than the height of the proposal I'm locked on. This allows restoration of liveness if a replica is locked on a stale block.
			if (proposal.justify.proposal_id == NULL_PROPOSAL_ID && _b_lock == NULL_PROPOSAL_ID) liveness_check = true; //if there is no justification on the proposal and I am not locked on anything, means the chain just launched or feature just activated
			else {
							
				proposal_store_type::nth_index<0>::type::iterator b_lock = _proposal_store.get<by_proposal_id>().find( _b_lock );
				proposal_store_type::nth_index<0>::type::iterator prop_justification = _proposal_store.get<by_proposal_id>().find( proposal.justify.proposal_id );

				if (prop_justification->get_height() > b_lock->get_height()){
					liveness_check = true;
				}
			}

		}
		else { 

			if (_log) ilog(" === ${id} not locked on anything, liveness and safety are true", ("id", _id));

			//if we're not locked on anything, means the protocol just activated or chain just launched
			liveness_check = true;
			safety_check = true;
		}

/*		ilog("=== final_on_qc_check : ${final_on_qc_check}, monotony_check : ${monotony_check}, liveness_check : ${liveness_check}, safety_check : ${safety_check}", 
			("final_on_qc_check", final_on_qc_check)
			("monotony_check", monotony_check)
			("liveness_check", liveness_check)
			("safety_check", safety_check));*/

		return final_on_qc_check && monotony_check && (liveness_check || safety_check); //return true if monotony check and at least one of liveness or safety check evaluated successfully

	}

	//on proposal received, called from network thread
	void qc_chain::on_hs_proposal_msg(hs_proposal_message msg){
std::exception_ptr eptr;
try{

		//ilog("=== ${id} qc on_hs_proposal_msg ===", ("id", _id));

		//std::lock_guard g( this-> _hotstuff_state_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_proposal(msg);
		
		//ilog(" === end of on_hs_proposal_msg");
}
catch (...){
	ilog(" *** ${id} error during on_hs_proposal_msg", ("id",_id));
	eptr = std::current_exception(); // capture
}
handle_eptr(eptr);
	}

	//on vote received, called from network thread
	void qc_chain::on_hs_vote_msg(hs_vote_message msg){
std::exception_ptr eptr;
try{

		//ilog("=== ${id} qc on_hs_vote_msg ===", ("id", _id));

		//std::lock_guard g( this-> _hotstuff_state_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_vote(msg);

		//ilog(" === end of on_hs_vote_msg");
	}
catch (...){
	ilog(" *** ${id} error during on_hs_vote_msg", ("id",_id));
	eptr = std::current_exception(); // capture
}
handle_eptr(eptr);
	}

	//on new view received, called from network thread
	void qc_chain::on_hs_new_view_msg(hs_new_view_message msg){
std::exception_ptr eptr;
try{

		//ilog("=== ${id} qc on_hs_new_view_msg ===", ("id", _id));

		//std::lock_guard g( this-> _hotstuff_state_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_new_view(msg);
		
		//ilog(" === end of on_hs_new_view_msg");
}
catch (...){
	ilog(" *** ${id} error during on_hs_new_view_msg", ("id",_id));
	eptr = std::current_exception(); // capture
}
handle_eptr(eptr);
	}

	//on new block received, called from network thread
	void qc_chain::on_hs_new_block_msg(hs_new_block_message msg){
std::exception_ptr eptr;	
try{

		//ilog("=== ${id} qc on_hs_new_block_msg ===", ("id", _id));

		//std::lock_guard g( this-> _hotstuff_state_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_new_block(msg);

		//ilog(" === end of on_hs_new_block_msg");
}
catch (...){
	ilog(" *** ${id} error during on_hs_new_block_msg", ("id",_id));
	eptr = std::current_exception(); // capture
}
handle_eptr(eptr);
	}

	void qc_chain::update(hs_proposal_message proposal){

		//ilog("=== update internal state ===");

		proposal_store_type::nth_index<0>::type::iterator b_lock;

		//if proposal has no justification, means we either just activated the feature or launched the chain, or the proposal is invalid
		if (proposal.justify.proposal_id == NULL_PROPOSAL_ID){
			if (_log) ilog(" === ${id} proposal has no justification ${proposal_id}", ("proposal_id", proposal.proposal_id)("id", _id));
			return;
		}

		std::vector<hs_proposal_message> current_qc_chain = get_qc_chain(proposal.justify.proposal_id);

		size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

		b_lock = _proposal_store.get<by_proposal_id>().find( _b_lock);

		//ilog("=== update_high_qc : proposal.justify ===");
		update_high_qc(proposal.justify);

		if (chain_length<1){
			if (_log) ilog(" === ${id} qc chain length is 0", ("id", _id));
			return;
		}

		auto itr = current_qc_chain.begin();
		hs_proposal_message b_2 = *itr;
		
		if (chain_length<2){
			if (_log) ilog(" === ${id} qc chain length is 1", ("id", _id));
			return;
		}

		itr++;

		hs_proposal_message b_1 = *itr;

		//if we're not locked on anything, means we just activated or chain just launched, else we verify if we've progressed enough to establish a new lock
		if (_b_lock == NULL_PROPOSAL_ID ||  b_1.get_height() > b_lock->get_height()){

			//ilog("setting _b_lock to ${proposal_id}", ("proposal_id",b_1.proposal_id ));
			_b_lock = b_1.proposal_id; //commit phase on b1

			if (_log) ilog(" === ${id} _b_lock updated : ${proposal_id}", ("proposal_id", b_1.proposal_id)("id", _id));

		}

		if (chain_length<3){
			if (_log) ilog(" === ${id} qc chain length is 2",("id", _id));
			return;
		}

		itr++;

		hs_proposal_message b = *itr;

/*		ilog("direct parent relationship verification : b_2.parent_id ${b_2.parent_id} b_1.proposal_id ${b_1.proposal_id} b_1.parent_id ${b_1.parent_id} b.proposal_id ${b.proposal_id} ",
			("b_2.parent_id",b_2.parent_id)
			("b_1.proposal_id", b_1.proposal_id)
			("b_1.parent_id", b_1.parent_id)
			("b.proposal_id", b.proposal_id));*/

		//direct parent relationship verification 
		if (b_2.parent_id == b_1.proposal_id && b_1.parent_id == b.proposal_id){

			//ilog("direct parent relationship verified");


			commit(b);
			
			//ilog("last executed proposal : #${block_num} ${block_id}", ("block_num", b.block_num())("block_id", b.block_id));

			//ilog("setting _b_exec to ${proposal_id}", ("proposal_id",b.proposal_id ));
			_b_exec = b.proposal_id; //decide phase on b
			_block_exec = b.block_id;

			gc_proposals( b.get_height()-1);

			//ilog("completed commit");

		}
		else {

			ilog(" *** ${id} could not verify direct parent relationship", ("id",_id));

			ilog(" *** b_2 #${block_num} ${b_2}", ("b_2", b_2)("block_num", b_2.block_num()));
			ilog(" *** b_1 #${block_num} ${b_1}", ("b_1", b_1)("block_num", b_1.block_num()));
			ilog(" *** b #${block_num} ${b}", ("b", b)("block_num", b.block_num()));

		}


	}

	void qc_chain::gc_proposals(uint64_t cutoff){

		//ilog("garbage collection on old data");

		auto end_itr = _proposal_store.get<by_proposal_height>().upper_bound(cutoff);

		while (_proposal_store.get<by_proposal_height>().begin() != end_itr){

			auto itr = _proposal_store.get<by_proposal_height>().begin();

			if (_log) ilog(" === ${id} erasing ${block_num} ${phase_counter} ${block_id} proposal_id ${proposal_id}",
				("id", _id) 
				("block_num", itr->block_num())
				("phase_counter", itr->phase_counter)
				("block_id", itr->block_id)
				("proposal_id", itr->proposal_id));

			_proposal_store.get<by_proposal_height>().erase(itr);


		}
		
	}

	void qc_chain::commit(hs_proposal_message proposal){

/*		ilog("=== attempting to commit proposal #${block_num} ${proposal_id} block_id : ${block_id} phase : ${phase_counter} parent_id : ${parent_id}", 
				("block_num", proposal.block_num())
				("proposal_id", proposal.proposal_id)
				("block_id", proposal.block_id)
				("phase_counter", proposal.phase_counter)
				("parent_id", proposal.parent_id));
		*/
		bool sequence_respected = false;

		proposal_store_type::nth_index<0>::type::iterator last_exec_prop = _proposal_store.get<by_proposal_id>().find( _b_exec );
	
/*		ilog("=== _b_exec proposal #${block_num} ${proposal_id} block_id : ${block_id} phase : ${phase_counter} parent_id : ${parent_id}", 
			("block_num", last_exec_prop->block_num())
			("proposal_id", last_exec_prop->proposal_id)
			("block_id", last_exec_prop->block_id)
			("phase_counter", last_exec_prop->phase_counter)
			("parent_id", last_exec_prop->parent_id));*/

		if (_b_exec==NULL_PROPOSAL_ID){
			//ilog("first block committed");
			sequence_respected = true;
		}
		else sequence_respected = last_exec_prop->get_height() < proposal.get_height();

		if (sequence_respected){
		
			proposal_store_type::nth_index<0>::type::iterator p_itr = _proposal_store.get<by_proposal_id>().find( proposal.parent_id );

			if (p_itr != _proposal_store.get<by_proposal_id>().end()){

				//ilog("=== recursively committing" );

				commit(*p_itr); //recursively commit all non-committed ancestor blocks sequentially first

			}

			if (_log) ilog(" === ${id} committed proposal #${block_num} phase ${phase_counter} block_id : ${block_id} proposal_id : ${proposal_id}", 
				("id", _id)
				("block_num", proposal.block_num())
				("phase_counter", proposal.phase_counter)
				("block_id", proposal.block_id)
				("proposal_id", proposal.proposal_id));
		
		}

	}

}}


