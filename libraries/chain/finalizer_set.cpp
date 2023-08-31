#include <eosio/chain/finalizer_set.hpp>
#include <eosio/chain/finalizer_authority.hpp>
#include <fc/crypto/bls_public_key.hpp>

namespace eosio::chain {

   /**
    * These definitions are all here to avoid including bls_public_key.hpp which includes <bls12-381/bls12-381.hpp>
    * and pulls in bls12-381 types. This keeps bls12-381 out of libtester.
    */

   finalizer_set::finalizer_set() = default;
   finalizer_set::~finalizer_set() = default;

   finalizer_set::finalizer_set(const finalizer_set&) = default;
   finalizer_set::finalizer_set(finalizer_set&&) = default;

   finalizer_set& finalizer_set::operator=(const finalizer_set&) = default;
   finalizer_set& finalizer_set::operator=(finalizer_set&&) = default;

   auto finalizer_set::operator<=>(const finalizer_set&) const = default;


   hs_finalizer_set_extension::hs_finalizer_set_extension(const finalizer_set& s)
           : finalizer_set(s) {}

} /// eosio::chain
