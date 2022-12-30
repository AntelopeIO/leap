#include <eosio/producer_plugin/qc_chain.hpp>
#include <eosio/chain/fork_database.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace eosio { namespace chain {
   using boost::multi_index_container;
   using namespace boost::multi_index;
   
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


   const block_id_type NULL_BLOCK_ID = block_id_type("00");

   const block_header_state_ptr NULL_BLOCK_HEADER_STATE_PTR = block_header_state_ptr();
   const block_state_ptr NULL_BLOCK_STATE_PTR = block_state_ptr();

   block_id_type _b_leaf = NULL_BLOCK_ID;
   block_id_type _b_lock = NULL_BLOCK_ID;
	block_id_type _b_exec = NULL_BLOCK_ID;
	
	eosio::chain::quorum_certificate _high_qc;

	uint32_t _dual_set_height = 0; //0 if single-set mode

	eosio::chain::extended_schedule _schedule;

    chain_plugin* _chain_plug = nullptr;
	std::set<name> _my_producers;

	struct by_block_id{};
	struct by_block_num{};

	typedef multi_index_container<
		eosio::chain::quorum_certificate, 
		indexed_by<
	      hashed_unique<
	        tag<by_block_id>,
      		BOOST_MULTI_INDEX_MEMBER(eosio::chain::quorum_certificate,block_id_type,block_id)
	   	  >,
	      ordered_non_unique<
	        tag<by_block_num>,
	        BOOST_MULTI_INDEX_CONST_MEM_FUN(eosio::chain::quorum_certificate,uint32_t,block_num)
	      >
	    >
	> qc_store_type;

	typedef multi_index_container<
		hs_proposal_message, 
		indexed_by<
	      hashed_unique<
	        tag<by_block_id>,
      		BOOST_MULTI_INDEX_MEMBER(hs_proposal_message,block_id_type,block_id)
	   	  >,
	      ordered_non_unique<
	        tag<by_block_num>,
	        BOOST_MULTI_INDEX_CONST_MEM_FUN(hs_proposal_message,uint32_t,block_num)
	      >
	    >
	> proposal_store_type;

	qc_store_type _qc_store;
	proposal_store_type _proposal_store;

	digest_type get_digest_to_sign(fc::crypto::blslib::bls_signature agg_sig, block_id_type block_id){

		digest_type h = digest_type::hash( std::make_pair( agg_sig, block_id ) );

		return h;

	}

	name qc_chain::get_proposer(){

		chain::controller& chain = _chain_plug->chain();

	   	const auto& hbs = chain.head_block_state();
	   	
		return hbs->header.producer;

	}

	name qc_chain::get_leader(){

		chain::controller& chain = _chain_plug->chain();

	   	const auto& hbs = chain.head_block_state();
	   	
		return hbs->header.producer;

	}

	name qc_chain::get_incoming_leader(){

		chain::controller& chain = _chain_plug->chain();

		//verify if leader changed
		signed_block_header current_block_header = chain.head_block_state()->header;

		block_timestamp_type next_block_time = current_block_header.timestamp.next();

		producer_authority p_auth = chain.head_block_state()->get_scheduled_producer(next_block_time);

		return p_auth.producer_name ;

	}

	std::vector<producer_authority> qc_chain::get_finalizers(){

		chain::controller& chain = _chain_plug->chain();

		const auto& hbs = chain.head_block_state();

		return hbs->active_schedule.producers;

	}

	hs_proposal_message qc_chain::new_proposal_candidate(block_state& hbs) {
		
		hs_proposal_message b;

		b.block_id = hbs.header.calculate_id();
		b.justify = _high_qc; //or null if no _high_qc upon activation or chain launch

		return b;
	}

	hs_new_block_message qc_chain::new_new_block_candidate(block_state& hbs) {
		
		hs_new_block_message b;

		b.block_id = hbs.header.calculate_id();
		b.justify = _high_qc; //or null if no _high_qc upon activation or chain launch

		return b;
	}

	bool _quorum_met(extended_schedule es, vector<name> finalizers, fc::crypto::blslib::bls_signature agg_sig){
         
         //ilog("evaluating if _quorum_met");

         if (finalizers.size() != _threshold){
         
            //ilog("finalizers.size() ${size}", ("size",finalizers.size()));
            return false;
         
         }

         //ilog("correct threshold");
         
         /* fc::crypto::blslib::bls_public_key agg_key;

         for (name f : finalizers) {

            auto itr = es.bls_pub_keys.find(f);

            if (itr==es.bls_pub_keys.end()) return false;

            agg_key = fc::crypto::blslib::aggregate({agg_key, itr->second });

         }

         std::vector<unsigned char> msg = std::vector<unsigned char>(block_id.data(), block_id.data() + 32);

         bool ok = fc::crypto::blslib::verify(agg_key, msg, agg_sig);

         return ok; */

         return true; //temporary

      }

	bool qc_chain::is_quorum_met(eosio::chain::quorum_certificate qc, extended_schedule schedule, bool dual_set_mode){

      if (  dual_set_mode && 
            qc.incoming_finalizers.has_value() && 
            qc.incoming_agg_sig.has_value()){
         return _quorum_met(schedule, qc.active_finalizers, qc.active_agg_sig) && _quorum_met(schedule, qc.incoming_finalizers.value(), qc.incoming_agg_sig.value());
      }
      else {
         return _quorum_met(schedule, qc.active_finalizers, qc.active_agg_sig);
      }

	}

	void qc_chain::init(chain_plugin& chain_plug, std::set<name> my_producers){

 		_chain_plug = &chain_plug;
 		_my_producers = my_producers;
 		
		ilog("qc chain initialized -> my producers : ");

		auto itr = _my_producers.begin();
		while ( itr != _my_producers.end()){

			ilog("${producer}", ("producer", *itr));

			itr++;
		}

	}

	block_header_state_ptr qc_chain::get_block_header( const block_id_type& id ){
		
		//ilog("get_block_header ");

 		chain::controller& chain = _chain_plug->chain();
		
		return chain.fork_db().get_block_header(id);
		
	}

	bool qc_chain::am_i_proposer(){

		name proposer = get_proposer();

			//ilog("Proposer : ${proposer}", ("proposer", proposer));

	    auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == proposer; });

	    if (prod_itr==_my_producers.end()) return false;
	    else return true;

	}
	
	bool qc_chain::am_i_incoming_leader(){

		name leader = get_incoming_leader();

			//ilog("Incoming leader : ${leader}", ("leader", leader));

	    auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == leader; });

	    if (prod_itr==_my_producers.end()) return false;
	    else return true;

	}

	bool qc_chain::am_i_leader(){

		name leader = get_leader();

			//ilog("Leader : ${leader}", ("leader", leader));

	    auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == leader; });

	    if (prod_itr==_my_producers.end()) return false;
	    else return true;

	}

	bool qc_chain::am_i_finalizer(){

			//ilog("am_i_finalizer");

		std::vector<producer_authority> finalizers = get_finalizers();

		auto mf_itr = _my_producers.begin();

		while(mf_itr!=_my_producers.end()){

			auto prod_itr = std::find_if(finalizers.begin(), finalizers.end(), [&](const auto& f){ return f.producer_name == *mf_itr; });

			if (prod_itr!=finalizers.end()) return true;

			mf_itr++;

		}
	   
	    return false;

	}

	void qc_chain::process_proposal(hs_proposal_message msg){

		//todo : block candidate validation hook (check if block is valid, etc.), return if not 
	
		/*

			First, we verify if we have already are aware of the proposal, and if the QC was updated

		*/

		ilog("=== Process proposal #${block_num} ${block_id}", ("block_id", msg.block_id)("block_num", msg.block_num()));

		auto itr = _proposal_store.get<by_block_id>().find( msg.block_id );

		if (itr != _proposal_store.get<by_block_id>().end()){

			ilog("duplicate proposal");
		
			return; //duplicate 
			
			//if (itr->justify.has_value() && msg.justify.has_value() && itr->justify.value().active_agg_sig == msg.justify.value().active_agg_sig) return; 

		} 
		else {
		
			ilog("new proposal. Adding to storage");

			_proposal_store.insert(msg); //new block proposal

		}

		//check if I'm finalizer

		//ilog("updating state");

		//update internal state
		update(msg);

		//ilog("checking if I should sign proposal");

		bool am_finalizer = am_i_finalizer();
		bool node_safe = is_node_safe(msg);

		//ilog("am_finalizer : ${am_finalizer}", ("am_finalizer", am_finalizer));
		//ilog("node_safe : ${node_safe}", ("node_safe", node_safe));

		bool signature_required = am_finalizer && node_safe;


		//if I am a finalizer for this proposal, test safenode predicate for possible vote
		if (signature_required){
			
			//ilog("signature required");

			_v_height = msg.block_num();

			/* 
				Sign message.	

				In Hotstuff, we need to sign a tuple of (msg.view_type, msg.view_number and msg.node).

				In our implementation, the view_type is generic, and the view_number and message node are both contained in the block_id.

				Therefore, we can ensure uniqueness by replacing the view_type with msg.block_candidate.justify.agg_sig.

				The digest to sign now becomes the tuple (msg.block_candidate.justify.agg_sig,  msg.block_candidate.block_id).

			*/

			fc::crypto::blslib::bls_signature agg_sig;

			if (msg.justify.has_value()) agg_sig = msg.justify.value().active_agg_sig;

			digest_type digest = get_digest_to_sign(agg_sig, msg.block_id);

			std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

			//iterate over all my finalizers and sign / broadcast for each that is in the schedule
			std::vector<producer_authority> finalizers = get_finalizers();

			ilog("signed proposal. Broadcasting for each of my producers");

			auto mf_itr = _my_producers.begin();

			while(mf_itr!=_my_producers.end()){

				auto prod_itr = std::find_if(finalizers.begin(), finalizers.end(), [&](const auto& f){ return f.producer_name == *mf_itr; });

				if (prod_itr!=finalizers.end()) {

					fc::crypto::blslib::bls_signature sig = _private_key.sign(h); //todo : use appropriate private key for each producer

					hs_vote_message v_msg = {msg.block_id, prod_itr->producer_name, sig};

					broadcast_hs_vote(v_msg);

				};

				mf_itr++;

			}

			//check for leader change
			on_leader_rotate(msg.block_id);
			
		}


	}

	void qc_chain::process_vote(hs_vote_message msg){

		//check for duplicate or invalid vote, return in either case
		//abstracted [...]

		bool am_leader = am_i_leader(); //am I leader?

		if(!am_leader) return;

		ilog("=== Process vote from ${finalizer}", ("finalizer", msg.finalizer));

		eosio::chain::quorum_certificate qc;

		//only leader need to take action on votes

		qc_store_type::nth_index<0>::type::iterator itr = _qc_store.get<by_block_id>().find( msg.block_id );

		if (itr!=_qc_store.get<by_block_id>().end()){

			bool quorum_met = is_quorum_met(*itr, _schedule, false);

			if (!quorum_met){
			
            _qc_store.modify( itr, [&]( auto& qc ) {
					qc.active_finalizers.push_back(msg.finalizer);
					qc.active_agg_sig = fc::crypto::blslib::aggregate({qc.active_agg_sig, msg.sig });
            });

            quorum_met = is_quorum_met(*itr, _schedule, false);

            if (quorum_met){

					ilog("=== Quorum met on #${block_num} : ${block_id}", ("block_num", compute_block_num(msg.block_id))("block_id", msg.block_id));

					update_high_qc(*itr);

		 			chain::controller& chain = _chain_plug->chain();

					//todo : optimistically-responsive liveness progress
	
            }
			
			}
			
		}
		else {

			ilog("  must create new qc for proposal");

			//new QC is created

			qc.block_id = msg.block_id;
			qc.active_finalizers.push_back(msg.finalizer);
			qc.active_agg_sig = msg.sig;

			_qc_store.insert(qc);

		}

	}
	
	void qc_chain::process_new_view(hs_new_view_message msg){
		
		ilog("=== Process new view ===");

		bool am_leader = am_i_leader(); //am I leader?

		if(!am_leader) return;

	}
	
	void qc_chain::process_new_block(hs_new_block_message msg){
		
		//ilog("=== Process new block ===");

	}

	void qc_chain::broadcast_hs_proposal(hs_proposal_message msg){

		//ilog("=== broadcast_hs_proposal ===");

 		chain::controller& chain = _chain_plug->chain();

 		hs_proposal_message_ptr ptr = std::make_shared<hs_proposal_message>(msg);

 		chain.commit_hs_proposal_msg(ptr);

		process_proposal(msg);

	}


	void qc_chain::broadcast_hs_vote(hs_vote_message msg){

		//ilog("=== broadcast_hs_vote ===");

 		chain::controller& chain = _chain_plug->chain();

 		hs_vote_message_ptr ptr = std::make_shared<hs_vote_message>(msg);

 		chain.commit_hs_vote_msg(ptr);

		process_vote(msg);

	}

	void qc_chain::broadcast_hs_new_view(hs_new_view_message msg){

		//ilog("=== broadcast_hs_new_view ===");

 		chain::controller& chain = _chain_plug->chain();

 		hs_new_view_message_ptr ptr = std::make_shared<hs_new_view_message>(msg);

 		chain.commit_hs_new_view_msg(ptr);

 		//process_new_view(msg); //notify ourselves

	}

	void qc_chain::broadcast_hs_new_block(hs_new_block_message msg){

		//ilog("=== broadcast_hs_new_block ===");

 		chain::controller& chain = _chain_plug->chain();

 		hs_new_block_message_ptr ptr = std::make_shared<hs_new_block_message>(msg);

 		chain.commit_hs_new_block_msg(ptr);

 		//process_new_block(msg); //notify ourselves

	}

	//extends predicate
	bool qc_chain::extends(block_id_type descendant, block_id_type ancestor){

		//todo : confirm the extends predicate never has to verify extension of irreversible blocks, otherwise this function needs to be modified

		block_header_state_ptr itr = get_block_header(descendant);
		//block_header_state_ptr a_itr = get_block_header(ancestor);

/*		if (a_itr == NULL_BLOCK_HEADER_STATE_PTR){
			ilog("ancestor does't exist, returning true");
			return true;
		}*/

		while (itr!=NULL_BLOCK_HEADER_STATE_PTR){

			itr = get_block_header(itr->header.previous);

			if (itr->id == ancestor) return true;


		}

		ilog(" ***** extends returned false : could not find #${d_block_num} ${d_block_id} descending from #${a_block_num} ${a_block_id} ",
				("d_block_num", compute_block_num(descendant))
				("d_block_id", descendant)
				("a_block_num", compute_block_num(ancestor))
				("a_block_id", ancestor));

		return false;

	}
	
	void qc_chain::on_beat(block_state& hbs){

		ilog("=== on beat ===");

		if (hbs.header.producer == "eosio"_n) return ; 

		bool am_proposer = am_i_proposer();
		bool am_leader = am_i_leader();

		//ilog("=== am_proposer = ${am_proposer}", ("am_proposer", am_proposer));
		//ilog("=== am_leader = ${am_leader}", ("am_leader", am_leader));
		
		if (!am_proposer && !am_leader){

			return; //nothing to do

		}

		//if I am the leader
		if (am_leader){

			//if I'm not also the proposer, perform block validation as required
			if (!am_proposer){

				//todo : extra validation 

			}

			hs_proposal_message block_candidate = new_proposal_candidate(hbs); 
		
			_b_leaf = block_candidate.block_id;

			ilog("=== broadcasting proposal = #${block_num} ${block_id}", ("block_id", block_candidate.block_id)("block_num", block_candidate.block_num()));

			broadcast_hs_proposal(block_candidate);

		}
		else {

			//if I'm only a proposer and not the leader, I send a new block message

			hs_new_block_message block_candidate = new_new_block_candidate(hbs);
		
			ilog("=== broadcasting new block = #${block_num} ${block_id}", ("block_id", block_candidate.block_id)("block_num", block_candidate.block_num()));

			broadcast_hs_new_block(block_candidate);

		}


	}

	void qc_chain::update_high_qc(eosio::chain::quorum_certificate high_qc){
		// if new high QC is higher than current, update to new
		if (high_qc.block_num()>_high_qc.block_num()){

			ilog("=== updating high qc, now is : #${block_num}  ${block_id}", ("block_num", compute_block_num(high_qc.block_id))("block_id", high_qc.block_id));

			_high_qc = high_qc;
			_b_leaf = _high_qc.block_id;

		} 

	}

	void qc_chain::on_leader_rotate(block_id_type block_id){

		//ilog("on_leader_rotate");

		chain::controller& chain = _chain_plug->chain();

		//verify if leader changed
		signed_block_header current_block_header = chain.head_block_state()->header;

		block_timestamp_type next_block_time = current_block_header.timestamp.next();

			ilog("timestamps : old ${old_timestamp} -> new ${new_timestamp} ", 
				("old_timestamp", current_block_header.timestamp)("new_timestamp", current_block_header.timestamp.next()));

		producer_authority p_auth = chain.head_block_state()->get_scheduled_producer(next_block_time);

		if (current_block_header.producer != p_auth.producer_name){

			ilog("=== rotating leader : ${old_leader} -> ${new_leader} ", 
				("old_leader", current_block_header.producer)("new_leader", p_auth.producer_name));

			//leader changed, we send our new_view message

			hs_new_view_message new_view;

			new_view.high_qc = _high_qc;

			broadcast_hs_new_view(new_view);
		}


	}

	//safenode predicate
	bool qc_chain::is_node_safe(hs_proposal_message proposal){

		//ilog("=== is_node_safe ===");

		bool monotony_check = false;
		bool safety_check = false;
		bool liveness_check = false;

		if (proposal.block_num() > _v_height){
			monotony_check = true;
		}
		
		if (_b_lock != NULL_BLOCK_ID){

			//Safety check : check if this proposal extends the chain I'm locked on
			if (extends(proposal.block_id, _b_lock)){
				safety_check = true;
			}

			//Liveness check : check if the height of this proposal's justification is higher than the height of the proposal I'm locked on. This allows restoration of liveness if a replica is locked on a stale block.
			if (!proposal.justify.has_value()) liveness_check = true;
			else if (proposal.justify.value().block_num() > compute_block_num(_b_lock)){
				liveness_check = true;
			}

		}
		else { 

			//if we're not locked on anything, means the protocol just activated or chain just launched
			liveness_check = true;
			safety_check = true;
		}

		ilog("=== safety check : monotony : ${monotony_check}, liveness : ${liveness_check}, safety : ${safety_check}", 
			("monotony_check", monotony_check)
			("liveness_check", liveness_check)
			("safety_check", safety_check));

		return monotony_check && (liveness_check || safety_check); //return true if monotony check and at least one of liveness or safety check evaluated successfully

	}

	//on proposal received, called from network thread
	void qc_chain::on_hs_proposal_msg(hs_proposal_message msg){

		//ilog("=== on_hs_proposal_msg ===");
		std::lock_guard g( this->_proposal_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_proposal(msg);
		
	}

	//on vote received, called from network thread
	void qc_chain::on_hs_vote_msg(hs_vote_message msg){
		
		//ilog("=== on_hs_vote_msg ===");
		std::lock_guard g( this->_vote_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_vote(msg);
		
	}

	//on new view received, called from network thread
	void qc_chain::on_hs_new_view_msg(hs_new_view_message msg){

		//ilog("=== on_hs_new_view_msg ===");
		std::lock_guard g( this->_new_view_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_new_view(msg);
		
	}

	//on new block received, called from network thread
	void qc_chain::on_hs_new_block_msg(hs_new_block_message msg){
		
		//ilog("=== on_hs_new_block_msg ===");
		std::lock_guard g( this->_new_block_mutex ); //lock mutex to prevent multiple concurrent threads from accessing code block

		process_new_block(msg);

	}

	void qc_chain::update(hs_proposal_message proposal){


		ilog("=== update internal state ===");

	 	chain::controller& chain = _chain_plug->chain();

		//proposal_store_type::nth_index<0>::type::iterator b_new_itr = _proposal_store.get<by_block_id>().find( proposal.block_id ); //guaranteed to exist

		//should all be guaranteed to exist ?
		proposal_store_type::nth_index<0>::type::iterator b_2_itr;
		proposal_store_type::nth_index<0>::type::iterator b_1_itr;
		proposal_store_type::nth_index<0>::type::iterator b_itr;
		
		b_2_itr = _proposal_store.get<by_block_id>().find( proposal.justify.value().block_id );
		b_1_itr = _proposal_store.get<by_block_id>().find( b_2_itr->justify.value().block_id );
		b_itr = _proposal_store.get<by_block_id>().find( b_1_itr->justify.value().block_id );

		block_header_state_ptr b_2_header = get_block_header(b_2_itr->block_id);
		block_header_state_ptr b_1_header = get_block_header(b_1_itr->block_id);
		block_header_state_ptr b_header = get_block_header(b_itr->block_id);

		ilog("b_2_itr->block_id : #${block_num}: ${block_id}" ,("block_num", compute_block_num(b_2_itr->block_id))("block_id", b_2_itr->block_id));
		ilog("b_1_itr->block_id : #${block_num}:${block_id}" ,("block_num", compute_block_num(b_1_itr->block_id))("block_id", b_1_itr->block_id));
		ilog("b_itr->block_id : #${block_num}:${block_id}" ,("block_num", compute_block_num(b_itr->block_id))("block_id", b_itr->block_id));

		//todo : check if pending transition of finalizer set exists


		if (b_2_itr==_proposal_store.get<by_block_id>().end()) return;
		//ilog("proposal.justify exists");

		update_high_qc(proposal.justify.value());
		
		if (b_1_itr==_proposal_store.get<by_block_id>().end()) return;
		//ilog("b_2_itr->justify exists");

		if (compute_block_num(b_1_itr->block_id) > compute_block_num(_b_lock)){
			ilog("commit phase on block : #${block_num}:${block_id}" ,("block_num", compute_block_num(b_1_itr->block_id))("block_id", b_1_itr->block_id));
			_b_lock = b_1_itr->block_id; //commit phase on b1
			//ilog("lock confirmed");
		}

		if (b_itr==_proposal_store.get<by_block_id>().end()) return;
		//ilog("b_1_itr->justify exists");

		ilog("parent relationship verification : b_2->previous ${b_2_previous} b_1->block_id ${b_1_block_id} b_1->previous ${b_1_previous} b->block_id ${b_block_id}", 
				("b_2_previous", b_2_header->header.previous)("b_1_block_id", b_1_itr->block_id)("b_1_previous",b_1_header->header.previous)("b_block_id",b_itr->block_id));

		//direct parent relationship verification 
		if (b_2_header->header.previous == b_1_itr->block_id && b_1_header->header.previous == b_itr->block_id){

			ilog("direct parent relationship verified");

			//if we are currently operating in dual set mode reaching this point, and the block we are about to commit has a height higher or equal to me._dual_set_height, it means we have reached extended quorum on a view ready to be committed, so we can transition into single_set mode again, where the incoming finalizer set becomes the active finalizer set
			if (_dual_set_height != 0 && compute_block_num(b_itr->block_id) >= _dual_set_height){

				ilog("transitionning out of dual set mode");

				//sanity check to verify quorum on justification for b (b1), should always evaluate to true
				//if (b_itr->justify.extended_quorum_met()){

					//reset internal state to single_set mode, with new finalizer set
					//me._schedule.block_finalizers = me_.schedule.incoming_finalizers;
					//me_.schedule.incoming_finalizers = null;
					//me._dual_set_height = -1;

				//}

			}

			commit(b_header);
			
			ilog("last executed block : #${block_num} ${block_id}", ("block_num", compute_block_num(b_itr->block_id))("block_id", b_itr->block_id));

			_b_exec = b_itr->block_id; //decide phase on b

			ilog("completed commit");

		}
		else {

			ilog("could not verify direct parent relationship");

		}


		//ilog("=== end update ===");

	}

	void qc_chain::commit(block_header_state_ptr block){
			
		block_header_state_ptr b_exec = get_block_header(_b_exec);

		bool sequence_respected;

		if (b_exec == NULL_BLOCK_HEADER_STATE_PTR) {
			ilog("first block committed");
			sequence_respected = true;
			
		}
		else sequence_respected = b_exec->header.block_num() < block->header.block_num();

		if (sequence_respected){
		
			block_header_state_ptr p_itr = get_block_header(block->header.previous);

			if (p_itr != NULL_BLOCK_HEADER_STATE_PTR){

				ilog("=== recursively committing" );

				commit(p_itr); //recursively commit all non-committed ancestor blocks sequentially first

			}

			//execute block cmd
			//abstracted [...]

			ilog("=== committed block #${block_id}", ("block_id", block->header.block_num()));
		
		}

	}

/*
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

*/

/*

    void qc_chain::process_confirmation_msg(confirmation_message msg, bool self_confirming){

	    auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == _view_leader; });

	    if (prod_itr==_my_producers.end()) return; //if we're not producing, we can ignore any confirmation messages

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

				if (ok==false){
					//ilog("WRONG signature invalid");
					return;
				}

				fc::crypto::blslib::bls_signature n_sig;

				if (_currentQC.finalizers.size() == 0) n_sig = msg.sig;
				else n_sig = fc::crypto::blslib::aggregate({_currentQC.sig,msg.sig});


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


	}
*/
}}


