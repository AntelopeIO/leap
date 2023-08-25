#pragma once

#include <eosio/chain/config.hpp>
#include <eosio/chain/types.hpp>
#include <chainbase/chainbase.hpp>
#include <eosio/chain/authority.hpp>
#include <eosio/chain/snapshot.hpp>

#include <fc/crypto/bls_public_key.hpp>

namespace eosio::chain {

   struct shared_finalizer_authority {
      shared_finalizer_authority() = delete;
      shared_finalizer_authority( const shared_finalizer_authority& ) = default;
      shared_finalizer_authority( shared_finalizer_authority&& ) = default;
      shared_finalizer_authority& operator= ( shared_finalizer_authority && ) = default;
      shared_finalizer_authority& operator= ( const shared_finalizer_authority & ) = default;

      shared_finalizer_authority( const std::string& description, const uint64_t fweight, const fc::crypto::blslib::bls_public_key& public_key )
      :description(description)
      ,fweight(fweight)
      ,public_key(public_key)
      {}

      std::string      description;
      uint64_t         fweight;
      fc::crypto::blslib::bls_public_key   public_key;
   };

   struct shared_finalizer_set {
      shared_finalizer_set() = delete;

      explicit shared_finalizer_set( chainbase::allocator<char> alloc )
      :finalizers(alloc){}

      shared_finalizer_set( const shared_finalizer_set& ) = default;
      shared_finalizer_set( shared_finalizer_set&& ) = default;
      shared_finalizer_set& operator= ( shared_finalizer_set && ) = default;
      shared_finalizer_set& operator= ( const shared_finalizer_set & ) = default;

      uint32_t                                   version = 0; ///< sequentially incrementing version number
      uint64_t                                   fthreshold = 0; // minimum finalizer fweight sum for block finalization
      shared_vector<shared_finalizer_authority>  finalizers;
   };

   struct finalizer_authority {

      std::string  description;
      uint64_t     fweight; // weight that this finalizer's vote has for meeting fthreshold
      fc::crypto::blslib::bls_public_key  public_key;

      auto to_shared(chainbase::allocator<char> alloc) const {
         return shared_finalizer_authority(description, fweight, public_key);
      }

      static auto from_shared( const shared_finalizer_authority& src ) {
         finalizer_authority result;
         result.description = src.description;
         result.fweight = src.fweight;
         result.public_key = src.public_key;
         return result;
      }

      /**
       * ABI's for contracts expect variants to be serialized as a 2 entry array of
       * [type-name, value].
       *
       * This is incompatible with standard FC rules for
       * static_variants which produce
       *
       * [ordinal, value]
       *
       * this method produces an appropriate variant for contracts where the authority field
       * is correctly formatted
       */
      fc::variant get_abi_variant() const;

      friend bool operator == ( const finalizer_authority& lhs, const finalizer_authority& rhs ) {
         return tie( lhs.description, lhs.fweight, lhs.public_key ) == tie( rhs.description, rhs.fweight, rhs.public_key );
      }
      friend bool operator != ( const finalizer_authority& lhs, const finalizer_authority& rhs ) {
         return tie( lhs.description, lhs.fweight, lhs.public_key ) != tie( rhs.description, rhs.fweight, rhs.public_key );
      }
   };

   struct finalizer_set {
      finalizer_set() = default;

      finalizer_set( uint32_t version, uint64_t fthreshold, std::initializer_list<finalizer_authority> finalizers )
      :version(version)
      ,fthreshold(fthreshold)
      ,finalizers(finalizers)
      {}

      auto to_shared(chainbase::allocator<char> alloc) const {
         auto result = shared_finalizer_set(alloc);
         result.version = version;
         result.fthreshold = fthreshold;
         result.finalizers.clear();
         result.finalizers.reserve( finalizers.size() );
         for( const auto& f : finalizers ) {
            result.finalizers.emplace_back(f.to_shared(alloc));
         }
         return result;
      }

      static auto from_shared( const shared_finalizer_set& src ) {
         finalizer_set result;
         result.version = src.version;
         result.fthreshold = src.fthreshold;
         result.finalizers.reserve( src.finalizers.size() );
         for( const auto& f : src.finalizers ) {
            result.finalizers.emplace_back(finalizer_authority::from_shared(f));
         }
         return result;
      }

      uint32_t                                       version = 0; ///< sequentially incrementing version number
      uint64_t                                       fthreshold;  // vote fweight threshold to finalize blocks
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
FC_REFLECT( eosio::chain::shared_finalizer_authority, (description)(fweight)(public_key) )
FC_REFLECT( eosio::chain::shared_finalizer_set, (version)(fthreshold)(finalizers) )
