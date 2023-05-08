#include <eosio/hotstuff/qc_chain.hpp>

/*

  Todo list / notes:
  - fork tests in unittests
  - network plugin versioning
  - handshake_message.network_version
  - independant of protocol feature activation
  - separate library for hotstuff (look at SHIP libray used by state history plugin )
  - boost tests producer plugin test
  - regression tests python framework as a base
  - performance testing
  - complete proposer / leader differentiation
  - integration with new bls implementation
  - hotstuff as a library with its own tests (model on state history plugin + state_history library )
  - unit / integration tests -> producer_plugin + fork_tests tests as a model
  - test deterministic sequence
  - test non-replica participation
  - test finality vioaltion
  - test loss of liveness
  - test split chain
  - store schedules and transition view height, and prune on commit
  - integration with fork_db / LIB overhaul
  - integration with performance testing
  - regression testing ci/cd -> python regression tests
  - implement bitset for efficiency
  - add APIs for proof data
  - add election proposal in block header
  - map proposers / finalizers / leader to new host functions
  - support pause / resume producer
  - keep track of proposals sent to peers
  - allow syncing of proposals
  - versioning of net protocol version
  - protocol feature activation HOTSTUFF_CONSENSUS
  - system contract update 1
  -- allow BPs to register + prove their aggregate pub key.
  -- Allow existing BPs to unreg + reg without new aggregate key.
  -- Prevent new BPs from registering without proving aggregate pub key
  - system contract update 2 (once all or at least overwhelming majority of BPs added a bls key)
  -- skip BPs without a bls key in the selection, new host functions are available
*/


// FIXME/REMOVE: remove all of this tracing
// Enables extra logging to help with debugging
//#define QC_CHAIN_TRACE_DEBUG


namespace eosio { namespace hotstuff {

   const hs_proposal_message* qc_chain::get_proposal(fc::sha256 proposal_id) {
#ifdef QC_CHAIN_SIMPLE_PROPOSAL_STORE
      if (proposal_id == NULL_PROPOSAL_ID)
         return nullptr;
      ph_iterator h_it = _proposal_height.find( proposal_id );
      if (h_it == _proposal_height.end())
         return nullptr;
      uint64_t proposal_height = h_it->second;
      ps_height_iterator psh_it = _proposal_stores_by_height.find( proposal_height );
      if (psh_it == _proposal_stores_by_height.end())
         return nullptr;
      proposal_store & pstore = psh_it->second;
      ps_iterator ps_it = pstore.find( proposal_id );
      if (ps_it == pstore.end())
         return nullptr;
      const hs_proposal_message & proposal = ps_it->second;
      return &proposal;
#else
      proposal_store_type::nth_index<0>::type::iterator itr = _proposal_store.get<by_proposal_id>().find( proposal_id );
      if (itr == _proposal_store.get<by_proposal_id>().end())
         return nullptr;
      return &(*itr);
#endif
   }

   bool qc_chain::insert_proposal(const hs_proposal_message & proposal) {
      std::lock_guard g( _state_mutex );
#ifdef QC_CHAIN_SIMPLE_PROPOSAL_STORE
      uint64_t proposal_height = proposal.get_height();
      ps_height_iterator psh_it = _proposal_stores_by_height.find( proposal_height );
      if (psh_it == _proposal_stores_by_height.end()) {
         _proposal_stores_by_height.emplace( proposal_height, proposal_store() );
         psh_it = _proposal_stores_by_height.find( proposal_height );
      }
      proposal_store & pstore = psh_it->second;
      const fc::sha256 & proposal_id = proposal.proposal_id;
      ps_iterator ps_it = pstore.find( proposal_id );
      if (ps_it != pstore.end())
         return false; // duplicate proposal insertion, so don't change anything actually
      _proposal_height.emplace( proposal_id, proposal_height );
      pstore.emplace( proposal_id, proposal );
      return true;
#else
      if (get_proposal( proposal.proposal_id ) != nullptr)
         return false;
      _proposal_store.insert(proposal); //new proposal
      return true;
#endif
   }

   void qc_chain::get_state( finalizer_state & fs ) {
      std::lock_guard g( _state_mutex );
      fs.chained_mode           = _chained_mode;
      fs.b_leaf                 = _b_leaf;
      fs.b_lock                 = _b_lock;
      fs.b_exec                 = _b_exec;
      fs.b_finality_violation   = _b_finality_violation;
      fs.block_exec             = _block_exec;
      fs.pending_proposal_block = _pending_proposal_block;
      fs.v_height               = _v_height;
      fs.high_qc                = _high_qc;
      fs.current_qc             = _current_qc;
      fs.schedule               = _schedule;
#ifdef QC_CHAIN_SIMPLE_PROPOSAL_STORE
      ps_height_iterator psh_it = _proposal_stores_by_height.begin();
      while (psh_it != _proposal_stores_by_height.end()) {
         proposal_store &pstore = psh_it->second;
         ps_iterator ps_it = pstore.begin();
         while (ps_it != pstore.end()) {
            fs.proposals.insert( *ps_it );
            ++ps_it;
         }
         ++psh_it;
      }
#else
      auto hgt_itr = _proposal_store.get<by_proposal_height>().begin();
      auto end_itr = _proposal_store.get<by_proposal_height>().end();
      while (hgt_itr != end_itr) {
         const hs_proposal_message & p = *hgt_itr;
         fs.proposals.emplace( p.proposal_id, p );
         ++hgt_itr;
      }
#endif
   }

   uint32_t qc_chain::positive_bits_count(fc::unsigned_int value){
      boost::dynamic_bitset b(21, value);
      uint32_t count = 0;
      for (boost::dynamic_bitset<>::size_type i = 0; i < b.size(); i++){
         if (b[i]==true)count++;
      }
      return count;
   }

   fc::unsigned_int qc_chain::update_bitset(fc::unsigned_int value, name finalizer ) {

#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === update bitset ${value} ${finalizer}",
           ("value", value)
           ("finalizer", finalizer));
#endif

      boost::dynamic_bitset b( 21, value );
      vector<name> finalizers = _pacemaker->get_finalizers();
      for (size_t i = 0; i < finalizers.size();i++) {
         if (finalizers[i] == finalizer) {
            b.flip(i);

#ifdef QC_CHAIN_TRACE_DEBUG
            ilog(" === finalizer found ${finalizer} new value : ${value}",
                 ("finalizer", finalizer)
                 ("value", b.to_ulong()));
#endif

            return b.to_ulong();
         }
      }
#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" *** finalizer not found ${finalizer}",
           ("finalizer", finalizer));
#endif
      throw std::runtime_error("qc_chain internal error: finalizer not found");
   }

   digest_type qc_chain::get_digest_to_sign(block_id_type block_id, uint8_t phase_counter, fc::sha256 final_on_qc){
      digest_type h1 = digest_type::hash( std::make_pair( block_id, phase_counter ) );
      digest_type h2 = digest_type::hash( std::make_pair( h1, final_on_qc ) );
      return h2;
   }

   std::vector<hs_proposal_message> qc_chain::get_qc_chain(fc::sha256 proposal_id) {
      std::vector<hs_proposal_message> ret_arr;
      const hs_proposal_message *b, *b1, *b2;
      b2 = get_proposal( proposal_id );
      if (b2 != nullptr) {
         ret_arr.push_back( *b2 );
         b1 = get_proposal( b2->justify.proposal_id );
         if (b1 != nullptr) {
            ret_arr.push_back( *b1 );
            b = get_proposal( b1->justify.proposal_id );
            if (b != nullptr)
               ret_arr.push_back( *b );
         }
      }
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
               const hs_proposal_message *p = get_proposal( b1.parent_id );
               //EOS_ASSERT( p != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", b1.parent_id) );
               if (p != nullptr) {
                  b_new.final_on_qc = p->final_on_qc;
               } else {
                  if (_errors) ilog(" *** ${id} expected to find proposal in new_proposal_candidate() but not found : ${proposal_id}", ("id",_id)("proposal_id", b1.parent_id));
               }
            }
         }
      }

      b_new.proposal_id = get_digest_to_sign(b_new.block_id, b_new.phase_counter, b_new.final_on_qc);

      if (_log)
         ilog(" === ${id} creating new proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id} : parent_id ${parent_id} : justify ${justify}",
              ("id", _id)
              ("block_num", b_new.block_num())
              ("phase_counter", b_new.phase_counter)
              ("proposal_id", b_new.proposal_id)
              ("parent_id", b_new.parent_id)
              ("justify", b_new.justify.proposal_id));

      return b_new;
   }

   void qc_chain::reset_qc(fc::sha256 proposal_id){
      std::lock_guard g( _state_mutex );
#ifdef QC_CHAIN_TRACE_DEBUG
      if (_log) ilog(" === ${id} resetting qc : ${proposal_id}", ("proposal_id" , proposal_id)("id", _id));
#endif
      _current_qc.proposal_id = proposal_id;
      _current_qc.quorum_met = false;
      _current_qc.active_finalizers = 0;
      _current_qc.active_agg_sig = fc::crypto::blslib::bls_signature();
   }

   hs_new_block_message qc_chain::new_block_candidate(block_id_type block_id) {
      hs_new_block_message b;
      b.block_id = block_id;
      b.justify = _high_qc; //or null if no _high_qc upon activation or chain launch
      return b;
   }

   bool qc_chain::evaluate_quorum(const extended_schedule & es, fc::unsigned_int finalizers, const fc::crypto::blslib::bls_signature & agg_sig, const hs_proposal_message & proposal){

      bool first = true;

      if (positive_bits_count(finalizers) < _pacemaker->get_quorum_threshold()){
         return false;
      }

      boost::dynamic_bitset fb(21, finalizers.value);
      fc::crypto::blslib::bls_public_key agg_key;

      for (boost::dynamic_bitset<>::size_type i = 0; i < fb.size(); i++) {
         if (fb[i] == 1){
            //adding finalizer's key to the aggregate pub key
            if (first) {
               first = false;
               agg_key = _private_key.get_public_key();
            }
            else agg_key = fc::crypto::blslib::aggregate({agg_key, _private_key.get_public_key() });
         }
      }

      // ****************************************************************************************************
      // FIXME/TODO: I removed this since it doesn't seem to be doing anything at the moment
      // ****************************************************************************************************
      //
      //fc::crypto::blslib::bls_signature justification_agg_sig;
      //
      //if (proposal.justify.proposal_id != NULL_PROPOSAL_ID) justification_agg_sig = proposal.justify.active_agg_sig;

      digest_type digest = get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

      std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

      bool ok = fc::crypto::blslib::verify(agg_key, h, agg_sig);

      return ok;
   }

   bool qc_chain::is_quorum_met(const eosio::chain::quorum_certificate & qc, const extended_schedule & schedule, const hs_proposal_message & proposal){

      if (qc.quorum_met) {
         return true; //skip evaluation if we've already verified quorum was met
      }
      else {
#ifdef QC_CHAIN_TRACE_DEBUG
         ilog(" === qc : ${qc}", ("qc", qc));
#endif
         // If the caller wants to update the quorum_met flag on its "qc" object, it will have to do so
         //   based on the return value of this method, since "qc" here is const.
         return evaluate_quorum(schedule, qc.active_finalizers, qc.active_agg_sig, proposal);
      }
   }


   qc_chain::qc_chain(name id, base_pacemaker* pacemaker, std::set<name> my_producers, bool info_logging, bool error_logging)
      : _id(id),
        _pacemaker(pacemaker),
        _my_producers(my_producers),
        _log(info_logging),
        _errors(error_logging)
   {
      if (_log) ilog(" === ${id} qc chain initialized ${my_producers}", ("my_producers", my_producers)("id", _id));
   }

   bool qc_chain::am_i_proposer(){
      name proposer = _pacemaker->get_proposer();
      auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == proposer; });
      if (prod_itr==_my_producers.end()) return false;
      else return true;
   }

   bool qc_chain::am_i_leader(){
      name leader = _pacemaker->get_leader();
      auto prod_itr = std::find_if(_my_producers.begin(), _my_producers.end(), [&](const auto& asp){ return asp == leader; });
      if (prod_itr==_my_producers.end()) return false;
      else return true;
   }

   bool qc_chain::am_i_finalizer(){
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

   hs_vote_message qc_chain::sign_proposal(const hs_proposal_message & proposal, name finalizer){

      std::unique_lock state_lock( _state_mutex );
      _v_height = proposal.get_height();
      state_lock.unlock();

      digest_type digest = get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

      std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

      fc::crypto::blslib::bls_signature sig = _private_key.sign(h); //FIXME/TODO: use appropriate private key for each producer

      hs_vote_message v_msg = {proposal.proposal_id, finalizer, sig};
      return v_msg;
   }

   void qc_chain::process_proposal(const hs_proposal_message & proposal){

      //auto start = fc::time_point::now();

      if (proposal.justify.proposal_id != NULL_PROPOSAL_ID){

         const hs_proposal_message *jp = get_proposal( proposal.justify.proposal_id );
         if (jp == nullptr) {
            if (_errors) ilog(" *** ${id} proposal justification unknown : ${proposal_id}", ("id",_id)("proposal_id", proposal.justify.proposal_id));
            return; //can't recognize a proposal with an unknown justification
         }
      }

      const hs_proposal_message *p = get_proposal( proposal.proposal_id );
      if (p != nullptr) {

         if (_errors) ilog(" *** ${id} proposal received twice : ${proposal_id}", ("id",_id)("proposal_id", proposal.proposal_id));

         if (p->justify.proposal_id != proposal.justify.proposal_id) {

            if (_errors) ilog(" *** ${id} two identical proposals (${proposal_id}) have different justifications :  ${justify_1} vs  ${justify_2}",
                              ("id",_id)
                              ("proposal_id", proposal.proposal_id)
                              ("justify_1", p->justify.proposal_id)
                              ("justify_2", proposal.justify.proposal_id));

         }

         return; //already aware of proposal, nothing to do
      }

#ifdef QC_CHAIN_SIMPLE_PROPOSAL_STORE
      ps_height_iterator psh_it = _proposal_stores_by_height.find( proposal.get_height() );
      if (psh_it != _proposal_stores_by_height.end())
      {
         proposal_store & pstore = psh_it->second;
         ps_iterator ps_it = pstore.begin();
         while (ps_it != pstore.end())
         {
            hs_proposal_message & existing_proposal = ps_it->second;
#else
      //height is not necessarily unique, so we iterate over all prior proposals at this height
      auto hgt_itr = _proposal_store.get<by_proposal_height>().lower_bound( proposal.get_height() );
      auto end_itr = _proposal_store.get<by_proposal_height>().upper_bound( proposal.get_height() );
      while (hgt_itr != end_itr)
      {
         const hs_proposal_message & existing_proposal = *hgt_itr;
#endif

         if (_errors) ilog(" *** ${id} received a different proposal at the same height (${block_num}, ${phase_counter})",
                           ("id",_id)
                           ("block_num", existing_proposal.block_num())
                           ("phase_counter", existing_proposal.phase_counter));

         if (_errors) ilog(" *** Proposal #1 : ${proposal_id_1} Proposal #2 : ${proposal_id_2}",
                           ("proposal_id_1", existing_proposal.proposal_id)
                           ("proposal_id_2", proposal.proposal_id));

#ifdef QC_CHAIN_SIMPLE_PROPOSAL_STORE
            ++ps_it;
         }
      }
#else
         hgt_itr++;
      }
#endif

      if (_log) ilog(" === ${id} received new proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id} : parent_id ${parent_id} justify ${justify}",
                     ("id", _id)
                     ("block_num", proposal.block_num())
                     ("phase_counter", proposal.phase_counter)
                     ("proposal_id", proposal.proposal_id)
                     ("parent_id", proposal.parent_id)
                     ("justify", proposal.justify.proposal_id));

      bool success = insert_proposal( proposal );
      EOS_ASSERT( success , chain_exception, "internal error: duplicate proposal insert attempt" ); // can't happen unless bad mutex somewhere; already checked for this

      //if I am a finalizer for this proposal and the safenode predicate for a possible vote is true, sign
      bool am_finalizer = am_i_finalizer();
      bool node_safe = is_node_safe(proposal);
      bool signature_required = am_finalizer && node_safe;

      std::vector<hs_vote_message> msgs;

      if (signature_required){

         //iterate over all my finalizers and sign / broadcast for each that is in the schedule
         std::vector<name> finalizers = _pacemaker->get_finalizers();

         auto mf_itr = _my_producers.begin();

         while(mf_itr!=_my_producers.end()){

            auto prod_itr = std::find(finalizers.begin(), finalizers.end(), *mf_itr);

            if (prod_itr!=finalizers.end()) {

               hs_vote_message v_msg = sign_proposal(proposal, *prod_itr);

#ifdef QC_CHAIN_TRACE_DEBUG
               if (_log) ilog(" === ${id} signed proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id}",
                              ("id", _id)
                              ("block_num", proposal.block_num())
                              ("phase_counter", proposal.phase_counter)
                              ("proposal_id", proposal.proposal_id));
#endif

               //send_hs_vote_msg(v_msg);
               msgs.push_back(v_msg);

            };

            mf_itr++;
         }
      }

#ifdef QC_CHAIN_TRACE_DEBUG
      else if (_log) ilog(" === ${id} skipping signature on proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id}",
                          ("id", _id)
                          ("block_num", proposal.block_num())
                          ("phase_counter", proposal.phase_counter)
                          ("proposal_id", proposal.proposal_id));
#endif

      //update internal state
      update(proposal);

      for (auto &msg : msgs) {
         send_hs_vote_msg(msg);
      }

      //check for leader change
      leader_rotation_check();

      //auto total_time = fc::time_point::now() - start;
      //if (_log) ilog(" ... process_proposal() total time : ${total_time}", ("total_time", total_time));
   }

   void qc_chain::process_vote(const hs_vote_message & vote){

      //auto start = fc::time_point::now();

      //TODO: check for duplicate or invalid vote. We will return in either case, but keep proposals for evidence of double signing

      bool am_leader = am_i_leader();

      if (!am_leader)
         return;
#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === Process vote from ${finalizer} : current bitset ${value}" , ("finalizer", vote.finalizer)("value", _current_qc.active_finalizers));
#endif
      // only leader need to take action on votes
      if (vote.proposal_id != _current_qc.proposal_id)
         return;

      const hs_proposal_message *p = get_proposal( vote.proposal_id );
      if (p == nullptr) {
         if (_errors) ilog(" *** ${id} couldn't find proposal", ("id",_id));
         if (_errors) ilog(" *** ${id} vote : ${vote}", ("vote", vote)("id",_id));
         return;
      }

      bool quorum_met = _current_qc.quorum_met; //check if quorum already met

      // If quorum is already met, we don't need to do anything else. Otherwise, we aggregate the signature.
      if (!quorum_met){

         std::unique_lock state_lock( _state_mutex );
         if (_current_qc.active_finalizers>0)
            _current_qc.active_agg_sig = fc::crypto::blslib::aggregate({_current_qc.active_agg_sig, vote.sig });
         else
            _current_qc.active_agg_sig = vote.sig;

         _current_qc.active_finalizers = update_bitset(_current_qc.active_finalizers, vote.finalizer);
         state_lock.unlock();

         quorum_met = is_quorum_met(_current_qc, _schedule, *p);

         if (quorum_met){

            if (_log) ilog(" === ${id} quorum met on #${block_num} ${phase_counter} ${proposal_id} ",
                           ("block_num", p->block_num())
                           ("phase_counter", p->phase_counter)
                           ("proposal_id", vote.proposal_id)
                           ("id", _id));

            state_lock.lock();
            _current_qc.quorum_met = true;
            state_lock.unlock();

            //ilog(" === update_high_qc : _current_qc ===");
            update_high_qc(_current_qc);

            //check for leader change
            leader_rotation_check();

            //if we're operating in event-driven mode and the proposal hasn't reached the decide phase yet
            if (_chained_mode == false && p->phase_counter < 3) {
#ifdef QC_CHAIN_TRACE_DEBUG
               if (_log) ilog(" === ${id} phase increment on proposal ${proposal_id}", ("proposal_id", vote.proposal_id)("id", _id));
#endif
               hs_proposal_message proposal_candidate;

               if (_pending_proposal_block == NULL_BLOCK_ID)
                  proposal_candidate = new_proposal_candidate( p->block_id, p->phase_counter + 1 );
               else
                  proposal_candidate = new_proposal_candidate( _pending_proposal_block, 0 );

               reset_qc(proposal_candidate.proposal_id);
#ifdef QC_CHAIN_TRACE_DEBUG
               if (_log) ilog(" === ${id} setting _pending_proposal_block to null (process_vote)", ("id", _id));
#endif
               state_lock.lock();
               _pending_proposal_block = NULL_BLOCK_ID;
               _b_leaf = proposal_candidate.proposal_id;
               state_lock.unlock();

               send_hs_proposal_msg(proposal_candidate);
#ifdef QC_CHAIN_TRACE_DEBUG
               if (_log) ilog(" === ${id} _b_leaf updated (process_vote): ${proposal_id}", ("proposal_id", proposal_candidate.proposal_id)("id", _id));
#endif
            }
         }
      }

      //auto total_time = fc::time_point::now() - start;
      //if (_log) ilog(" ... process_vote() total time : ${total_time}", ("total_time", total_time));
   }

   void qc_chain::process_new_view(const hs_new_view_message & msg){
#ifdef QC_CHAIN_TRACE_DEBUG
      if (_log) ilog(" === ${id} process_new_view === ${qc}", ("qc", msg.high_qc)("id", _id));
#endif
      update_high_qc(msg.high_qc);
   }

   void qc_chain::process_new_block(const hs_new_block_message & msg){

      // If I'm not a leader, I probably don't care about hs-new-block messages.
      // TODO: check for a need to gossip/rebroadcast even if it's not for us (maybe here, maybe somewhere else).
      if (! am_i_leader()) {

#ifdef QC_CHAIN_TRACE_DEBUG
         ilog(" === ${id} process_new_block === discarding because I'm not the leader; block_id : ${bid}, justify : ${just}", ("bid", msg.block_id)("just", msg.justify)("id", _id));
#endif
         return;
      }

#ifdef QC_CHAIN_TRACE_DEBUG
      if (_log) ilog(" === ${id} process_new_block === am leader; block_id : ${bid}, justify : ${just}", ("bid", msg.block_id)("just", msg.justify)("id", _id));
#endif

      // ------------------------------------------------------------------
      //
      //   FIXME/REVIEW/TODO: What to do with the received msg.justify?
      //
      //   We are the leader, and we got a block_id from a proposer, but
      //     we should probably do something with the justify QC that
      //     comes with it (which is the _high_qc of the proposer (?))
      //
      // ------------------------------------------------------------------

      if (_current_qc.proposal_id != NULL_PROPOSAL_ID && _current_qc.quorum_met == false) {

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} pending proposal found ${proposal_id} : quorum met ${quorum_met}",
                        ("id", _id)
                        ("proposal_id", _current_qc.proposal_id)
                        ("quorum_met", _current_qc.quorum_met));
         if (_log) ilog(" === ${id} setting _pending_proposal_block to ${block_id} (on_beat)", ("id", _id)("block_id", msg.block_id));
#endif
         std::unique_lock state_lock( _state_mutex );
         _pending_proposal_block = msg.block_id;
         state_lock.unlock();

      } else {

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} preparing new proposal ${proposal_id} : quorum met ${quorum_met}",
                        ("id", _id)
                        ("proposal_id", _current_qc.proposal_id)
                        ("quorum_met", _current_qc.quorum_met));
#endif
         hs_proposal_message proposal_candidate = new_proposal_candidate( msg.block_id, 0 );

         reset_qc(proposal_candidate.proposal_id);

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} setting _pending_proposal_block to null (process_new_block)", ("id", _id));
#endif
         std::unique_lock state_lock( _state_mutex );
         _pending_proposal_block = NULL_BLOCK_ID;
         _b_leaf = proposal_candidate.proposal_id;
         state_lock.unlock();

         send_hs_proposal_msg(proposal_candidate);

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} _b_leaf updated (on_beat): ${proposal_id}", ("proposal_id", proposal_candidate.proposal_id)("id", _id));
#endif
      }
   }

   void qc_chain::send_hs_proposal_msg(const hs_proposal_message & msg){
#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === broadcast_hs_proposal ===");
#endif
      _pacemaker->send_hs_proposal_msg(msg, _id);
      process_proposal(msg);
   }

   void qc_chain::send_hs_vote_msg(const hs_vote_message & msg){
#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === broadcast_hs_vote ===");
#endif
      _pacemaker->send_hs_vote_msg(msg, _id);
      process_vote(msg);
   }

   void qc_chain::send_hs_new_view_msg(const hs_new_view_message & msg){
#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === broadcast_hs_new_view ===");
#endif
      _pacemaker->send_hs_new_view_msg(msg, _id);
   }

   void qc_chain::send_hs_new_block_msg(const hs_new_block_message & msg){
#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === broadcast_hs_new_block ===");
#endif
      _pacemaker->send_hs_new_block_msg(msg, _id);
   }

   //extends predicate
   bool qc_chain::extends(fc::sha256 descendant, fc::sha256 ancestor){

      //TODO: confirm the extends predicate never has to verify extension of irreversible blocks, otherwise this function needs to be modified

      uint32_t counter = 0;
      const hs_proposal_message *p = get_proposal( descendant );
      while (p != nullptr) {
         fc::sha256 parent_id = p->parent_id;
         p = get_proposal( parent_id );
         if (p == nullptr) {
            if (_errors) ilog(" *** ${id} cannot find proposal id while looking for ancestor : ${proposal_id}", ("id",_id)("proposal_id", parent_id));
            return false;
         }
         if (p->proposal_id == ancestor) {
            if (counter > 25) {
               if (_errors) ilog(" *** ${id} took ${counter} iterations to find ancestor ", ("id",_id)("counter", counter));
            }
            return true;
         }
         ++counter;
      }

      if (_errors) ilog(" *** ${id} extends returned false : could not find ${d_proposal_id} descending from ${a_proposal_id} ",
                        ("id",_id)
                        ("d_proposal_id", descendant)
                        ("a_proposal_id", ancestor));

      return false;
   }

   // Invoked when we could perhaps make a proposal to the network (or to ourselves, if we are the leader).
   void qc_chain::on_beat(){

      // Non-proposing leaders do not care about on_beat(), because leaders react to a block proposal
      //   which comes from processing an incoming new block message from a proposer instead.
      // on_beat() is called by the pacemaker, which decides when it's time to check whether we are
      //   proposers that should check whether as proposers we should propose a new hotstuff block to
      //   the network (or to ourselves, which is faster and doesn't require the bandwidth of an additional
      //   gossip round for a new proposed block).
      // The current criteria for a leader selecting a proposal among all proposals it receives is to go
      //   with the first valid one that it receives. So if a proposer is also a leader, it silently goes
      //   with its own proposal, which is hopefully valid at the point of generation which is also the
      //   point of consumption.
      //
      if (! am_i_proposer())
         return;

      block_id_type current_block_id = _pacemaker->get_current_block_id();

      hs_new_block_message block_candidate = new_block_candidate( current_block_id );

      if (am_i_leader()) {

         // I am the proposer; so this assumes that no additional proposal validation is required.

#ifdef QC_CHAIN_TRACE_DEBUG
         ilog(" === I am a leader-proposer that is proposing a block for itself to lead");
#endif
         // Hardwired consumption by self; no networking.
         process_new_block( block_candidate );

      } else {

         // I'm only a proposer and not the leader; send a new-block-proposal message out to
         //   the network, until it reaches the leader.

#ifdef QC_CHAIN_TRACE_DEBUG
         ilog(" === broadcasting new block = #${block_height} ${proposal_id}", ("proposal_id", block_candidate.block_id)("block_height",compute_block_num(block_candidate.block_id) ));
#endif
         send_hs_new_block_msg( block_candidate );
      }
   }

   void qc_chain::update_high_qc(const eosio::chain::quorum_certificate & high_qc){

#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === check to update high qc ${proposal_id}", ("proposal_id", high_qc.proposal_id));
#endif

      // if new high QC is higher than current, update to new

      if (_high_qc.proposal_id == NULL_PROPOSAL_ID){

         std::unique_lock state_lock( _state_mutex );
         _high_qc = high_qc;
         _b_leaf = _high_qc.proposal_id;
         state_lock.unlock();

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} _b_leaf updated (update_high_qc) : ${proposal_id}", ("proposal_id", _high_qc.proposal_id)("id", _id));
#endif
      } else {
         const hs_proposal_message *old_high_qc_prop = get_proposal( _high_qc.proposal_id );
         const hs_proposal_message *new_high_qc_prop = get_proposal( high_qc.proposal_id );
         if (old_high_qc_prop == nullptr)
            return;
         if (new_high_qc_prop == nullptr)
            return;

         if (new_high_qc_prop->get_height() > old_high_qc_prop->get_height()
             && is_quorum_met(high_qc, _schedule, *new_high_qc_prop))
         {
            // "The caller does not need this updated on their high_qc structure" -- g
            //high_qc.quorum_met = true;

#ifdef QC_CHAIN_TRACE_DEBUG
            ilog(" === updated high qc, now is : #${get_height}  ${proposal_id}", ("get_height", new_high_qc_prop->get_height())("proposal_id", new_high_qc_prop->proposal_id));
#endif
            std::unique_lock state_lock( _state_mutex );
            _high_qc = high_qc;
            _high_qc.quorum_met = true;
            _b_leaf = _high_qc.proposal_id;
            state_lock.unlock();

#ifdef QC_CHAIN_TRACE_DEBUG
            if (_log) ilog(" === ${id} _b_leaf updated (update_high_qc) : ${proposal_id}", ("proposal_id", _high_qc.proposal_id)("id", _id));
#endif
         }
      }
   }

   void qc_chain::leader_rotation_check(){

      //verify if leader changed

      name current_leader = _pacemaker->get_leader();
      name next_leader = _pacemaker->get_next_leader();

      if (current_leader != next_leader){

         if (_log) ilog(" /// ${id} rotating leader : ${old_leader} -> ${new_leader} ",
                        ("id", _id)
                        ("old_leader", current_leader)
                        ("new_leader", next_leader));

         //leader changed, we send our new_view message

         reset_qc(NULL_PROPOSAL_ID);

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} setting _pending_proposal_block to null (leader_rotation_check)", ("id", _id));
#endif

         std::unique_lock state_lock( _state_mutex );
         _pending_proposal_block = NULL_BLOCK_ID;
         state_lock.unlock();

         hs_new_view_message new_view;

         new_view.high_qc = _high_qc;

         send_hs_new_view_msg(new_view);
      }
   }

   //safenode predicate
   bool qc_chain::is_node_safe(const hs_proposal_message & proposal){

      //ilog(" === is_node_safe ===");

      bool monotony_check = false;
      bool safety_check = false;
      bool liveness_check = false;
      bool final_on_qc_check = false;

      fc::sha256 upcoming_commit;

      if (proposal.justify.proposal_id == NULL_PROPOSAL_ID && _b_lock == NULL_PROPOSAL_ID)
         final_on_qc_check = true; //if chain just launched or feature just activated
      else {

         std::vector<hs_proposal_message> current_qc_chain = get_qc_chain(proposal.justify.proposal_id);

         size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

         if (chain_length >= 2) {

            auto itr = current_qc_chain.begin();

            hs_proposal_message b2 = *itr;
            ++itr;
            hs_proposal_message b1 = *itr;

            if (proposal.parent_id == b2.proposal_id && b2.parent_id == b1.proposal_id)
               upcoming_commit = b1.proposal_id;
            else {
               const hs_proposal_message *p = get_proposal( b1.parent_id );
               //EOS_ASSERT( p != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", b1.parent_id) );
               if (p != nullptr) {
                  upcoming_commit = p->final_on_qc;
               } else {
                  if (_errors) ilog(" *** ${id} in is_node_safe did not find expected proposal id: ${proposal_id}", ("id",_id)("proposal_id", b1.parent_id));
               }
            }
         }

         //abstracted [...]
         if (upcoming_commit == proposal.final_on_qc) {
            final_on_qc_check = true;
         }
      }

      if (proposal.get_height() > _v_height) {
         monotony_check = true;
      }

      if (_b_lock != NULL_PROPOSAL_ID){

         //Safety check : check if this proposal extends the chain I'm locked on
         if (extends(proposal.proposal_id, _b_lock)) {
            safety_check = true;
         }

         //Liveness check : check if the height of this proposal's justification is higher than the height of the proposal I'm locked on. This allows restoration of liveness if a replica is locked on a stale block.
         if (proposal.justify.proposal_id == NULL_PROPOSAL_ID && _b_lock == NULL_PROPOSAL_ID) {
            liveness_check = true; //if there is no justification on the proposal and I am not locked on anything, means the chain just launched or feature just activated
         } else {
            const hs_proposal_message *b_lock = get_proposal( _b_lock );
            EOS_ASSERT( b_lock != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", _b_lock) );
            const hs_proposal_message *prop_justification = get_proposal( proposal.justify.proposal_id );
            EOS_ASSERT( prop_justification != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", proposal.justify.proposal_id) );

            if (prop_justification->get_height() > b_lock->get_height()) {
               liveness_check = true;
            }
         }
      } else {
         //if we're not locked on anything, means the protocol just activated or chain just launched
         liveness_check = true;
         safety_check = true;

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} not locked on anything, liveness and safety are true", ("id", _id));
#endif
      }

#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === final_on_qc_check : ${final_on_qc_check}, monotony_check : ${monotony_check}, liveness_check : ${liveness_check}, safety_check : ${safety_check}",
           ("final_on_qc_check", final_on_qc_check)
           ("monotony_check", monotony_check)
           ("liveness_check", liveness_check)
           ("safety_check", safety_check));
#endif

      bool node_is_safe = final_on_qc_check && monotony_check && (liveness_check || safety_check);
      if (!node_is_safe) {

         if (_errors)
            ilog(" *** node is NOT safe. Checks : final_on_qc: ${final_on_qc}, monotony_check: ${monotony_check}, liveness_check: ${liveness_check}, safety_check: ${safety_check})",
                 ("final_on_qc_check",final_on_qc_check)
                 ("monotony_check",monotony_check)
                 ("liveness_check",liveness_check)
                 ("safety_check",safety_check));
      }

      //return true if monotony check and at least one of liveness or safety check evaluated successfully
      return final_on_qc_check && monotony_check && (liveness_check || safety_check);
   }

   //on proposal received, called from network thread
   void qc_chain::on_hs_proposal_msg(const hs_proposal_message & msg){
      process_proposal(msg);
   }

   //on vote received, called from network thread
   void qc_chain::on_hs_vote_msg(const hs_vote_message & msg){
      process_vote(msg);
   }

   //on new view received, called from network thread
   void qc_chain::on_hs_new_view_msg(const hs_new_view_message & msg){
      process_new_view(msg);
   }

   //on new block received, called from network thread
   void qc_chain::on_hs_new_block_msg(const hs_new_block_message & msg){
      process_new_block(msg);
   }

   void qc_chain::update(const hs_proposal_message & proposal){
      //ilog(" === update internal state ===");
      //if proposal has no justification, means we either just activated the feature or launched the chain, or the proposal is invalid
      if (proposal.justify.proposal_id == NULL_PROPOSAL_ID){
         if (_log) ilog(" === ${id} proposal has no justification ${proposal_id}", ("proposal_id", proposal.proposal_id)("id", _id));
         return;
      }

      std::vector<hs_proposal_message> current_qc_chain = get_qc_chain(proposal.justify.proposal_id);

      size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

      const hs_proposal_message *b_lock = get_proposal( _b_lock );
      EOS_ASSERT( b_lock != nullptr || _b_lock == NULL_PROPOSAL_ID , chain_exception, "expected hs_proposal ${id} not found", ("id", _b_lock) );

      //ilog(" === update_high_qc : proposal.justify ===");
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

#ifdef QC_CHAIN_TRACE_DEBUG
      if (_log) ilog(" === ${id} _b_lock ${_b_lock} b_1 height ${b_1_height}",
                     ("id", _id)
                     ("_b_lock", _b_lock)
                     ("b_1_height", b_1.block_num())
                     ("b_1_phase", b_1.phase_counter));

      if ( b_lock != nullptr ) {
         if (_log) ilog(" === b_lock height ${b_lock_height} b_lock phase ${b_lock_phase}",
                        ("b_lock_height", b_lock->block_num())
                        ("b_lock_phase", b_lock->phase_counter));
      }
#endif

      if (_b_lock == NULL_PROPOSAL_ID || b_1.get_height() > b_lock->get_height()){
#ifdef QC_CHAIN_TRACE_DEBUG
         ilog("setting _b_lock to ${proposal_id}", ("proposal_id",b_1.proposal_id ));
#endif
         std::unique_lock state_lock( _state_mutex );
         _b_lock = b_1.proposal_id; //commit phase on b1
         state_lock.unlock();

#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} _b_lock updated : ${proposal_id}", ("proposal_id", b_1.proposal_id)("id", _id));
#endif
      }

      if (chain_length < 3) {
         if (_log) ilog(" === ${id} qc chain length is 2",("id", _id));
         return;
      }

      ++itr;

      hs_proposal_message b = *itr;

#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === direct parent relationship verification : b_2.parent_id ${b_2.parent_id} b_1.proposal_id ${b_1.proposal_id} b_1.parent_id ${b_1.parent_id} b.proposal_id ${b.proposal_id} ",
                ("b_2.parent_id",b_2.parent_id)
                ("b_1.proposal_id", b_1.proposal_id)
                ("b_1.parent_id", b_1.parent_id)
                ("b.proposal_id", b.proposal_id));
#endif

      //direct parent relationship verification
      if (b_2.parent_id == b_1.proposal_id && b_1.parent_id == b.proposal_id){

         if (_b_exec!= NULL_PROPOSAL_ID){

            const hs_proposal_message *b_exec = get_proposal( _b_exec );
            EOS_ASSERT( b_exec != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", _b_exec) );

            if (b_exec->get_height() >= b.get_height() && b_exec->proposal_id != b.proposal_id){

               if (_errors)
                  ilog(" *** ${id} finality violation detected at height ${block_num}, phase : ${phase}. Proposal ${proposal_id_1} conflicts with ${proposal_id_2}",
                       ("id", _id)
                       ("block_num", b.block_num())
                       ("phase", b.phase_counter)
                       ("proposal_id_1", b.proposal_id)
                       ("proposal_id_2", b_exec->proposal_id));

               std::unique_lock state_lock( _state_mutex );
               _b_finality_violation = b.proposal_id;
               state_lock.unlock();

               //protocol failure
               return;
            }
         }

         commit(b);

#ifdef QC_CHAIN_TRACE_DEBUG
         ilog(" === last executed proposal : #${block_num} ${block_id}", ("block_num", b.block_num())("block_id", b.block_id));
#endif

         std::unique_lock state_lock( _state_mutex );
         _b_exec = b.proposal_id; //decide phase on b
         _block_exec = b.block_id;
         state_lock.unlock();

         gc_proposals( b.get_height()-1);
      }
      else {
         if (_errors) ilog(" *** ${id} could not verify direct parent relationship", ("id",_id));
         if (_errors) ilog("   *** b_2 ${b_2}", ("b_2", b_2));
         if (_errors) ilog("   *** b_1 ${b_1}", ("b_1", b_1));
         if (_errors) ilog("   *** b   ${b}", ("b", b));
      }
   }

   void qc_chain::gc_proposals(uint64_t cutoff){
      //ilog(" === garbage collection on old data");
      std::lock_guard g( _state_mutex );
#ifdef QC_CHAIN_SIMPLE_PROPOSAL_STORE
      ps_height_iterator psh_it = _proposal_stores_by_height.begin();
      while (psh_it != _proposal_stores_by_height.end()) {
         uint64_t height = psh_it->first;
         if (height <= cutoff) {
            // remove all entries from _proposal_height for this proposal store
            proposal_store & pstore = psh_it->second;
            ps_iterator ps_it = pstore.begin();
            while (ps_it != pstore.end()) {
               hs_proposal_message & p = ps_it->second;
               ph_iterator ph_it = _proposal_height.find( p.proposal_id );
               EOS_ASSERT( ph_it != _proposal_height.end(), chain_exception, "gc_proposals internal error: no proposal height entry");
               uint64_t proposal_height = ph_it->second;
               EOS_ASSERT(proposal_height == p.get_height(), chain_exception, "gc_proposals internal error: mismatched proposal height record"); // this check is unnecessary
               _proposal_height.erase( ph_it );
               ++ps_it;
            }
            // then remove the entire proposal store
            psh_it = _proposal_stores_by_height.erase( psh_it );
         } else {
            ++psh_it;
         }
      }
#else
      auto end_itr = _proposal_store.get<by_proposal_height>().upper_bound(cutoff);
      while (_proposal_store.get<by_proposal_height>().begin() != end_itr){
         auto itr = _proposal_store.get<by_proposal_height>().begin();
#ifdef QC_CHAIN_TRACE_DEBUG
         if (_log) ilog(" === ${id} erasing ${block_num} ${phase_counter} ${block_id} proposal_id ${proposal_id}",
                        ("id", _id)
                        ("block_num", itr->block_num())
                        ("phase_counter", itr->phase_counter)
                        ("block_id", itr->block_id)
                        ("proposal_id", itr->proposal_id));
#endif
         _proposal_store.get<by_proposal_height>().erase(itr);
      }
#endif
   }

   void qc_chain::commit(const hs_proposal_message & proposal){

#ifdef QC_CHAIN_TRACE_DEBUG
      ilog(" === attempting to commit proposal #${block_num} ${proposal_id} block_id : ${block_id} phase : ${phase_counter} parent_id : ${parent_id}",
                ("block_num", proposal.block_num())
                ("proposal_id", proposal.proposal_id)
                ("block_id", proposal.block_id)
                ("phase_counter", proposal.phase_counter)
                ("parent_id", proposal.parent_id));
#endif

      bool exec_height_check = false;

      const hs_proposal_message *last_exec_prop = get_proposal( _b_exec );
      EOS_ASSERT( last_exec_prop != nullptr || _b_exec == NULL_PROPOSAL_ID, chain_exception, "expected hs_proposal ${id} not found", ("id", _b_exec) );

#ifdef QC_CHAIN_TRACE_DEBUG
      if (last_exec_prop != nullptr) {
         ilog(" === _b_exec proposal #${block_num} ${proposal_id} block_id : ${block_id} phase : ${phase_counter} parent_id : ${parent_id}",
              ("block_num", last_exec_prop->block_num())
              ("proposal_id", last_exec_prop->proposal_id)
              ("block_id", last_exec_prop->block_id)
              ("phase_counter", last_exec_prop->phase_counter)
              ("parent_id", last_exec_prop->parent_id));

         ilog(" *** last_exec_prop ${proposal_id_1} ${phase_counter_1} vs proposal ${proposal_id_2} ${phase_counter_2} ",
              ("proposal_id_1", last_exec_prop->block_num())
              ("phase_counter_1", last_exec_prop->phase_counter)
              ("proposal_id_2", proposal.block_num())
              ("phase_counter_2", proposal.phase_counter));
      } else {
         ilog(" === _b_exec proposal is null vs proposal ${proposal_id_2} ${phase_counter_2} ",
              ("proposal_id_2", proposal.block_num())
              ("phase_counter_2", proposal.phase_counter));
      }
#endif

      if (_b_exec == NULL_PROPOSAL_ID)
         exec_height_check = true;
      else
         exec_height_check = last_exec_prop->get_height() < proposal.get_height();

      if (exec_height_check){

         const hs_proposal_message *p = get_proposal( proposal.parent_id );
         if (p != nullptr) {
            //ilog(" === recursively committing" );
            commit(*p); //recursively commit all non-committed ancestor blocks sequentially first
         }

         //Execute commands [...]

         if (_log) ilog(" === ${id} committed proposal #${block_num} phase ${phase_counter} block_id : ${block_id} proposal_id : ${proposal_id}",
                        ("id", _id)
                        ("block_num", proposal.block_num())
                        ("phase_counter", proposal.phase_counter)
                        ("block_id", proposal.block_id)
                        ("proposal_id", proposal.proposal_id));
      }
      else {
#ifdef QC_CHAIN_TRACE_DEBUG
         if (_errors) ilog(" *** ${id} sequence not respected on #${block_num} phase ${phase_counter} proposal_id : ${proposal_id}",
                ("id", _id)
                ("block_num", proposal.block_num())
                ("phase_counter", proposal.phase_counter)
                ("proposal_id", proposal.proposal_id));
#endif
      }
   }

}}
