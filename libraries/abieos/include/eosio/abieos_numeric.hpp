// copyright defined in abieos/LICENSE.txt

#pragma once

#include <stdint.h>
#include <algorithm>
#include <array>
#include <eosio/from_json.hpp>
#include <string>
#include <string_view>

#include "abieos_ripemd160.hpp"

#define ABIEOS_NODISCARD [[nodiscard]]

namespace abieos
{
   template <typename State>
   ABIEOS_NODISCARD bool set_error(State& state, std::string error)
   {
      state.error = std::move(error);
      return false;
   }

   ABIEOS_NODISCARD inline bool set_error(std::string& state, std::string error)
   {
      state = std::move(error);
      return false;
   }

   inline constexpr char base58_chars[] =
       "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

   inline constexpr auto create_base58_map()
   {
      std::array<int8_t, 256> base58_map{{0}};
      for (unsigned i = 0; i < base58_map.size(); ++i)
         base58_map[i] = -1;
      for (unsigned i = 0; i < sizeof(base58_chars); ++i)
         base58_map[base58_chars[i]] = i;
      return base58_map;
   }

   inline constexpr auto base58_map = create_base58_map();

   template <auto size>
   bool is_negative(const std::array<uint8_t, size>& a)
   {
      return a[size - 1] & 0x80;
   }

   template <auto size>
   void negate(std::array<uint8_t, size>& a)
   {
      uint8_t carry = 1;
      for (auto& byte : a)
      {
         int x = uint8_t(~byte) + carry;
         byte = x;
         carry = x >> 8;
      }
   }

   template <auto size>
   inline void decimal_to_binary(std::array<uint8_t, size>& result, std::string_view s)
   {
      memset(result.begin(), 0, result.size());
      for (auto& src_digit : s)
      {
         eosio::check(!(src_digit < '0' || src_digit > '9'),
                      eosio::convert_json_error(eosio::from_json_error::expected_int));
         uint8_t carry = src_digit - '0';
         for (auto& result_byte : result)
         {
            int x = result_byte * 10 + carry;
            result_byte = x;
            carry = x >> 8;
         }
         eosio::check(!carry,
                      eosio::convert_json_error(eosio::from_json_error::number_out_of_range));
      }
   }

   template <auto size>
   std::string binary_to_decimal(const std::array<uint8_t, size>& bin)
   {
      std::string result("0");
      for (auto byte_it = bin.rbegin(); byte_it != bin.rend(); ++byte_it)
      {
         int carry = *byte_it;
         for (auto& result_digit : result)
         {
            int x = ((result_digit - '0') << 8) + carry;
            result_digit = '0' + x % 10;
            carry = x / 10;
         }
         while (carry)
         {
            result.push_back('0' + carry % 10);
            carry = carry / 10;
         }
      }
      std::reverse(result.begin(), result.end());
      return result;
   }

}  // namespace abieos
