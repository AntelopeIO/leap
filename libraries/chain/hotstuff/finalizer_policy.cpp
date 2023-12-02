#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/hotstuff/finalizer_authority.hpp>
#include <fc/crypto/bls_public_key.hpp>

namespace eosio::chain {

   /**
    * These definitions are all here to avoid including bls_public_key.hpp which includes <bls12-381/bls12-381.hpp>
    * and pulls in bls12-381 types. This keeps bls12-381 out of libtester.
    */

   finalizer_policy::finalizer_policy() = default;
   finalizer_policy::~finalizer_policy() = default;

   finalizer_policy::finalizer_policy(const finalizer_policy&) = default;
   finalizer_policy::finalizer_policy(finalizer_policy&&) noexcept = default;

   finalizer_policy& finalizer_policy::operator=(const finalizer_policy&) = default;
   finalizer_policy& finalizer_policy::operator=(finalizer_policy&&) noexcept = default;

} /// eosio::chain
