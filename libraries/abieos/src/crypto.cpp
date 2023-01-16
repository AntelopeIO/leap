#include "../include/eosio/crypto.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "../include/eosio/from_bin.hpp"
#include "../include/eosio/from_json.hpp"
#include "../include/eosio/to_bin.hpp"
#include "../include/eosio/to_json.hpp"

#include "eosio/abieos_ripemd160.hpp"

using namespace eosio;

namespace
{
   enum key_type : uint8_t
   {
      k1 = 0,
      r1 = 1,
      wa = 2,
   };

   constexpr char base58_chars[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

   constexpr auto create_base58_map()
   {
      std::array<int8_t, 256> base58_map{{0}};
      for (unsigned i = 0; i < base58_map.size(); ++i)
         base58_map[i] = -1;
      for (unsigned i = 0; i < sizeof(base58_chars); ++i)
         base58_map[base58_chars[i]] = i;
      return base58_map;
   }

   constexpr auto base58_map = create_base58_map();

   template <typename Container>
   void base58_to_binary(Container& result, std::string_view s)
   {
      std::size_t offset = result.size();
      for (auto& src_digit : s)
      {
         int carry = base58_map[static_cast<uint8_t>(src_digit)];
         check(carry >= 0, ::eosio::convert_json_error(::eosio::from_json_error::expected_key));
         for (std::size_t i = offset; i < result.size(); ++i)
         {
            auto& result_byte = result[i];
            int x = static_cast<uint8_t>(result_byte) * 58 + carry;
            result_byte = x;
            carry = x >> 8;
         }
         if (carry)
            result.push_back(static_cast<uint8_t>(carry));
      }
      for (auto& src_digit : s)
         if (src_digit == '1')
            result.push_back(0);
         else
            break;
      std::reverse(result.begin() + offset, result.end());
   }

   template <typename Container>
   std::string binary_to_base58(const Container& bin)
   {
      std::string result("");
      for (auto byte : bin)
      {
         static_assert(sizeof(byte) == 1);
         int carry = static_cast<uint8_t>(byte);
         for (auto& result_digit : result)
         {
            int x = (base58_map[result_digit] << 8) + carry;
            result_digit = base58_chars[x % 58];
            carry = x / 58;
         }
         while (carry)
         {
            result.push_back(base58_chars[carry % 58]);
            carry = carry / 58;
         }
      }
      for (auto byte : bin)
         if (byte)
            break;
         else
            result.push_back('1');
      std::reverse(result.begin(), result.end());
      return result;
   }

   template <typename... Container>
   std::array<unsigned char, 20> digest_suffix_ripemd160(const Container&... data)
   {
      std::array<unsigned char, 20> digest;
      abieos_ripemd160::ripemd160_state self;
      abieos_ripemd160::ripemd160_init(&self);
      (abieos_ripemd160::ripemd160_update(&self, data.data(), data.size()), ...);
      check(abieos_ripemd160::ripemd160_digest(&self, digest.data()),
            convert_json_error(eosio::from_json_error::invalid_signature));
      return digest;
   }

   template <typename Key>
   Key string_to_key(std::string_view s, key_type type, std::string_view suffix)
   {
      std::vector<char> whole;
      whole.push_back(uint8_t{type});
      base58_to_binary(whole, s);
      check(whole.size() > 5, convert_json_error(eosio::from_json_error::expected_key));
      auto ripe_digest =
          digest_suffix_ripemd160(std::string_view(whole.data() + 1, whole.size() - 5), suffix);
      check(memcmp(ripe_digest.data(), whole.data() + whole.size() - 4, 4) == 0,
            convert_json_error(from_json_error::expected_key));
      whole.erase(whole.end() - 4, whole.end());
      return convert_from_bin<Key>(whole);
   }

   template <typename Key>
   std::string key_to_string(const Key& key, std::string_view suffix, const char* prefix)
   {
      auto whole = convert_to_bin(key);
      auto ripe_digest =
          digest_suffix_ripemd160(std::string_view(whole.data() + 1, whole.size() - 1), suffix);
      whole.insert(whole.end(), ripe_digest.data(), ripe_digest.data() + 4);
      return prefix + binary_to_base58(std::string_view(whole.data() + 1, whole.size() - 1));
   }
}  // namespace

std::string eosio::public_key_to_string(const public_key& key)
{
   if (key.index() == key_type::k1)
   {
      return key_to_string(key, "K1", "PUB_K1_");
   }
   else if (key.index() == key_type::r1)
   {
      return key_to_string(key, "R1", "PUB_R1_");
   }
   else if (key.index() == key_type::wa)
   {
      return key_to_string(key, "WA", "PUB_WA_");
   }
   else
   {
      check(false, convert_json_error(eosio::from_json_error::expected_public_key));
      __builtin_unreachable();
   }
}

public_key eosio::public_key_from_string(std::string_view s)
{
   public_key result;
   if (s.substr(0, 3) == "EOS")
   {
      return string_to_key<public_key>(s.substr(3), key_type::k1, "");
   }
   else if (s.substr(0, 7) == "PUB_K1_")
   {
      return string_to_key<public_key>(s.substr(7), key_type::k1, "K1");
   }
   else if (s.substr(0, 7) == "PUB_R1_")
   {
      return string_to_key<public_key>(s.substr(7), key_type::r1, "R1");
   }
   else if (s.substr(0, 7) == "PUB_WA_")
   {
      return string_to_key<public_key>(s.substr(7), key_type::wa, "WA");
   }
   else
   {
      check(false, convert_json_error(from_json_error::expected_public_key));
      __builtin_unreachable();
   }
}

std::string eosio::private_key_to_string(const private_key& private_key)
{
   if (private_key.index() == key_type::k1)
      return key_to_string(private_key, "K1", "PVT_K1_");
   else if (private_key.index() == key_type::r1)
      return key_to_string(private_key, "R1", "PVT_R1_");
   else
   {
      check(false, convert_json_error(from_json_error::expected_private_key));
      __builtin_unreachable();
   }
}

private_key eosio::private_key_from_string(std::string_view s)
{
   if (s.substr(0, 7) == "PVT_K1_")
      return string_to_key<private_key>(s.substr(7), key_type::k1, "K1");
   else if (s.substr(0, 7) == "PVT_R1_")
      return string_to_key<private_key>(s.substr(7), key_type::r1, "R1");
   else if (s.substr(0, 4) == "PVT_")
   {
      check(false, convert_json_error(from_json_error::expected_private_key));
      __builtin_unreachable();
   }
   else
   {
      std::vector<char> whole;
      base58_to_binary(whole, s);
      check(whole.size() >= 5, convert_json_error(from_json_error::expected_private_key));
      whole[0] = key_type::k1;
      whole.erase(whole.end() - 4, whole.end());
      return convert_from_bin<private_key>(whole);
   }
}

std::string eosio::signature_to_string(const eosio::signature& signature)
{
   if (signature.index() == key_type::k1)
      return key_to_string(signature, "K1", "SIG_K1_");
   else if (signature.index() == key_type::r1)
      return key_to_string(signature, "R1", "SIG_R1_");
   else if (signature.index() == key_type::wa)
      return key_to_string(signature, "WA", "SIG_WA_");
   else
   {
      check(false, convert_json_error(eosio::from_json_error::expected_signature));
      __builtin_unreachable();
   }
}

signature eosio::signature_from_string(std::string_view s)
{
   if (s.size() >= 7 && s.substr(0, 7) == "SIG_K1_")
      return string_to_key<signature>(s.substr(7), key_type::k1, "K1");
   else if (s.size() >= 7 && s.substr(0, 7) == "SIG_R1_")
      return string_to_key<signature>(s.substr(7), key_type::r1, "R1");
   else if (s.size() >= 7 && s.substr(0, 7) == "SIG_WA_")
      return string_to_key<signature>(s.substr(7), key_type::wa, "WA");
   else
   {
      check(false, convert_json_error(eosio::from_json_error::expected_signature));
      __builtin_unreachable();
   }
}

namespace eosio
{
   std::string to_base58(const uint8_t* d, size_t s)
   {
      return binary_to_base58(std::string_view((const char*)d, s));
   }

   std::vector<uint8_t> from_base58(const std::string_view& s)
   {
      std::vector<uint8_t> ret;
      base58_to_binary(ret, s);
      return ret;
   }
}  // namespace eosio
