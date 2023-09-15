#include <eosio/chain/hotstuff.hpp>
#include <eosio/chain/finalizer_set.hpp>
#include <eosio/chain/finalizer_authority.hpp>

namespace eosio::chain {

bool quorum_certificate_message::verify(const hs_proposal_message& m, const finalizer_set& finset) const {
   const hs_bitset bitset { active_finalizers.cbegin(), active_finalizers.cend() };
   std::size_t num_signers = bitset.count();
   if (num_signers < finset.fthreshold ||
       bitset.size()  < finset.finalizers.size())
      return false;

   std::vector<fc::crypto::blslib::bls_public_key> keys;
   keys.reserve(num_signers);
   for (std::size_t i=0; i<finset.finalizers.size(); ++i) {
      if (bitset.test(i)) 
         keys.push_back(finset.finalizers[i].public_key);
   }

   // fc::crypto::blslib::aggregate and  fc::crypto::blslib::verify should take std::span and not std::vector
   auto agg_key = fc::crypto::blslib::aggregate(keys);
   std::vector<uint8_t> h(proposal_id.data(), proposal_id.data() + proposal_id.data_size()); // use span instead
   return fc::crypto::blslib::verify(agg_key, h, active_agg_sig);
}

bool hs_proposal_message::verify(const hs_proposal_message& parent, const finalizer_set& finset) const {
   // check parent_id? New hotstuff.hpp from Areg makes more sense
   // see: https://gist.github.com/arhag/3071922a6cecf563ad6cca183b8a76d0#file-hotstuff_pseudo_code-hpp-L89
   return justify.proposal_id == parent.proposal_id && justify.verify(parent, finset);
}

bool hs_commitment::verify(const finalizer_set& finset) const {
   return b1.verify(b, finset) && b2.verify(b1, finset) && bstar.verify(b2, finset);
}

} /// eosio::chain
