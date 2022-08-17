#include <fc/crypto/hex.hpp>
#include <fc/crypto/sha3.hpp>
#include <fc/utility.hpp>

#include <benchmark.hpp>

using namespace fc;

namespace benchmark {

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

   auto hash_small_msg = [&]() {
      fc::sha3::hash(small_message, true);
   };
   benchmarking("sha3-256 (" + std::to_string(small_message.length()) + " bytes)", hash_small_msg);

   auto hash_large_msg = [&]() {
      fc::sha3::hash(large_message, true);
   };
   benchmarking("sha3-256 (" + std::to_string(large_message.length()) + " bytes)", hash_large_msg);

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
