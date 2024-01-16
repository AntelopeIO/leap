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

bool pending_quorum_certificate::votes_t::add_vote(const std::vector<uint8_t>& proposal_digest, size_t index,
                                                   const bls_public_key& pubkey, const bls_signature& new_sig) {
   if (_bitset[index]) {
      return false; // shouldn't be already present
   }
   if (!fc::crypto::blslib::verify(pubkey, proposal_digest, new_sig)) {
      wlog( "signature from finalizer ${i} cannot be verified", ("i", index) );
      return false;
   }
   _bitset.set(index);
   _sig = fc::crypto::blslib::aggregate({_sig, new_sig}); // works even if _sig is default initialized (fp2::zero())
   return true;
}

void pending_quorum_certificate::votes_t::reset(size_t num_finalizers) {
   if (num_finalizers != _bitset.size())
      _bitset.resize(num_finalizers);
   _bitset.reset();
   _sig = bls_signature();
}

pending_quorum_certificate::pending_quorum_certificate()
   : _mtx(std::make_unique<std::mutex>()) {
}

pending_quorum_certificate::pending_quorum_certificate(size_t num_finalizers, size_t quorum)
   : _num_finalizers(num_finalizers)
   , _quorum(quorum)
   , _mtx(std::make_unique<std::mutex>()) {
   _weak_votes.resize(num_finalizers);
   _strong_votes.resize(num_finalizers);
}

pending_quorum_certificate::pending_quorum_certificate(const fc::sha256&  proposal_id,
                                                       const digest_type& proposal_digest, size_t num_finalizers,
                                                       size_t quorum)
   : pending_quorum_certificate(num_finalizers, quorum) {
   _proposal_id = proposal_id;
   _proposal_digest.assign(proposal_digest.data(), proposal_digest.data() + 32);
}

bool pending_quorum_certificate::is_quorum_met() const {
   std::lock_guard g(*_mtx);
   return _state == state_t::weak_achieved || _state == state_t::weak_final || _state == state_t::strong;
}

void pending_quorum_certificate::reset(const fc::sha256& proposal_id, const digest_type& proposal_digest,
                                       size_t num_finalizers, size_t quorum) {
   std::lock_guard g(*_mtx);
   _proposal_id = proposal_id;
   _proposal_digest.assign(proposal_digest.data(), proposal_digest.data() + 32);
   _quorum = quorum;
   _strong_votes.reset(num_finalizers);
   _weak_votes.reset(num_finalizers);
   _num_finalizers = num_finalizers;
   _state          = state_t::unrestricted;
}

// called by add_vote, already protected by mutex
bool pending_quorum_certificate::add_strong_vote(const std::vector<uint8_t>& proposal_digest, size_t index,
                                                 const bls_public_key& pubkey, const bls_signature& sig) {
   assert(index < _num_finalizers);
   if (!_strong_votes.add_vote(proposal_digest, index, pubkey, sig))
      return false;
   size_t weak   = num_weak();
   size_t strong = num_strong();

   switch (_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (strong >= _quorum) {
         assert(_state != state_t::restricted);
         _state = state_t::strong;
      } else if (weak + strong >= _quorum)
         _state = (_state == state_t::restricted) ? state_t::weak_final : state_t::weak_achieved;
      break;

   case state_t::weak_achieved:
      if (strong >= _quorum)
         _state = state_t::strong;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another strong vote...nothing to do
      break;
   }
   return true;
}

// called by add_vote, already protected by mutex
bool pending_quorum_certificate::add_weak_vote(const std::vector<uint8_t>& proposal_digest, size_t index,
                                               const bls_public_key& pubkey, const bls_signature& sig) {
   assert(index < _num_finalizers);
   if (!_weak_votes.add_vote(proposal_digest, index, pubkey, sig))
      return false;
   size_t weak   = num_weak();
   size_t strong = num_strong();

   switch (_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (weak + strong >= _quorum)
         _state = state_t::weak_achieved;

      if (weak > (_num_finalizers - _quorum)) {
         if (_state == state_t::weak_achieved)
            _state = state_t::weak_final;
         else if (_state == state_t::unrestricted)
            _state = state_t::restricted;
      }
      break;

   case state_t::weak_achieved:
      if (weak >= (_num_finalizers - _quorum))
         _state = state_t::weak_final;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another weak vote... nothing to do
      break;
   }
   return true;
}

// thread safe
bool pending_quorum_certificate::add_vote(bool strong, const std::vector<uint8_t>& proposal_digest, size_t index,
                                          const bls_public_key& pubkey, const bls_signature& sig) {
   std::lock_guard g(*_mtx);
   return strong ? add_strong_vote(proposal_digest, index, pubkey, sig)
                 : add_weak_vote(proposal_digest, index, pubkey, sig);
}

// thread safe
valid_quorum_certificate pending_quorum_certificate::to_valid_quorum_certificate() const {
   std::lock_guard g(*_mtx);

   valid_quorum_certificate valid_qc;

   valid_qc._proposal_id = _proposal_id;
   valid_qc._proposal_digest = _proposal_digest;
   if( _state == state_t::strong ) {
      valid_qc._strong_votes = _strong_votes._bitset;
      valid_qc._sig          = _strong_votes._sig;
   } else if (is_quorum_met()) {
      valid_qc._strong_votes = _strong_votes._bitset;
      valid_qc._weak_votes   = _weak_votes._bitset;
      valid_qc._sig          = fc::crypto::blslib::aggregate({_strong_votes._sig, _weak_votes._sig});
   } else
      assert(0); // this should be called only when we have a valid qc.

   return valid_qc;
}

// ================== begin compatibility functions =======================
// these are present just to make the tests still work. will be removed.
// these assume *only* strong votes.
quorum_certificate_message pending_quorum_certificate::to_msg() const {
   return {.proposal_id    = _proposal_id,
           .strong_votes   = bitset_to_vector(_strong_votes._bitset),
           .active_agg_sig = _strong_votes._sig};
}

std::string pending_quorum_certificate::get_votes_string() const {
   return std::string("strong(\"") + bitset_to_string(_strong_votes._bitset) + "\", weak(\"" +
          bitset_to_string(_weak_votes._bitset) + "\"";
}
// ================== end compatibility functions =======================

valid_quorum_certificate::valid_quorum_certificate(
   const fc::sha256& proposal_id, const std::vector<uint8_t>& proposal_digest,
   const std::vector<uint32_t>& strong_votes, // bitset encoding, following canonical order
   const std::vector<uint32_t>& weak_votes,   // bitset encoding, following canonical order
   const bls_signature&         sig)
   : _proposal_id(proposal_id)
   , _proposal_digest(proposal_digest)
   , _sig(sig) {
   if (!strong_votes.empty())
      _strong_votes = vector_to_bitset(strong_votes);
   if (!weak_votes.empty())
      _weak_votes = vector_to_bitset(weak_votes);
}

quorum_certificate_message valid_quorum_certificate::to_msg() const {
   return {
      .proposal_id    = _proposal_id,
      .strong_votes   = _strong_votes ? bitset_to_vector(*_strong_votes) : std::vector<uint32_t>{1, 0},
      .active_agg_sig = _sig
   };
}

} // namespace eosio::chain
