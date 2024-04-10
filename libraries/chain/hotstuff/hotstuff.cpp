#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

inline std::string bitset_to_string(const hs_bitset& bs) {
   std::string r;
   boost::to_string(bs, r);
   return r;
}

inline hs_bitset vector_to_bitset(const std::vector<uint32_t>& v) {
   return {v.cbegin(), v.cend()};
}

inline std::vector<uint32_t> bitset_to_vector(const hs_bitset& bs) {
   std::vector<uint32_t> r;
   r.resize(bs.num_blocks());
   boost::to_block_range(bs, r.begin());
   return r;
}

bool pending_quorum_certificate::has_voted(size_t index) const {
   return _strong_votes.has_voted(index) || _weak_votes.has_voted(index);
}

bool pending_quorum_certificate::has_voted_no_lock(bool strong, size_t index) const {
   if (strong) {
      return _strong_votes.has_voted(index);
   }
   return _weak_votes.has_voted(index);
}

void pending_quorum_certificate::votes_t::reflector_init() {
   _processed = std::vector<std::atomic<bool>>(_bitset.size());
   for (size_t i = 0; i < _bitset.size(); ++i) {
      if (_bitset[i]) {
         _processed[i].store(true, std::memory_order_relaxed);
      }
   }
}

bool pending_quorum_certificate::votes_t::has_voted(size_t index) const {
   assert(index < _processed.size());
   return _processed[index].load(std::memory_order_relaxed);
}


vote_status pending_quorum_certificate::votes_t::add_vote(size_t index, const bls_signature& sig) {
   if (_bitset[index]) { // check here as could have come in while unlocked
      return vote_status::duplicate; // shouldn't be already present
   }
   _processed[index].store(true, std::memory_order_relaxed);
   _bitset.set(index);
   _sig.aggregate(sig); // works even if _sig is default initialized (fp2::zero())
   return vote_status::success;
}

pending_quorum_certificate::pending_quorum_certificate()
   : _mtx(std::make_unique<std::mutex>()) {
}

pending_quorum_certificate::pending_quorum_certificate(size_t num_finalizers, uint64_t quorum, uint64_t max_weak_sum_before_weak_final)
   : _mtx(std::make_unique<std::mutex>())
   , _quorum(quorum)
   , _max_weak_sum_before_weak_final(max_weak_sum_before_weak_final)
   , _weak_votes(num_finalizers)
   , _strong_votes(num_finalizers) {
}

bool pending_quorum_certificate::is_quorum_met() const {
   std::lock_guard g(*_mtx);
   return is_quorum_met_no_lock();
}

// called by add_vote, already protected by mutex
vote_status pending_quorum_certificate::add_strong_vote(size_t index, const bls_signature& sig, uint64_t weight) {
   if (auto s = _strong_votes.add_vote(index, sig); s != vote_status::success) {
      return s;
   }
   _strong_sum += weight;

   switch (_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (_strong_sum >= _quorum) {
         assert(_state != state_t::restricted);
         _state = state_t::strong;
      } else if (_weak_sum + _strong_sum >= _quorum)
         _state = (_state == state_t::restricted) ? state_t::weak_final : state_t::weak_achieved;
      break;

   case state_t::weak_achieved:
      if (_strong_sum >= _quorum)
         _state = state_t::strong;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another strong vote...nothing to do
      break;
   }
   return vote_status::success;
}

// called by add_vote, already protected by mutex
vote_status pending_quorum_certificate::add_weak_vote(size_t index, const bls_signature& sig, uint64_t weight) {
   if (auto s = _weak_votes.add_vote(index, sig); s != vote_status::success)
      return s;
   _weak_sum += weight;

   switch (_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (_weak_sum + _strong_sum >= _quorum)
         _state = state_t::weak_achieved;

      if (_weak_sum > _max_weak_sum_before_weak_final) {
         if (_state == state_t::weak_achieved)
            _state = state_t::weak_final;
         else if (_state == state_t::unrestricted)
            _state = state_t::restricted;
      }
      break;

   case state_t::weak_achieved:
      if (_weak_sum >= _max_weak_sum_before_weak_final)
         _state = state_t::weak_final;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another weak vote... nothing to do
      break;
   }
   return vote_status::success;
}

// thread safe
vote_status pending_quorum_certificate::add_vote(block_num_type block_num, bool strong, std::span<const uint8_t> proposal_digest, size_t index,
                                                 const bls_public_key& pubkey, const bls_signature& sig, uint64_t weight) {
   vote_status s = vote_status::success;

   if (has_voted_no_lock(strong, index)) {
      dlog("block_num: ${bn}, vote strong: ${sv}, duplicate", ("bn", block_num)("sv", strong));
      return vote_status::duplicate;
   }

   if (!fc::crypto::blslib::verify(pubkey, proposal_digest, sig)) {
      wlog( "signature from finalizer ${i} cannot be verified", ("i", index) );
      return vote_status::invalid_signature;
   }

   std::unique_lock g(*_mtx);
   state_t pre_state = _state;
   s = strong ? add_strong_vote(index, sig, weight)
              : add_weak_vote(index, sig, weight);
   state_t post_state = _state;
   g.unlock();

   dlog("block_num: ${bn}, vote strong: ${sv}, status: ${s}, pre-state: ${pre}, post-state: ${state}, quorum_met: ${q}",
        ("bn", block_num)("sv", strong)("s", s)("pre", pre_state)("state", post_state)("q", is_quorum_met(post_state)));
   return s;
}

// called by get_best_qc which acquires a mutex
valid_quorum_certificate pending_quorum_certificate::to_valid_quorum_certificate() const {
   valid_quorum_certificate valid_qc;

   if( _state == state_t::strong ) {
      valid_qc._strong_votes = _strong_votes._bitset;
      valid_qc._sig          = _strong_votes._sig;
   } else if (is_quorum_met_no_lock()) {
      valid_qc._strong_votes = _strong_votes._bitset;
      valid_qc._weak_votes   = _weak_votes._bitset;
      valid_qc._sig          = _strong_votes._sig;
      valid_qc._sig.aggregate(_weak_votes._sig);
   } else
      assert(0); // this should be called only when we have a valid qc.

   return valid_qc;
}

std::optional<quorum_certificate> pending_quorum_certificate::get_best_qc(block_num_type block_num) const {
   std::lock_guard g(*_mtx);
   // if pending_qc does not have a valid QC, consider valid_qc only
   if( !is_quorum_met_no_lock() ) {
      if( _valid_qc ) {
         return std::optional{quorum_certificate{ block_num, *_valid_qc }};
      } else {
         return std::nullopt;
      }
   }

   // extract valid QC from pending_qc
   valid_quorum_certificate valid_qc_from_pending = to_valid_quorum_certificate();

   // if valid_qc does not have value, consider valid_qc_from_pending only
   if( !_valid_qc ) {
      return std::optional{quorum_certificate{ block_num, valid_qc_from_pending }};
   }

   // Both valid_qc and valid_qc_from_pending have value. Compare them and select a better one.
   // Strong beats weak. Tie break by valid_qc.
   const auto& best_qc =
      _valid_qc->is_strong() == valid_qc_from_pending.is_strong() ?
      *_valid_qc : // tie broken by valid_qc
      _valid_qc->is_strong() ? *_valid_qc : valid_qc_from_pending; // strong beats weak
   return std::optional{quorum_certificate{ block_num, best_qc }};
}

void pending_quorum_certificate::set_valid_qc(const valid_quorum_certificate& qc) {
   std::lock_guard g(*_mtx);
   _valid_qc = qc;
}

bool pending_quorum_certificate::valid_qc_is_strong() const {
   std::lock_guard g(*_mtx);
   return _valid_qc && _valid_qc->is_strong();
}

bool pending_quorum_certificate::is_quorum_met_no_lock() const {
   return is_quorum_met(_state);
}

} // namespace eosio::chain
