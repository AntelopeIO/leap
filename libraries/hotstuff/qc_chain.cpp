#include <eosio/hotstuff/qc_chain.hpp>
#include <fc/scoped_exit.hpp>
#include <boost/range/adaptor/reversed.hpp>

namespace eosio::hotstuff {

   void qc_chain::write_safety_state_file() {
      if (_safety_state_file.empty())
         return;
      if (!_safety_state_file_handle.is_open())
         _safety_state_file_handle.open(fc::cfile::create_or_update_rw_mode);
      state_db_manager<safety_state>::write(_safety_state_file_handle, _safety_state);
   }

   const hs_proposal* qc_chain::get_proposal(const fc::sha256& proposal_id) {
      proposal_store_type::nth_index<0>::type::iterator itr = _proposal_store.get<by_proposal_id>().find( proposal_id );
      if (itr == _proposal_store.get<by_proposal_id>().end())
         return nullptr;
      return &(*itr);
   }

   bool qc_chain::insert_proposal(const hs_proposal& proposal) {
      if (get_proposal( proposal.proposal_id ) != nullptr)
         return false;
      _proposal_store.insert(proposal); //new proposal
      return true;
   }

   void qc_chain::get_state(finalizer_state& fs) const {
      fs.b_leaf                 = _b_leaf;
      fs.b_lock                 = _safety_state.get_b_lock();
      fs.b_exec                 = _b_exec;
      fs.b_finality_violation   = _b_finality_violation;
      fs.block_exec             = _block_exec;
      fs.pending_proposal_block = _pending_proposal_block;
      fs.v_height               = _safety_state.get_v_height();
      fs.high_qc                = _high_qc.to_msg();
      fs.current_qc             = _current_qc.to_msg();
      auto hgt_itr = _proposal_store.get<by_proposal_height>().begin();
      auto end_itr = _proposal_store.get<by_proposal_height>().end();
      while (hgt_itr != end_itr) {
         const hs_proposal & p = *hgt_itr;
         fs.proposals.emplace( p.proposal_id, p );
         ++hgt_itr;
      }
   }

   uint32_t qc_chain::positive_bits_count(const hs_bitset& finalizers) {
      return finalizers.count(); // the number of bits in this bitset that are set.
   }

   hs_bitset qc_chain::update_bitset(const hs_bitset& finalizer_set, const fc::crypto::blslib::bls_public_key& finalizer_key ) {

      hs_bitset b(finalizer_set );

      const auto& finalizers = _pacemaker->get_finalizer_set().finalizers;

      for (size_t i = 0; i < finalizers.size();i++) {
         if (finalizers[i].public_key == finalizer_key) {
            b.set(i);

            fc_tlog(_logger, " === finalizer found ${finalizer} new value : ${value}",
                    ("value", [&](){ std::string r; boost::to_string(b, r); return r; }()));

            return b;
         }
      }
      fc_tlog(_logger, " *** finalizer_key not found ${finalizer_key}",
           ("finalizer_key", finalizer_key));
      throw std::runtime_error("qc_chain internal error: finalizer_key not found");
   }

   std::vector<hs_proposal> qc_chain::get_qc_chain(const fc::sha256& proposal_id) {
      std::vector<hs_proposal> ret_arr;
      if ( const hs_proposal* b2 = get_proposal( proposal_id ) ) {
         ret_arr.push_back( *b2 );
         if (const hs_proposal* b1 = get_proposal( b2->justify.proposal_id ) ) {
            ret_arr.push_back( *b1 );
            if (const hs_proposal* b = get_proposal( b1->justify.proposal_id ) ) {
               ret_arr.push_back( *b );
            }
         }
      }
      return ret_arr;
   }

   hs_proposal qc_chain::new_proposal_candidate(const block_id_type& block_id, uint8_t phase_counter) {
      hs_proposal b_new;
      b_new.block_id = block_id;
      b_new.parent_id =  _b_leaf;
      b_new.phase_counter = phase_counter;
      b_new.justify = _high_qc.to_msg(); //or null if no _high_qc upon activation or chain launch
      if (!b_new.justify.proposal_id.empty()){
         std::vector<hs_proposal> current_qc_chain = get_qc_chain(b_new.justify.proposal_id);
         size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());
         if (chain_length>=2){
            auto itr = current_qc_chain.begin();
            hs_proposal b2 = *itr;
            itr++;
            hs_proposal b1 = *itr;
            if (b_new.parent_id == b2.proposal_id && b2.parent_id == b1.proposal_id) b_new.final_on_qc = b1.proposal_id;
            else {
               const hs_proposal *p = get_proposal( b1.parent_id );
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

   bool qc_chain::evaluate_quorum(const hs_bitset& finalizers, const fc::crypto::blslib::bls_signature& agg_sig, const hs_proposal& proposal) {
      if (positive_bits_count(finalizers) < _pacemaker->get_quorum_threshold()){
         return false;
      }
      const auto& c_finalizers = _pacemaker->get_finalizer_set().finalizers;
      std::vector<fc::crypto::blslib::bls_public_key> keys;
      keys.reserve(finalizers.size());
      for (hs_bitset::size_type i = 0; i < finalizers.size(); ++i)
         if (finalizers[i])
            keys.push_back(c_finalizers[i].public_key);
      fc::crypto::blslib::bls_public_key agg_key = fc::crypto::blslib::aggregate(keys);

      digest_type digest = proposal.get_proposal_id(); //get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

      std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);
      bool ok = fc::crypto::blslib::verify(agg_key, h, agg_sig);
      return ok;
   }

   bool qc_chain::is_quorum_met(const quorum_certificate& qc, const hs_proposal& proposal) {

      if (qc.is_quorum_met()) {
         return true; //skip evaluation if we've already verified quorum was met
      }
      else {
         fc_tlog(_logger, " === qc : ${qc}", ("qc", qc.to_msg()));
         // If the caller wants to update the quorum_met flag on its "qc" object, it will have to do so
         //   based on the return value of this method, since "qc" here is const.
         return evaluate_quorum(qc.get_active_finalizers(), qc.get_active_agg_sig(), proposal);
      }
   }

   qc_chain::qc_chain(std::string id,
                      base_pacemaker* pacemaker,
                      std::set<name> my_producers,
                      bls_key_map_t finalizer_keys,
                      fc::logger& logger,
                      std::string safety_state_file)
      : _pacemaker(pacemaker),
        _my_producers(std::move(my_producers)),
        _my_finalizer_keys(std::move(finalizer_keys)),
        _id(std::move(id)),
        _safety_state_file(safety_state_file),
        _logger(logger)
   {
      //todo : read liveness state / select initialization heuristics ?

      if (!_safety_state_file.empty()) {
         _safety_state_file_handle.set_file_path(safety_state_file);
         state_db_manager<safety_state>::read(_safety_state_file, _safety_state);
      }

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

      const auto& finalizers = _pacemaker->get_finalizer_set().finalizers;
         return !_my_finalizer_keys.empty() &&
            std::any_of(finalizers.begin(), finalizers.end(), [&](const auto& fa) { return _my_finalizer_keys.contains(fa.public_key); });

   }

   hs_vote_message qc_chain::sign_proposal(const hs_proposal& proposal,
                                           const fc::crypto::blslib::bls_public_key& finalizer_pub_key,
                                           const fc::crypto::blslib::bls_private_key& finalizer_priv_key)
   {
      _safety_state.set_v_height(finalizer_pub_key, proposal.get_view_number());

      digest_type digest = proposal.get_proposal_id(); //get_digest_to_sign(proposal.block_id, proposal.phase_counter, proposal.final_on_qc);

      std::vector<uint8_t> h = std::vector<uint8_t>(digest.data(), digest.data() + 32);

      fc::crypto::blslib::bls_signature sig = finalizer_priv_key.sign(h);

      hs_vote_message v_msg = {proposal.proposal_id, finalizer_priv_key.get_public_key(), sig};
      return v_msg;
   }

   // Proposal messages are no longer sent through the network, so this method does not do propagation.
   // test_pacemaker bypasses the topology emulation, so proposals are sent to all emulated test nodes.
   void qc_chain::process_proposal(const hs_proposal& proposal){

      if (!proposal.justify.proposal_id.empty()) {
         const hs_proposal *jp = get_proposal( proposal.justify.proposal_id );
         if (jp == nullptr) {
            fc_elog(_logger, " *** ${id} proposal justification unknown : ${proposal_id}", ("id",_id)("proposal_id", proposal.justify.proposal_id));
            return; //can't recognize a proposal with an unknown justification
         }
      }

      const hs_proposal *p = get_proposal( proposal.proposal_id );
      if (p != nullptr) {
         fc_elog(_logger, " *** ${id} proposal received twice : ${proposal_id}", ("id",_id)("proposal_id", proposal.proposal_id));
         if (p->justify.proposal_id != proposal.justify.proposal_id) {
            fc_elog(_logger, " *** ${id} two identical proposals (${proposal_id}) have different justifications :  ${justify_1} vs  ${justify_2}",
                              ("id",_id)
                              ("proposal_id", proposal.proposal_id)
                              ("justify_1", p->justify.proposal_id)
                              ("justify_2", proposal.justify.proposal_id));
         }
         return; //already aware of proposal, nothing to do
      }

      //height is not necessarily unique, so we iterate over all prior proposals at this height
      auto hgt_itr = _proposal_store.get<by_proposal_height>().lower_bound( proposal.get_key() );
      auto end_itr = _proposal_store.get<by_proposal_height>().upper_bound( proposal.get_key() );
      while (hgt_itr != end_itr)
      {
         const hs_proposal & existing_proposal = *hgt_itr;
         fc_elog(_logger, " *** ${id} received a different proposal at the same height (${block_num}, ${phase_counter})",
                           ("id",_id)
                           ("block_num", existing_proposal.block_num())
                           ("phase_counter", existing_proposal.phase_counter));

         fc_elog(_logger, " *** Proposal #1 : ${proposal_id_1} Proposal #2 : ${proposal_id_2}",
                           ("proposal_id_1", existing_proposal.proposal_id)
                           ("proposal_id_2", proposal.proposal_id));
         hgt_itr++;
      }

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

      if (signature_required && !_my_finalizer_keys.empty()){
         //iterate over all my finalizer keys and sign / broadcast for each that is in the schedule
         const auto& finalizers = _pacemaker->get_finalizer_set().finalizers;

         for (const auto& i : finalizers) {
            auto mfk_itr = _my_finalizer_keys.find(i.public_key);

            if (mfk_itr!=_my_finalizer_keys.end()) {

               hs_vote_message v_msg = sign_proposal(proposal, mfk_itr->first, mfk_itr->second);

               fc_tlog(_logger, " === ${id} signed proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id}",
                              ("id", _id)
                              ("block_num", proposal.block_num())
                              ("phase_counter", proposal.phase_counter)
                              ("proposal_id", proposal.proposal_id));

               msgs.push_back(v_msg);
            }
         }

      }
      else fc_tlog(_logger, " === ${id} skipping signature on proposal : block_num ${block_num} phase ${phase_counter} : proposal_id ${proposal_id}",
                          ("id", _id)
                          ("block_num", proposal.block_num())
                          ("phase_counter", proposal.phase_counter)
                          ("proposal_id", proposal.proposal_id));

      //update internal state
      update(proposal);

      write_safety_state_file();

      for (auto &msg : msgs) {
         send_hs_vote_msg( std::nullopt, msg );
      }

      //check for leader change
      leader_rotation_check();

      //auto total_time = fc::time_point::now() - start;
      //fc_dlog(_logger, " ... process_proposal() total time : ${total_time}", ("total_time", total_time));
   }

   void qc_chain::process_vote(const std::optional<uint32_t>& connection_id, const hs_vote_message& vote){

      //auto start = fc::time_point::now();
#warning check for duplicate or invalid vote. We will return in either case, but keep proposals for evidence of double signing
      //TODO: check for duplicate or invalid vote. We will return in either case, but keep proposals for evidence of double signing

      bool am_leader = am_i_leader();

      if (am_leader) {
         if (vote.proposal_id != _current_qc.get_proposal_id()) {
            send_hs_message_warning(connection_id, hs_message_warning::discarded); // example; to be tuned to actual need
            return;
         }
       }

      const hs_proposal *p = get_proposal( vote.proposal_id );
      if (p == nullptr) {
         if (am_leader)
            fc_elog(_logger, " *** ${id} couldn't find proposal, vote : ${vote}", ("id",_id)("vote", vote));
         send_hs_message_warning(connection_id, hs_message_warning::discarded); // example; to be tuned to actual need
         return;
      }

      // if not leader, check message propagation and quit
      if (! am_leader) {
         seen_votes_store_type::nth_index<0>::type::iterator itr = _seen_votes_store.get<by_seen_votes_proposal_id>().find( p->proposal_id );
         bool propagate = false;
         if (itr == _seen_votes_store.get<by_seen_votes_proposal_id>().end()) {
            seen_votes sv = { p->proposal_id, p->get_key(), { vote.finalizer_key } };
            _seen_votes_store.insert(sv);
            propagate = true;
         } else {
            _seen_votes_store.get<by_seen_votes_proposal_id>().modify(itr, [&](seen_votes& sv) {
               if (sv.finalizers.count(vote.finalizer_key) == 0) {
                  sv.finalizers.insert(vote.finalizer_key);
                  propagate = true;
               }
            });
         }
         if (propagate)
            send_hs_vote_msg(connection_id, vote);
         return;
      }

      fc_tlog(_logger, " === Process vote from ${finalizer_key} : current bitset ${value}" ,
              ("finalizer_key", vote.finalizer_key)("value", _current_qc.get_active_finalizers_string()));

      bool quorum_met = _current_qc.is_quorum_met(); //check if quorum already met

      // If quorum is already met, we don't need to do anything else. Otherwise, we aggregate the signature.
      if (!quorum_met){

         auto increment_version = fc::make_scoped_exit([this]() { ++_state_version; });

         const hs_bitset& finalizer_set = _current_qc.get_active_finalizers();

         // if a finalizer has already aggregated a vote signature for the current QC, just discard this vote

         const auto& finalizers = _pacemaker->get_finalizer_set().finalizers;
         for (size_t i=0; i<finalizers.size(); ++i)
            if (finalizers[i].public_key == vote.finalizer_key)
               if (finalizer_set.test(i))
                  return;

         if (finalizer_set.any())
            _current_qc.set_active_agg_sig(fc::crypto::blslib::aggregate({_current_qc.get_active_agg_sig(), vote.sig }));
         else
            _current_qc.set_active_agg_sig(vote.sig);
         fc_tlog(_logger, " === update bitset ${value} ${finalizer_key}", ("value", _current_qc.get_active_finalizers_string())("finalizer_key", vote.finalizer_key));
         _current_qc.set_active_finalizers(update_bitset(finalizer_set, vote.finalizer_key));

         quorum_met = is_quorum_met(_current_qc, *p);

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
         }
      }

      //auto total_time = fc::time_point::now() - start;
      //fc_tlog(_logger, " ... process_vote() total time : ${total_time}", ("total_time", total_time));
   }

   void qc_chain::process_new_view(const std::optional<uint32_t>& connection_id, const hs_new_view_message& msg){
      fc_tlog(_logger, " === ${id} process_new_view === ${qc}", ("qc", msg.high_qc)("id", _id));
      auto increment_version = fc::make_scoped_exit([this]() { ++_state_version; });
      if (!update_high_qc(quorum_certificate{msg.high_qc, 21})) { // TODO: use active schedule size
         increment_version.cancel();
      } else {
         // Always propagate a view that's newer than ours.
         // If it's not newer, then we have already propagated ours.
         // If the recipient doesn't think ours is newer, it has already propagated its own, and so on.
         send_hs_new_view_msg(connection_id, msg);
      }
   }

   void qc_chain::test_receive_proposal(const hs_proposal& proposal) {
      process_proposal(proposal);
   }

   hs_proposal qc_chain::test_create_proposal(const block_id_type& block_id) {
      return create_proposal(block_id);
   }

   hs_proposal qc_chain::create_proposal(const block_id_type& block_id) {
      auto increment_version = fc::make_scoped_exit([this]() { ++_state_version; });

      if (!_current_qc.get_proposal_id().empty() && !_current_qc.is_quorum_met()) {

         fc_tlog(_logger, " === ${id} pending proposal found ${proposal_id} : quorum met ${quorum_met}",
                        ("id", _id)
                        ("proposal_id", _current_qc.get_proposal_id())
                        ("quorum_met", _current_qc.is_quorum_met()));

         fc_tlog(_logger, " === ${id} setting _pending_proposal_block to ${block_id} (create_proposal)", ("id", _id)("block_id", block_id));
         _pending_proposal_block = block_id;

#warning TODO/REVIEW: I guess in this case we just keep the proposal we have and return it?
         return *get_proposal(_current_qc.get_proposal_id());

      } else {

         fc_tlog(_logger, " === ${id} preparing new proposal ${proposal_id} (test_create_proposal): quorum met ${quorum_met}",
                        ("id", _id)
                        ("proposal_id", _current_qc.get_proposal_id())
                        ("quorum_met", _current_qc.is_quorum_met()));
         hs_proposal proposal_candidate = new_proposal_candidate( block_id, 0 );

         reset_qc(proposal_candidate.proposal_id);

         fc_tlog(_logger, " === ${id} setting _pending_proposal_block to null (test_create_proposal)", ("id", _id));

         _pending_proposal_block = {};
         _b_leaf = proposal_candidate.proposal_id;

         //todo : asynchronous?
         //write_state(_liveness_state_file , _liveness_state);

         fc_tlog(_logger, " === ${id} _b_leaf updated (test_create_proposal): ${proposal_id}", ("proposal_id", proposal_candidate.proposal_id)("id", _id));

         // this is for testing, so we will just return it.
         // the test_pacemaker can loop calling qc_chain::test_receive_proposal() on the returned proposal
         //send_hs_proposal_msg( proposal_candidate );
         return proposal_candidate;
      }
   }

   void qc_chain::send_hs_vote_msg(const std::optional<uint32_t>& connection_id, const hs_vote_message & msg){
      fc_tlog(_logger, " === broadcast_hs_vote ===");
      _pacemaker->send_hs_vote_msg(msg, _id, connection_id);
      if (!connection_id.has_value())
         process_vote( std::nullopt, msg );
   }

   void qc_chain::send_hs_new_view_msg(const std::optional<uint32_t>& connection_id, const hs_new_view_message & msg){
      fc_tlog(_logger, " === broadcast_hs_new_view ===");
      _pacemaker->send_hs_new_view_msg(msg, _id, connection_id);
   }

   void qc_chain::send_hs_message_warning(const std::optional<uint32_t>& connection_id, const chain::hs_message_warning code) {
      if (connection_id.has_value())
         _pacemaker->send_hs_message_warning(connection_id.value(), code);
   }

   //extends predicate
   bool qc_chain::extends(const fc::sha256& descendant, const fc::sha256& ancestor){

#warning confirm the extends predicate never has to verify extension of irreversible blocks, otherwise this function needs to be modified
      //TODO: confirm the extends predicate never has to verify extension of irreversible blocks, otherwise this function needs to be modified

      uint32_t counter = 0;
      const hs_proposal *p = get_proposal( descendant );
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

   // returns true on state change (caller decides update on state version
   bool qc_chain::update_high_qc(const quorum_certificate& high_qc) {

      fc_tlog(_logger, " === check to update high qc ${proposal_id}", ("proposal_id", high_qc.get_proposal_id()));

      // if new high QC is higher than current, update to new

      if (_high_qc.get_proposal_id().empty()){

         _high_qc = high_qc;
         _b_leaf = _high_qc.get_proposal_id();

         //todo : asynchronous?
         //write_state(_liveness_state_file , _liveness_state);

         fc_tlog(_logger, " === ${id} _b_leaf updated (update_high_qc) : ${proposal_id}", ("proposal_id", _high_qc.get_proposal_id())("id", _id));

         // avoid looping message propagation when receiving a new-view message with a high_qc.get_proposal_id().empty().
         // not sure if high_qc.get_proposal_id().empty() + _high_qc.get_proposal_id().empty() is something that actually ever happens in the real world.
         // not sure if high_qc.get_proposal_id().empty() should be tested and always rejected (return false + no _high_qc / _b_leaf update).
         // if this returns false, we won't update the get_finality_status information, but I don't think we care about that at all.
         return !high_qc.get_proposal_id().empty();
      } else {
         const hs_proposal *old_high_qc_prop = get_proposal( _high_qc.get_proposal_id() );
         const hs_proposal *new_high_qc_prop = get_proposal( high_qc.get_proposal_id() );

         if (old_high_qc_prop == nullptr)
            return false;
         if (new_high_qc_prop == nullptr)
            return false;

         if (new_high_qc_prop->get_view_number() > old_high_qc_prop->get_view_number()
             && is_quorum_met(high_qc, *new_high_qc_prop))
         {
            fc_tlog(_logger, " === updated high qc, now is : #${view_number}  ${proposal_id}", ("view_number", new_high_qc_prop->get_view_number())("proposal_id", new_high_qc_prop->proposal_id));
            _high_qc = high_qc;
            _high_qc.set_quorum_met();
            _b_leaf = _high_qc.get_proposal_id();

            //todo : asynchronous?
            //write_state(_liveness_state_file , _liveness_state);

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

         send_hs_new_view_msg( std::nullopt, new_view );
      }
   }

   //safenode predicate
   bool qc_chain::is_node_safe(const hs_proposal& proposal) {

      //fc_tlog(_logger, " === is_node_safe ===");

      bool monotony_check = false;
      bool safety_check = false;
      bool liveness_check = false;
      bool final_on_qc_check = false;

      fc::sha256 upcoming_commit;

      if (proposal.justify.proposal_id.empty() && _safety_state.get_b_lock().empty()) {

         final_on_qc_check = true; //if chain just launched or feature just activated
      } else {

         std::vector<hs_proposal> current_qc_chain = get_qc_chain(proposal.justify.proposal_id);

         size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

         if (chain_length >= 2) {

            auto itr = current_qc_chain.begin();

            hs_proposal b2 = *itr;
            ++itr;
            hs_proposal b1 = *itr;

            if (proposal.parent_id == b2.proposal_id && b2.parent_id == b1.proposal_id)
               upcoming_commit = b1.proposal_id;
            else {
               const hs_proposal *p = get_proposal( b1.parent_id );
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

      if (proposal.get_view_number() > _safety_state.get_v_height()) {
         monotony_check = true;
      }

      if (!_safety_state.get_b_lock().empty()){

         //Safety check : check if this proposal extends the chain I'm locked on
         if (extends(proposal.proposal_id, _safety_state.get_b_lock())) {
            safety_check = true;
         }

         //Liveness check : check if the height of this proposal's justification is higher than the height of the proposal I'm locked on. This allows restoration of liveness if a replica is locked on a stale block.

         if (proposal.justify.proposal_id.empty() && _safety_state.get_b_lock().empty()) {

            liveness_check = true; //if there is no justification on the proposal and I am not locked on anything, means the chain just launched or feature just activated
         } else {
            const hs_proposal *b_lock = get_proposal( _safety_state.get_b_lock() );
            EOS_ASSERT( b_lock != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", _safety_state.get_b_lock()) );
            const hs_proposal *prop_justification = get_proposal( proposal.justify.proposal_id );
            EOS_ASSERT( prop_justification != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", proposal.justify.proposal_id) );

            if (prop_justification->get_view_number() > b_lock->get_view_number()) {
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

   //on vote received, called from network thread
   void qc_chain::on_hs_vote_msg(const uint32_t connection_id, const hs_vote_message& msg) {
      process_vote( std::optional<uint32_t>(connection_id), msg );
   }

   //on new view received, called from network thread
   void qc_chain::on_hs_new_view_msg(const uint32_t connection_id, const hs_new_view_message& msg) {
      process_new_view( std::optional<uint32_t>(connection_id), msg );
   }

   void qc_chain::update(const hs_proposal& proposal) {
      //fc_tlog(_logger, " === update internal state ===");
      //if proposal has no justification, means we either just activated the feature or launched the chain, or the proposal is invalid
      if (proposal.justify.proposal_id.empty()) {
         fc_dlog(_logger, " === ${id} proposal has no justification ${proposal_id}", ("proposal_id", proposal.proposal_id)("id", _id));
         return;
      }

      std::vector<hs_proposal> current_qc_chain = get_qc_chain(proposal.justify.proposal_id);

      size_t chain_length = std::distance(current_qc_chain.begin(), current_qc_chain.end());

      const hs_proposal *b_lock = get_proposal( _safety_state.get_b_lock() );
      EOS_ASSERT( b_lock != nullptr || _safety_state.get_b_lock().empty() , chain_exception, "expected hs_proposal ${id} not found", ("id", _safety_state.get_b_lock()) );

      //fc_tlog(_logger, " === update_high_qc : proposal.justify ===");
      update_high_qc(quorum_certificate{proposal.justify, 21}); // TODO: use active schedule size

      if (chain_length<1){
         fc_dlog(_logger, " === ${id} qc chain length is 0", ("id", _id));
         return;
      }

      auto itr = current_qc_chain.begin();
      hs_proposal b_2 = *itr;

      if (chain_length<2){
         fc_dlog(_logger, " === ${id} qc chain length is 1", ("id", _id));
         return;
      }

      itr++;

      hs_proposal b_1 = *itr;

      //if we're not locked on anything, means we just activated or chain just launched, else we verify if we've progressed enough to establish a new lock

      fc_tlog(_logger, " === ${id} _b_lock ${_b_lock} b_1 height ${b_1_height}",
                     ("id", _id)
                     ("_b_lock", _safety_state.get_b_lock())
                     ("b_1_height", b_1.block_num())
                     ("b_1_phase", b_1.phase_counter));

      if ( b_lock != nullptr ) {
         fc_tlog(_logger, " === b_lock height ${b_lock_height} b_lock phase ${b_lock_phase}",
                        ("b_lock_height", b_lock->block_num())
                        ("b_lock_phase", b_lock->phase_counter));
      }

      if (_safety_state.get_b_lock().empty() || b_1.get_view_number() > b_lock->get_view_number()){

         fc_tlog(_logger, "setting _b_lock to ${proposal_id}", ("proposal_id",b_1.proposal_id ));

         for (const auto& f_itr : _my_finalizer_keys) {
            _safety_state.set_b_lock(f_itr.first, b_1.proposal_id); //commit phase on b1
         }

         fc_tlog(_logger, " === ${id} _b_lock updated : ${proposal_id}", ("proposal_id", b_1.proposal_id)("id", _id));
      }

      if (chain_length < 3) {
         fc_dlog(_logger, " === ${id} qc chain length is 2",("id", _id));
         return;
      }

      ++itr;

      hs_proposal b = *itr;

      fc_tlog(_logger, " === direct parent relationship verification : b_2.parent_id ${b_2.parent_id} b_1.proposal_id ${b_1.proposal_id} b_1.parent_id ${b_1.parent_id} b.proposal_id ${b.proposal_id} ",
                ("b_2.parent_id",b_2.parent_id)
                ("b_1.proposal_id", b_1.proposal_id)
                ("b_1.parent_id", b_1.parent_id)
                ("b.proposal_id", b.proposal_id));

      //direct parent relationship verification
      if (b_2.parent_id == b_1.proposal_id && b_1.parent_id == b.proposal_id){

         if (!_b_exec.empty()){

            const hs_proposal *b_exec = get_proposal( _b_exec );
            EOS_ASSERT( b_exec != nullptr , chain_exception, "expected hs_proposal ${id} not found", ("id", _b_exec) );

            if (b_exec->get_view_number() >= b.get_view_number() && b_exec->proposal_id != b.proposal_id){

               fc_elog(_logger, " *** ${id} finality violation detected at height ${block_num}, phase : ${phase}. Proposal ${proposal_id_1} conflicts with ${proposal_id_2}",
                       ("id", _id)
                       ("block_num", b.block_num())
                       ("phase", b.phase_counter)
                       ("proposal_id_1", b.proposal_id)
                       ("proposal_id_2", b_exec->proposal_id));

               _b_finality_violation = b.proposal_id;

               //protocol failure
               return;
            }
         }

         commit(b); //todo : ensure that block is marked irreversible / lib is updated etc.

         //todo : asynchronous?
         //write_state(_liveness_state_file , _liveness_state);

         fc_tlog(_logger, " === last executed proposal : #${block_num} ${block_id}", ("block_num", b.block_num())("block_id", b.block_id));

         _b_exec = b.proposal_id; //decide phase on b
         _block_exec = b.block_id;

         gc_proposals( b.get_key()-1);
      }
      else {
         fc_elog(_logger, " *** ${id} could not verify direct parent relationship", ("id",_id));
         fc_elog(_logger, "   *** b_2 ${b_2}", ("b_2", b_2));
         fc_elog(_logger, "   *** b_1 ${b_1}", ("b_1", b_1));
         fc_elog(_logger, "   *** b   ${b}", ("b", b));
      }
   }

   void qc_chain::gc_proposals(uint64_t cutoff){
      //fc_tlog(_logger, " === garbage collection on old data");

      auto& seen_votes_index = _seen_votes_store.get<by_seen_votes_proposal_height>();
      seen_votes_index.erase(seen_votes_index.begin(), seen_votes_index.upper_bound(cutoff));

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
   }

void qc_chain::commit(const hs_proposal& initial_proposal) {
   std::vector<const hs_proposal*> proposal_chain;
   proposal_chain.reserve(10);

   const hs_proposal* p = &initial_proposal;
   while (p) {
      fc_tlog(_logger, " === attempting to commit proposal #${block_num}:${phase} ${prop_id} block_id: ${block_id} parent_id: ${parent_id}",
              ("block_num", p->block_num())("prop_id", p->proposal_id)("block_id", p->block_id)
              ("phase", p->phase_counter)("parent_id", p->parent_id));

      const hs_proposal* last_exec_prop = get_proposal(_b_exec);
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


      bool exec_height_check = _b_exec.empty() || last_exec_prop->get_view_number() < p->get_view_number();
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
         // Execute commands [...]
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
