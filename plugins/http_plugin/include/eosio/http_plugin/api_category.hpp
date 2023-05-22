#pragma once
#include <eosio/chain/types.hpp>
#include <stdint.h>
namespace eosio {

enum class api_category : uint32_t {
   unknown      = 0,
   chain_ro     = 1 << 0,
   chain_rw     = 1 << 1,
   db_size      = 1 << 2,
   net_ro       = 1 << 3,
   net_rw       = 1 << 4,
   producer_ro  = 1 << 5,
   producer_rw  = 1 << 6,
   snapshot     = 1 << 7,
   trace_api    = 1 << 8,
   prometheus   = 1 << 9,
   test_control = 1 << 10,
   node        = UINT32_MAX
};

class api_category_set {
   uint32_t data = {};
public:
   constexpr api_category_set() = default;
   constexpr explicit api_category_set(api_category c) : data(static_cast<uint32_t>(c)){}
   constexpr api_category_set(std::initializer_list<api_category> l) {
      for (auto c: l)
         insert(c);
   }
   constexpr bool contains(api_category category) const { 
      return eosio::chain::has_field(data, category);
   }
   constexpr void insert(api_category category) { 
      data = eosio::chain::set_field(data, category, true);
   }

   constexpr static api_category_set all() {
      return api_category_set(api_category::node);
   }

   constexpr bool operator == (const api_category_set& other) const {
      return data == other.data;
   }
};

}