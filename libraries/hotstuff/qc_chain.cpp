#include <eosio/hotstuff/qc_chain.hpp>
#include <fc/scoped_exit.hpp>
#include <boost/range/adaptor/reversed.hpp>

namespace eosio::hotstuff {

   const hs_proposal_message* qc_chain::get_proposal(const fc::sha256& proposal_id) {
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

   bool qc_chain::insert_proposal(const hs_proposal_message& proposal) {
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

   void qc_chain::get_state(finalizer_state& fs) const {
      fs.chained_mode           = _chained_mode;
      fs.b_leaf                 = _b_leaf;
      fs.b_lock                 = _b_lock;
      fs.b_exec                 = _b_exec;
      fs.b_finality_violation   = _b_finality_violation;
      fs.block_exec             = _block_exec;
      fs.pending_proposal_block = _pending_proposal_block;
      fs.v_height               = _v_height;
      fs.high_qc                = _high_qc.to_msg();
      fs.current_qc             = _current_qc.to_msg();
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

   uint32_t qc_chain::positive_bits_count(const hs_bitset& finalizers) {
      return finalizers.count(); // the number of bits in this bitset that are set.
   }

      hs_bitset qc_chain::update_bitset(const hs_bitset& finalizer_set, name finalizer ) {

      hs_bitset b(finalizer_set );
      vector<name> finalizers = _pacemaker->get_finalizers();
      for (size_t i = 0; i < finalizers.size();i++) {
         if (finalizers[i] == finalizer) {
            b.set(i);

            fc_tlog(_logger, " === finalizer found ${finalizer} new value : ${value}",
                    ("finalizer", finalizer)("value", [&](){ std::string r; boost::to_string(b, r); return r; }()));

            return b;
         }
      }
      fc_tlog(_logger, " *** finalizer not found ${finalizer}",
           ("finalizer", finalizer));
      throw std::runtime_error("qc_chain internal error: finalizer not found");
   }

   digest_type qc_chain::get_digest_to_sign(const block_id_type& block_id, uint8_t phase_counter, const fc::sha256& final_on_qc){
      digest_type h1 = digest_type::hash( std::make_pair( block_id, phase_counter ) );
      digest_type h2 = digest_type::hash( std::make_pair( h1, final_on_qc ) );
      return h2;
   }

   std::vector<hs_proposal_message> qc_chain::get_qc_chain(const fc::sha256& proposal_id) {
      std::vector<hs_proposal_message> ret_arr;
      ret_arr.reserve(3);
      if ( const hs_proposal_message* b2 = get_proposal( proposal_id ) ) {
         ret_arr.push_back( *b2 );
         if (const hs_proposal_message* b1 = get_proposal( b2->justify.proposal_id ) ) {
            ret_arr.push_back( *b1 );
            if (const hs_proposal_message* b = get_proposal( b1->justify.proposal_id ) ) {
               ret_arr.push_back( *b );
            }
         }
      }
      return ret_arr;
   }

   hs_proposal_message qc_chain::new_proposal_candidate(const block_id_type& block_id, uint8_t phase_counter) {
      hs_proposal_message b_new;
      b_new.block_id = block_id;
      b_new.parent_id =  _b_leaf;
      b_new.phase_counter = phase_counter;
      b_new.justify = _high_qc.to_msg(); //or null if no _high_qc upon activation or chain launch
      if (!b_new.justify.proposal_id.empty()) {
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
                  fc_elog(_logger, " *** ${id} expected to find proposal in new_proposal_candidate() but not found : ${proposal_id}", ("id",_id)("proposal_id", b1.parent_id));
               }
            }
         }
      }

      b_new.proposal_id = get_digest_to_sign(b_new.block_id, b_new.phase_counter, b_new.final_on_qc);

      fc_dlog(_logger, " === ${id} creating new proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id} : parent_id ${parent_id} : justify ${justify}",
              ("id", _id)
              ("block_num", b_new.block_num())
              ("phase_counter", b_new.phase_counter)
              ("proposal_id", b_new.proposal_id)
              ("parent_id", b_new.parent_id)
              ("justify", b_new.justify.proposal_id));

      return b_new;
   }

   void qc_chain::reset_qc(const fc::sha256& proposal_id) {
      fc_tlog(_logger, " === ${id} resetting qc : ${proposal_id}", ("proposal_id" , proposal_id)("id", _id));
      _current_qc.reset(proposal_id, 21); // TODO: use active schedule size
   }

   hs_new_block_message qc_chain::new_block_candidate(const block_id_type& block_id) {
      hs_new_block_message b;
      b.block_id = block_id;
      b.justify = _high_qc.to_msg(); //or null if no _high_qc upon activation or chain launch
      return b;
   }

   bool qc_chain::evaluate_quorum(const extended_schedule& es, const hs_bitset& finalizers, const fc::crypto::blslib::bls_signature& agg_sig, const hs_proposal_message& proposal) {


      if (positive_bits_count(finalizers) < _pacemaker->get_quorum_threshold()){
         return false;
      }

      fc::crypto::blslib::bls_public_key agg_key;

      bool first = true;
      for (hs_bitset::size_type i = 0; i < finalizers.size(); ++i) {
         if (finalizers[i]){
            //adding finalizer's key to the aggregate pub key
            if (first) {
               first = false;
               agg_key = _private_key.get_public_key();
            } else {
               agg_key = fc::crypto::blslib::aggregate({agg_key, _private_key.get_public_key()});
            }
         }
      }
#warning fix todo
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

   bool qc_chain::is_quorum_met(const quorum_certificate& qc, const extended_schedule& schedule, const hs_proposal_message& proposal) {

      if (qc.is_quorum_met()) {
         return true; //skip evaluation if we've already verified quorum was met
      }
      else {
         fc_tlog(_logger, " === qc : ${qc}", ("qc", qc.to_msg()));
         // If the caller wants to update the quorum_met flag on its "qc" object, it will have to do so
         //   based on the return value of this method, since "qc" here is const.
         return evaluate_quorum(schedule, qc.get_active_finalizers(), qc.get_active_agg_sig(), proposal);
      }
   }


   qc_chain::qc_chain(name id, base_pacemaker* pacemaker,
                      std::set<name> my_producers,
                      bls_key_map_t finalizer_keys,
                      fc::logger& logger)
      : _pacemaker(pacemaker),
        _my_producers(std::move(my_producers)),
        _my_finalizer_keys(std::move(finalizer_keys)),
        _id(id),
        _logger(logger)
   {
      _high_qc.reset({}, 21); // TODO: use active schedule size
      _current_qc.reset({}, 21); // TODO: use active schedule size

      fc_dlog(_logger, " === ${id} qc chain initialized ${my_producers}", ("my_producers", my_producers)("id", _id));
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
      _v_height = proposal.get_height();

      digest_type digest = get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

      std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);
#warning use appropriate private key for each producer
      fc::crypto::blslib::bls_signature sig = _private_key.sign(h); //FIXME/TODO: use appropriate private key for each producer

      hs_vote_message v_msg = {proposal.proposal_id, finalizer, sig};
      return v_msg;
   }

   std::optional<hs_commitment> qc_chain::process_proposal(const hs_proposal_message & proposal){

      //auto start = fc::time_point::now();

      if (!proposal.justify.proposal_id.empty()) {

         const hs_proposal_message *jp = get_proposal( proposal.justify.proposal_id );
         if (jp == nullptr) {
            fc_elog(_logger, " *** ${id} proposal justification unknown : ${proposal_id}", ("id",_id)("proposal_id", proposal.justify.proposal_id));
            return {}; //can't recognize a proposal with an unknown justification
         }
      }

      const hs_proposal_message *p = get_proposal( proposal.proposal_id );
      if (p != nullptr) {

         fc_elog(_logger, " *** ${id} proposal received twice : ${proposal_id}", ("id",_id)("proposal_id", proposal.proposal_id));

         if (p->justify.proposal_id != proposal.justify.proposal_id) {

            fc_elog(_logger, " *** ${id} two identical proposals (${proposal_id}) have different justifications :  ${justify_1} vs  ${justify_2}",
                              ("id",_id)
                              ("proposal_id", proposal.proposal_id)
                              ("justify_1", p->justify.proposal_id)
                              ("justify_2", proposal.justify.proposal_id));

         }

         return {}; //already aware of proposal, nothing to do
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

         fc_elog(_logger, " *** ${id} received a different proposal at the same height (${block_num}, ${phase_counter})",
                           ("id",_id)
                           ("block_num", existing_proposal.block_num())
                           ("phase_counter", existing_proposal.phase_counter));

         fc_elog(_logger, " *** Proposal #1 : ${proposal_id_1} Proposal #2 : ${proposal_id_2}",
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

      fc_dlog(_logger, " === ${id} received new proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id} : parent_id ${parent_id} justify ${justify}",
                     ("id", _id)
                     ("block_num", proposal.block_num())
                     ("phase_counter", proposal.phase_counter)
                     ("proposal_id", proposal.proposal_id)
                     ("parent_id", proposal.parent_id)
                     ("justify", proposal.justify.proposal_id));

      bool success = insert_proposal( proposal );
      EOS_ASSERT( success , chain_exception, "internal error: duplicate proposal insert attempt" ); // can't happen unless bad mutex somewhere; already checked for this

      auto increment_version = fc::make_scoped_exit([this]() { ++_state_version; }); // assert failing above would mean no change

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

               fc_tlog(_logger, " === ${id} signed proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id}",
                              ("id", _id)
                              ("block_num", proposal.block_num())
                              ("phase_counter", proposal.phase_counter)
                              ("proposal_id", proposal.proposal_id));

               //send_hs_vote_msg(v_msg);
               msgs.push_back(v_msg);

            };

            mf_itr++;
         }
      }

      else fc_tlog(_logger, " === ${id} skipping signature on proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id}",
                          ("id", _id)
                          ("block_num", proposal.block_num())
                          ("phase_counter", proposal.phase_counter)
                          ("proposal_id", proposal.proposal_id));

      //update internal state
      auto optional_commitment = update(proposal);

      for (auto &msg : msgs) {
         send_hs_msg(msg);
      }

      //check for leader change
      leader_rotation_check();

      //auto total_time = fc::time_point::now() - start;
      //fc_dlog(_logger, " ... process_proposal() total time : ${total_time}", ("total_time", total_time));

      return optional_commitment;
   }

   void qc_chain::process_vote(const hs_vote_message & vote){

      //auto start = fc::time_point::now();
#warning check for duplicate or invalid vote. We will return in either case, but keep proposals for evidence of double signing
      //TODO: check for duplicate or invalid vote. We will return in either case, but keep proposals for evidence of double signing

      bool am_leader = am_i_leader();

      if (!am_leader)
         return;
      fc_tlog(_logger, " === Process vote from ${finalizer} : current bitset ${value}" ,
              ("finalizer", vote.finalizer)("value", _current_qc.get_active_finalizers_string()));
      // only leader need to take action on votes
      if (vote.proposal_id != _current_qc.get_proposal_id())
         return;

      const hs_proposal_message *p = get_proposal( vote.proposal_id );
      if (p == nullptr) {
         fc_elog(_logger, " *** ${id} couldn't find proposal, vote : ${vote}", ("id",_id)("vote", vote));
         return;
      }

      bool quorum_met = _current_qc.is_quorum_met(); //check if quorum already met

      // If quorum is already met, we don't need to do anything else. Otherwise, we aggregate the signature.
      if (!quorum_met){

         auto increment_version = fc::make_scoped_exit([this]() { ++_state_version; });

         const hs_bitset& finalizer_set = _current_qc.get_active_finalizers();
         if (finalizer_set.any())
            _current_qc.set_active_agg_sig(fc::crypto::blslib::aggregate({_current_qc.get_active_agg_sig(), vote.sig }));
         else
            _current_qc.set_active_agg_sig(vote.sig);

         fc_tlog(_logger, " === update bitset ${value} ${finalizer}", ("value", _current_qc.get_active_finalizers_string())("finalizer", vote.finalizer));
         _current_qc.set_active_finalizers(update_bitset(finalizer_set, vote.finalizer));

         quorum_met = is_quorum_met(_current_qc, _schedule, *p);

         if (quorum_met){

            fc_dlog(_logger, " === ${id} quorum met on #${block_num} ${phase_counter} ${proposal_id} ",
                           ("block_num", p->block_num())
                           ("phase_counter", p->phase_counter)
                           ("proposal_id", vote.proposal_id)
                           ("id", _id));

            _current_qc.set_quorum_met();

            //fc_tlog(_logger, " === update_high_qc : _current_qc ===");
            update_high_qc(_current_qc);

            //check for leader change
            leader_rotation_check();

            //if we're operating in event-driven mode and the proposal hasn't reached the decide phase yet
            if (_chained_mode == false && p->phase_counter < 3) {
               fc_tlog(_logger, " === ${id} phase increment on proposal ${proposal_id}", ("proposal_id", vote.proposal_id)("id", _id));
               hs_proposal_message proposal_candidate;

               if (_pending_proposal_block.empty())
                  proposal_candidate = new_proposal_candidate( p->block_id, p->phase_counter + 1 );
               else
                  proposal_candidate = new_proposal_candidate( _pending_proposal_block, 0 );

               reset_qc(proposal_candidate.proposal_id);
               fc_tlog(_logger, " === ${id} setting _pending_proposal_block to null (process_vote)", ("id", _id));
               _pending_proposal_block = {};
               _b_leaf = proposal_candidate.proposal_id;

               send_hs_msg(proposal_candidate);
               fc_tlog(_logger, " === ${id} _b_leaf updated (process_vote): ${proposal_id}", ("proposal_id", proposal_candidate.proposal_id)("id", _id));
            }
         }
      }

      //auto total_time = fc::time_point::now() - start;
      //fc_tlog(_logger, " ... process_vote() total time : ${total_time}", ("total_time", total_time));
   }

   void qc_chain::process_new_view(const hs_new_view_message & msg){
      fc_tlog(_logger, " === ${id} process_new_view === ${qc}", ("qc", msg.high_qc)("id", _id));
      auto increment_version = fc::make_scoped_exit([this]() { ++_state_version; });
      if (!update_high_qc(quorum_certificate{msg.high_qc})) {
         increment_version.cancel();
      }
   }

   void qc_chain::process_new_block(const hs_new_block_message & msg){

      // If I'm not a leader, I probably don't care about hs-new-block messages.
#warning check for a need to gossip/rebroadcast even if it's not for us (maybe here, maybe somewhere else).
      // TODO: check for a need to gossip/rebroadcast even if it's not for us (maybe here, maybe somewhere else).
      if (! am_i_leader()) {
         fc_tlog(_logger, " === ${id} process_new_block === discarding because I'm not the leader; block_id : ${bid}, justify : ${just}", ("bid", msg.block_id)("just", msg.justify)("id", _id));
         return;
      }

      fc_tlog(_logger, " === ${id} process_new_block === am leader; block_id : ${bid}, justify : ${just}", ("bid", msg.block_id)("just", msg.justify)("id", _id));

#warning What to do with the received msg.justify?
      // ------------------------------------------------------------------
      //
      //   FIXME/REVIEW/TODO: What to do with the received msg.justify?
      //
      //   We are the leader, and we got a block_id from a proposer, but
      //     we should probably do something with the justify QC that
      //     comes with it (which is the _high_qc of the proposer (?))
      //
      // ------------------------------------------------------------------

      auto increment_version = fc::make_scoped_exit([this]() { ++_state_version; });

      if (!_current_qc.get_proposal_id().empty() && !_current_qc.is_quorum_met()) {

         fc_tlog(_logger, " === ${id} pending proposal found ${proposal_id} : quorum met ${quorum_met}",
                        ("id", _id)
                        ("proposal_id", _current_qc.get_proposal_id())
                        ("quorum_met", _current_qc.is_quorum_met()));

         fc_tlog(_logger, " === ${id} setting _pending_proposal_block to ${block_id} (on_beat)", ("id", _id)("block_id", msg.block_id));
         _pending_proposal_block = msg.block_id;

      } else {

         fc_tlog(_logger, " === ${id} preparing new proposal ${proposal_id} : quorum met ${quorum_met}",
                        ("id", _id)
                        ("proposal_id", _current_qc.get_proposal_id())
                        ("quorum_met", _current_qc.is_quorum_met()));
         hs_proposal_message proposal_candidate = new_proposal_candidate( msg.block_id, 0 );

         reset_qc(proposal_candidate.proposal_id);

         fc_tlog(_logger, " === ${id} setting _pending_proposal_block to null (process_new_block)", ("id", _id));

         _pending_proposal_block = {};
         _b_leaf = proposal_candidate.proposal_id;

         send_hs_msg(proposal_candidate);

         fc_tlog(_logger, " === ${id} _b_leaf updated (on_beat): ${proposal_id}", ("proposal_id", proposal_candidate.proposal_id)("id", _id));
      }
   }

   std::optional<hs_commitment> qc_chain::send_hs_msg(const hs_message& msg) {
      std::visit(overloaded{
              [this](const hs_vote_message& m) { fc_tlog(_logger, " === broadcast_hs_vote ==="); },
              [this](const hs_proposal_message& m) { fc_tlog(_logger, " === broadcast_hs_proposal ==="); },
              [this](const hs_new_block_message& m) { fc_tlog(_logger, " === broadcast_hs_new_block ==="); },
              [this](const hs_new_view_message& m) { fc_tlog(_logger, " === broadcast_hs_new_view ==="); },
      }, msg);
      _pacemaker->send_hs_msg(msg, _id);

      std::optional<hs_commitment> res;
      std::visit(overloaded{
                        [this](const hs_vote_message& m) { process_vote(m); },
                        [this, &res](const hs_proposal_message& m) { res = process_proposal(m); },
                        [](const hs_new_block_message& m) {},
                        [](const hs_new_view_message& m) {},
                    },
                    msg);
      return res;
   }

   //extends predicate
   bool qc_chain::extends(const fc::sha256& descendant, const fc::sha256& ancestor){

#warning confirm the extends predicate never has to verify extension of irreversible blocks, otherwise this function needs to be modified
      //TODO: confirm the extends predicate never has to verify extension of irreversible blocks, otherwise this function needs to be modified

      uint32_t counter = 0;
      const hs_proposal_message *p = get_proposal( descendant );
      while (p != nullptr) {
         fc::sha256 parent_id = p->parent_id;
         p = get_proposal( parent_id );
         if (p == nullptr) {
            fc_elog(_logger, " *** ${id} cannot find proposal id while looking for ancestor : ${proposal_id}", ("id",_id)("proposal_id", parent_id));
            return false;
         }
         if (p->proposal_id == ancestor) {
            if (counter > 25) {
               fc_elog(_logger, " *** ${id} took ${counter} iterations to find ancestor ", ("id",_id)("counter", counter));
            }
            return true;
         }
         ++counter;
      }

      fc_elog(_logger, " *** ${id} extends returned false : could not find ${d_proposal_id} descending from ${a_proposal_id} ",
                        ("id",_id)
                        ("d_proposal_id", descendant)
                        ("a_proposal_id", ancestor));

      return false;
   }

   // Invoked when we could perhaps make a proposal to the network (or to ourselves, if we are the leader).
   // Called from the main application thread
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

         fc_tlog(_logger, " === I am a leader-proposer that is proposing a block for itself to lead");
         // Hardwired consumption by self; no networking.
         process_new_block( block_candidate );

      } else {

         // I'm only a proposer and not the leader; send a new-block-proposal message out to
         //   the network, until it reaches the leader.

         fc_tlog(_logger, " === broadcasting new block = #${block_num} ${proposal_id}", ("proposal_id", block_candidate.block_id)("block_num",(block_header::num_from_id(block_candidate.block_id))));
         send_hs_msg( block_candidate );
      }
   }

   // returns true on state change (caller decides update on state version
   bool qc_chain::update_high_qc(const quorum_certificate& high_qc) {

      fc_tlog(_logger, " === check to update high qc ${proposal_id}", ("proposal_id", high_qc.get_proposal_id()));

      // if new high QC is higher than current, update to new

      if (_high_qc.get_proposal_id().empty()) {
         _high_qc = high_qc;
         _b_leaf = _high_qc.get_proposal_id();

         fc_tlog(_logger, " === ${id} _b_leaf updated (update_high_qc) : ${proposal_id}", ("proposal_id", _high_qc.get_proposal_id())("id", _id));
         return true;
      } else {
         const hs_proposal_message *old_high_qc_prop = get_proposal( _high_qc.get_proposal_id() );
         const hs_proposal_message *new_high_qc_prop = get_proposal( high_qc.get_proposal_id() );
         if (old_high_qc_prop == nullptr)
            return false;
         if (new_high_qc_prop == nullptr)
            return false;

         if (new_high_qc_prop->get_height() > old_high_qc_prop->get_height()
             && is_quorum_met(high_qc, _schedule, *new_high_qc_prop))
         {
            // "The caller does not need this updated on their high_qc structure" -- g
            //high_qc.quorum_met = true;

            fc_tlog(_logger, " === updated high qc, now is : #${get_height}  ${proposal_id}", ("get_height", new_high_qc_prop->get_height())("proposal_id", new_high_qc_prop->proposal_id));
            _high_qc = high_qc;
            _high_qc.set_quorum_met();
            _b_leaf = _high_qc.get_proposal_id();

            fc_tlog(_logger, " === ${id} _b_leaf updated (update_high_qc) : ${proposal_id}", ("proposal_id", _high_qc.get_proposal_id())("id", _id));
            return true;
         }
      }
      return false;
   }

   void qc_chain::leader_rotation_check(){
      //verify if leader changed

      name current_leader = _pacemaker->get_leader();
      name next_leader = _pacemaker->get_next_leader();

      if (current_leader != next_leader){

         fc_dlog(_logger, " /// ${id} rotating leader : ${old_leader} -> ${new_leader} ",
                        ("id", _id)
                        ("old_leader", current_leader)
                        ("new_leader", next_leader));

         //leader changed, we send our new_view message

         reset_qc({});

         fc_tlog(_logger, " === ${id} setting _pending_proposal_block to null (leader_rotation_check)", ("id", _id));

         _pending_proposal_block = {};

         hs_new_view_message new_view;

         new_view.high_qc = _high_qc.to_msg();

         send_hs_msg(new_view);
      }
   }

   //safenode predicate
   bool qc_chain::is_node_safe(const hs_proposal_message& proposal) {

      //fc_tlog(_logger, " === is_node_safe ===");

      bool monotony_check = false;
      bool safety_check = false;
      bool liveness_check = false;
      bool final_on_qc_check = false;

      fc::sha256 upcoming_commit;

      if (proposal.justify.proposal_id.empty() && _b_lock.empty()) {
         final_on_qc_check = true; //if chain just launched or feature just activated
      } else {

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
                  fc_elog(_logger, " *** ${id} in is_node_safe did not find expected proposal id: ${proposal_id}", ("id",_id)("proposal_id", b1.parent_id));
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

      if (!_b_lock.empty()) {

         //Safety check : check if this proposal extends the chain I'm locked on
         if (extends(proposal.proposal_id, _b_lock)) {
            safety_check = true;
         }

         //Liveness check : check if the height of this proposal's justification is higher than the height of the proposal I'm locked on. This allows restoration of liveness if a replica is locked on a stale block.
         if (proposal.justify.proposal_id.empty() && _b_lock.empty()) {
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

         fc_tlog(_logger, " === ${id} not locked on anything, liveness and safety are true", ("id", _id));
      }

      fc_tlog(_logger, " === final_on_qc_check : ${final_on_qc_check}, monotony_check : ${monotony_check}, liveness_check : ${liveness_check}, safety_check : ${safety_check}",
           ("final_on_qc_check", final_on_qc_check)
           ("monotony_check", monotony_check)
           ("liveness_check", liveness_check)
           ("safety_check", safety_check));

      bool node_is_safe = final_on_qc_check && monotony_check && (liveness_check || safety_check);
      if (!node_is_safe) {

         fc_elog(_logger, " *** node is NOT safe. Checks : final_on_qc: ${final_on_qc}, monotony_check: ${monotony_check}, liveness_check: ${liveness_check}, safety_check: ${safety_check})",
                 ("final_on_qc_check",final_on_qc_check)
                 ("monotony_check",monotony_check)
                 ("liveness_check",liveness_check)
                 ("safety_check",safety_check));
      }

      //return true if monotony check and at least one of liveness or safety check evaluated successfully
      return final_on_qc_check && monotony_check && (liveness_check || safety_check);
   }

   //hs_message received, called from network thread
   //returns highest block_id that was made irreversible (if any)
   std::optional<hs_commitment> qc_chain::on_hs_msg(const hs_message& msg) {
      std::optional<hs_commitment> res;
      std::visit(overloaded{
                     [this](const hs_vote_message& m) { process_vote(m); },
                     [this, &res](const hs_proposal_message& m) { res = process_proposal(m); },
                     [this](const hs_new_block_message& m) { process_new_block(m); },
                     [this](const hs_new_view_message& m) { process_new_view(m); },
                 },
                 msg);
      return res;
   }

   std::optional<hs_commitment> qc_chain::update(const hs_proposal_message& proposal) {
      //fc_tlog(_logger, " === update internal state ===");
      //if proposal has no justification, means we either just activated the feature or launched the chain, or the proposal is invalid
      if (proposal.justify.proposal_id.empty()) {
         fc_dlog(_logger, " === ${id} proposal has no justification ${proposal_id}", ("proposal_id", proposal.proposal_id)("id", _id));
         return {};
      }

      std::vector<hs_proposal_message> current_qc_chain = get_qc_chain(proposal.justify.proposal_id);

      size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

      const hs_proposal_message *b_lock = get_proposal( _b_lock );
      EOS_ASSERT( b_lock != nullptr || _b_lock.empty(), chain_exception, "expected hs_proposal ${id} not found", ("id", _b_lock) );

      //fc_tlog(_logger, " === update_high_qc : proposal.justify ===");
      update_high_qc(quorum_certificate{proposal.justify});

      if (chain_length<1){
         fc_dlog(_logger, " === ${id} qc chain length is 0", ("id", _id));
         return {};
      }

      auto itr = current_qc_chain.begin();
      hs_proposal_message b_2 = *itr;

      if (chain_length<2){
         fc_dlog(_logger, " === ${id} qc chain length is 1", ("id", _id));
         return {};
      }

      itr++;

      hs_proposal_message b_1 = *itr;

      //if we're not locked on anything, means we just activated or chain just launched, else we verify if we've progressed enough to establish a new lock

      fc_tlog(_logger, " === ${id} _b_lock ${_b_lock} b_1 height ${b_1_height}",
                     ("id", _id)
                     ("_b_lock", _b_lock)
                     ("b_1_height", b_1.block_num())
                     ("b_1_phase", b_1.phase_counter));

      if ( b_lock != nullptr ) {
         fc_tlog(_logger, " === b_lock height ${b_lock_height} b_lock phase ${b_lock_phase}",
                        ("b_lock_height", b_lock->block_num())
                        ("b_lock_phase", b_lock->phase_counter));
      }

      if (_b_lock.empty() || b_1.get_height() > b_lock->get_height()) {
         fc_tlog(_logger, "setting _b_lock to ${proposal_id}", ("proposal_id",b_1.proposal_id ));
         _b_lock = b_1.proposal_id; //commit phase on b1

         fc_tlog(_logger, " === ${id} _b_lock updated : ${proposal_id}", ("proposal_id", b_1.proposal_id)("id", _id));
      }

      if (chain_length < 3) {
         fc_dlog(_logger, " === ${id} qc chain length is 2",("id", _id));
         return {};
      }

      ++itr;

      hs_proposal_message b = *itr;

      fc_tlog(_logger, " === direct parent relationship verification : b_2.parent_id ${b_2.parent_id} b_1.proposal_id ${b_1.proposal_id} b_1.parent_id ${b_1.parent_id} b.proposal_id ${b.proposal_id} ",
                ("b_2.parent_id",b_2.parent_id)
                ("b_1.proposal_id", b_1.proposal_id)
                ("b_1.parent_id", b_1.parent_id)
                ("b.proposal_id", b.proposal_id));

      //direct parent relationship verification
      if (b_2.parent_id == b_1.proposal_id && b_1.parent_id == b.proposal_id){

         if (!_b_exec.empty()) {

            const hs_proposal_message *b_exec = get_proposal( _b_exec );
            EOS_ASSERT( b_exec != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", _b_exec) );

            if (b_exec->get_height() >= b.get_height() && b_exec->proposal_id != b.proposal_id){

               fc_elog(_logger, " *** ${id} finality violation detected at height ${block_num}, phase : ${phase}. Proposal ${proposal_id_1} conflicts with ${proposal_id_2}",
                       ("id", _id)
                       ("block_num", b.block_num())
                       ("phase", b.phase_counter)
                       ("proposal_id_1", b.proposal_id)
                       ("proposal_id_2", b_exec->proposal_id));

               _b_finality_violation = b.proposal_id;

               //protocol failure
               return {};
            }
         }

         commit(b);

         fc_tlog(_logger, " === last executed proposal : #${block_num} ${block_id}", ("block_num", b.block_num())("block_id", b.block_id));

         _b_exec = b.proposal_id; //decide phase on b
         _block_exec = b.block_id;

         gc_proposals( b.get_height()-1);
         return hs_commitment{ .b = b, .b1 = b_1, .b2 = b_2, .bstar = proposal };
      } else {
         fc_elog(_logger, " *** ${id} could not verify direct parent relationship", ("id",_id));
         fc_elog(_logger, "   *** b_2 ${b_2}", ("b_2", b_2));
         fc_elog(_logger, "   *** b_1 ${b_1}", ("b_1", b_1));
         fc_elog(_logger, "   *** b   ${b}", ("b", b));
      }
      return {};
   }

   void qc_chain::gc_proposals(uint64_t cutoff){
      //fc_tlog(_logger, " === garbage collection on old data");

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
         fc_tlog(_logger, " === ${id} erasing ${block_num} ${phase_counter} ${block_id} proposal_id ${proposal_id}",
                        ("id", _id)
                        ("block_num", itr->block_num())
                        ("phase_counter", itr->phase_counter)
                        ("block_id", itr->block_id)
                        ("proposal_id", itr->proposal_id));
         _proposal_store.get<by_proposal_height>().erase(itr);
      }
#endif
   }

void qc_chain::commit(const hs_proposal_message& initial_proposal) {
   std::vector<const hs_proposal_message*> proposal_chain;
   proposal_chain.reserve(10);
   
   const hs_proposal_message* p = &initial_proposal;
   while (p) {
      fc_tlog(_logger, " === attempting to commit proposal #${block_num}:${phase} ${prop_id} block_id: ${block_id} parent_id: ${parent_id}",
              ("block_num", p->block_num())("prop_id", p->proposal_id)("block_id", p->block_id)
              ("phase", p->phase_counter)("parent_id", p->parent_id));

      const hs_proposal_message* last_exec_prop = get_proposal(_b_exec);
      EOS_ASSERT(last_exec_prop != nullptr || _b_exec.empty(), chain_exception,
                 "expected hs_proposal ${id} not found", ("id", _b_exec));

      if (last_exec_prop != nullptr) {
         fc_tlog(_logger, " === _b_exec proposal #${block_num}:${phase} ${prop_id} block_id: ${block_id} parent_id: ${parent_id}",
                 ("block_num", last_exec_prop->block_num())("prop_id", last_exec_prop->proposal_id)
                 ("block_id", last_exec_prop->block_id)("phase", last_exec_prop->phase_counter)
                 ("parent_id", last_exec_prop->parent_id));

         fc_tlog(_logger, " *** last_exec_prop ${prop_id_1} ${phase_1} vs proposal ${prop_id_2} ${phase_2} ",
                 ("prop_id_1", last_exec_prop->block_num())("phase_1", last_exec_prop->phase_counter)
                 ("prop_id_2", p->block_num())("phase_2", p->phase_counter));
      } else {
         fc_tlog(_logger, " === _b_exec proposal is null vs proposal ${prop_id_2} ${phase_2} ",
                 ("prop_id_2", p->block_num())("phase_2", p->phase_counter));
      }

      bool exec_height_check = _b_exec.empty() || last_exec_prop->get_height() < p->get_height();
      if (exec_height_check) {
         proposal_chain.push_back(p);         // add proposal to vector for further processing
         p = get_proposal(p->parent_id);      // process parent if non-null
      } else {
         fc_elog(_logger, " *** ${id} sequence not respected on #${block_num}:${phase} proposal_id: ${prop_id}",
                 ("id", _id)("block_num", p->block_num())("phase", p->phase_counter)("prop_id", p->proposal_id));
         break;
      }
   }

   if (!proposal_chain.empty()) {
      // commit all ancestor blocks sequentially first (hence the reverse)
      for (auto p : boost::adaptors::reverse(proposal_chain)) {
         // issue #1522:  HotStuff finality should drive LIB in controller
         // no need to do anything here. `qc_chain::update()` will return the `block_id_type` made final
         // (which is b.block_id) to `chain_pacemaker`, which will notify the controller. The controller
         // will notify `fork_database` so that this block and its ancestors are marked irreversible and
         // moved out of `fork_database`.
         ;
      }

      auto p = proposal_chain.back();
      if (proposal_chain.size() > 1) {
         auto last = proposal_chain.front();
         fc_dlog(_logger, " === ${id} committed {num} proposals from  #${block_num}:${phase} block_id: ${block_id} "
                 "proposal_id: ${prop_id} to #${block_num_2}:${phase_2} block_id: ${block_id_2} proposal_id: ${prop_id_2}",
                 ("id", _id)("block_num", p->block_num())("phase", p->phase_counter)("block_id", p->block_id)
                 ("prop_id", p->proposal_id)("num", proposal_chain.size())("block_num_2", last->block_num())
                 ("phase_2", last->phase_counter)("block_id_2", last->block_id)("prop_id_2", last->proposal_id));
      } else {
         fc_dlog(_logger, " === ${id} committed proposal #${block_num}:${phase} block_id: ${block_id} proposal_id: ${prop_id}",
                 ("id", _id)("block_num", p->block_num())("phase", p->phase_counter)
                 ("block_id", p->block_id)("prop_id", p->proposal_id));
      }
   }
}

}
