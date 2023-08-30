#pragma once

#include <eosio/chain/config.hpp>
#include <eosio/chain/types.hpp>
#include <chainbase/chainbase.hpp>
#include <eosio/chain/authority.hpp>
#include <eosio/chain/snapshot.hpp>

#include <fc/crypto/bls_public_key.hpp>

namespace eosio::chain {

   struct finalizer_authority {

      std::string  description;
      uint64_t     fweight = 0; // weight that this finalizer's vote has for meeting fthreshold
      fc::crypto::blslib::bls_public_key  public_key;

      friend bool operator == ( const finalizer_authority& lhs, const finalizer_authority& rhs ) {
         return tie( lhs.description, lhs.fweight, lhs.public_key ) == tie( rhs.description, rhs.fweight, rhs.public_key );
      }
      friend bool operator != ( const finalizer_authority& lhs, const finalizer_authority& rhs ) {
         return !(lhs == rhs);
      }
   };

   struct finalizer_set {
      finalizer_set() = default;

      finalizer_set( uint32_t version, uint64_t fthreshold, std::initializer_list<finalizer_authority> finalizers )
      :version(version)
      ,fthreshold(fthreshold)
      ,finalizers(finalizers)
      {}

      uint32_t                                       version = 0; ///< sequentially incrementing version number
      uint64_t                                       fthreshold = 0;  // vote fweight threshold to finalize blocks
      vector<finalizer_authority>                    finalizers; // Instant Finality voter set

      friend bool operator == ( const finalizer_set& a, const finalizer_set& b )
      {
         if( a.version != b.version ) return false;
         if( a.fthreshold != b.fthreshold ) return false;
         if ( a.finalizers.size() != b.finalizers.size() ) return false;
         for( uint32_t i = 0; i < a.finalizers.size(); ++i )
            if( ! (a.finalizers[i] == b.finalizers[i]) ) return false;
         return true;
      }

      friend bool operator != ( const finalizer_set& a, const finalizer_set& b )
      {
         return !(a==b);
      }
   };

} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_authority, (description)(fweight)(public_key) )
FC_REFLECT( eosio::chain::finalizer_set, (version)(fthreshold)(finalizers) )
