#include <eosio/chain/authority.hpp>

namespace fc {
   void to_variant(const eosio::chain::shared_public_key& var, fc::variant& vo, const fc::yield_function_t& yield) {
      vo = var.to_string(yield);
   }
} // namespace fc
