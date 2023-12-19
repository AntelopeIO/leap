#include "eosio.bios.hpp"
#include <eosio/instant_finality.hpp>
#include <unordered_set>

namespace eosiobios {

void bios::setabi( name account, const std::vector<char>& abi ) {
   abi_hash_table table(get_self(), get_self().value);
   auto itr = table.find( account.value );
   if( itr == table.end() ) {
      table.emplace( account, [&]( auto& row ) {
         row.owner = account;
         row.hash  = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
      });
   } else {
      table.modify( itr, eosio::same_payer, [&]( auto& row ) {
         row.hash = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
      });
   }
}

void bios::setfinalizer( const finalizer_policy& finalizer_policy ) {
   // exensive checks are performed to make sure setfinalizer host function
   // will never fail

   require_auth( get_self() );

   check(finalizer_policy.finalizers.size() <= max_finalizers, "number of finalizers exceeds the maximum allowed");
   check(finalizer_policy.finalizers.size() > 0, "require at least one finalizer");

   eosio::abi_finalizer_policy abi_finalizer_policy;
   abi_finalizer_policy.fthreshold = finalizer_policy.threshold;
   abi_finalizer_policy.finalizers.reserve(finalizer_policy.finalizers.size());

   const std::string pk_prefix = "PUB_BLS";
   const std::string sig_prefix = "SIG_BLS";

   // use raw affine format (bls_g1 is std::array<char, 96>) for uniqueness check
   struct g1_hash {
      std::size_t operator()(const eosio::bls_g1& g1) const {
         std::hash<const char*> hash_func;
         return hash_func(g1.data());
      }
   };
   struct g1_equal {
      bool operator()(const eosio::bls_g1& lhs, const eosio::bls_g1& rhs) const {
         return std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
      }
   };
   std::unordered_set<eosio::bls_g1, g1_hash, g1_equal> unique_finalizer_keys;

   uint64_t weight_sum = 0;

   for (const auto& f: finalizer_policy.finalizers) {
      check(f.description.size() <= max_finalizer_description_size, "Finalizer description greater than max allowed size");

      // basic key format checks
      check(f.public_key.substr(0, pk_prefix.length()) == pk_prefix, "public key shoud start with PUB_BLS");
      check(f.pop.substr(0, sig_prefix.length()) == sig_prefix, "proof of possession signature should start with SIG_BLS");

      // check overflow
      check(std::numeric_limits<uint64_t>::max() - weight_sum >= f.weight, "sum of weights causes uint64_t overflow");
      weight_sum += f.weight;

      // decode_bls_public_key_to_g1 will fail ("check" function fails)
      // if the key is invalid
      const auto pk = eosio::decode_bls_public_key_to_g1(f.public_key);
      // duplicate key check
      check(unique_finalizer_keys.insert(pk).second, "duplicate public key");

      const auto signature = eosio::decode_bls_signature_to_g2(f.pop);

      // proof of possession of private key check
      check(eosio::bls_pop_verify(pk, signature), "proof of possession failed");

      std::vector<char> pk_vector(pk.begin(), pk.end());
      abi_finalizer_policy.finalizers.emplace_back(eosio::abi_finalizer_authority{f.description, f.weight, std::move(pk_vector)});
   }

   check(finalizer_policy.threshold > weight_sum / 2, "finalizer policy threshold must be greater than half of the sum of the weights");

   set_finalizers(std::move(abi_finalizer_policy));
}

void bios::onerror( ignore<uint128_t>, ignore<std::vector<char>> ) {
   check( false, "the onerror action cannot be called directly" );
}

void bios::setpriv( name account, uint8_t is_priv ) {
   require_auth( get_self() );
   set_privileged( account, is_priv );
}

void bios::setalimits( name account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight ) {
   require_auth( get_self() );
   set_resource_limits( account, ram_bytes, net_weight, cpu_weight );
}

void bios::setprods( const std::vector<eosio::producer_authority>& schedule ) {
   require_auth( get_self() );
   set_proposed_producers( schedule );
}

void bios::setparams( const eosio::blockchain_parameters& params ) {
   require_auth( get_self() );
   set_blockchain_parameters( params );
}

void bios::reqauth( name from ) {
   require_auth( from );
}

void bios::activate( const eosio::checksum256& feature_digest ) {
   require_auth( get_self() );
   preactivate_feature( feature_digest );
   print( "feature digest activated: ", feature_digest, "\n" );
}

void bios::reqactivated( const eosio::checksum256& feature_digest ) {
   check( is_feature_activated( feature_digest ), "protocol feature is not activated" );
}

}
