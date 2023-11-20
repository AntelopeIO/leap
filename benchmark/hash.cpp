#include <fc/crypto/hex.hpp>
#include <fc/crypto/sha1.hpp>
#include <fc/crypto/sha3.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/sha512.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/utility.hpp>

#include <benchmark.hpp>

using namespace fc;

namespace eosio::benchmark {

void hash_benchmarking() {
   std::string small_message = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ01";

   // build a large message
   constexpr auto large_msg_size = 4096;
   std::string large_message;
   large_message.reserve(large_msg_size);
   uint32_t num_concats = large_msg_size/small_message.length();
   for (uint32_t i = 0; i < num_concats; ++i) {
      large_message += small_message;
   }

   auto sha1_small_msg = [&]() {
      fc::sha1::hash(small_message);
   };
   benchmarking("sha1 (" + std::to_string(small_message.length()) + " bytes)", sha1_small_msg);

   auto sha1_large_msg = [&]() {
      fc::sha1::hash(large_message);
   };
   benchmarking("sha1 (" + std::to_string(large_message.length()) + " bytes)", sha1_large_msg);

   auto sha256_small_msg = [&]() {
      fc::sha256::hash(small_message);
   };
   benchmarking("sha256 (" + std::to_string(small_message.length()) + " bytes)", sha256_small_msg);

   auto sha256_large_msg = [&]() {
      fc::sha256::hash(large_message);
   };
   benchmarking("sha256 (" + std::to_string(large_message.length()) + " bytes)", sha256_large_msg);

   auto sha512_small_msg = [&]() {
      fc::sha512::hash(small_message);
   };
   benchmarking("sha512 (" + std::to_string(small_message.length()) + " bytes)", sha512_small_msg);

   auto sha512_large_msg = [&]() {
      fc::sha512::hash(large_message);
   };
   benchmarking("sha512 (" + std::to_string(large_message.length()) + " bytes)", sha512_large_msg);

   auto ripemd160_small_msg = [&]() {
      fc::ripemd160::hash(small_message);
   };
   benchmarking("ripemd160 (" + std::to_string(small_message.length()) + " bytes)", ripemd160_small_msg);

   auto ripemd160_large_msg = [&]() {
      fc::ripemd160::hash(large_message);
   };
   benchmarking("ripemd160 (" + std::to_string(large_message.length()) + " bytes)", ripemd160_large_msg);

   auto sha3_small_msg = [&]() {
      fc::sha3::hash(small_message, true);
   };
   benchmarking("sha3-256 (" + std::to_string(small_message.length()) + " bytes)", sha3_small_msg);

   auto sha3_large_msg = [&]() {
      fc::sha3::hash(large_message, true);
   };
   benchmarking("sha3-256 (" + std::to_string(large_message.length()) + " bytes)", sha3_large_msg);

   auto keccak_small_msg = [&]() {
      fc::sha3::hash(small_message, false);
   };
   benchmarking("keccak256 (" + std::to_string(small_message.length()) + " bytes)", keccak_small_msg);

   auto keccak_large_msg = [&]() {
      fc::sha3::hash(large_message, false);
   };
   benchmarking("keccak256 (" + std::to_string(large_message.length()) + " bytes)", keccak_large_msg);

}

} // benchmark
