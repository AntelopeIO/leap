#include <fc/crypto/modular_arithmetic.hpp>

#include <random>

#include <benchmark.hpp>

namespace benchmark {

void modexp_benchmarking() {
   std::mt19937 r(0x11223344);

   auto generate_random_bytes = [](std::mt19937& rand_eng, unsigned int num_bytes) {
      std::vector<char> result(num_bytes);

      uint_fast32_t v = 0;
      for(int byte_pos = 0, end = result.size(); byte_pos < end; ++byte_pos) {
          if ((byte_pos & 0x03) == 0) { // if divisible by 4
              v = rand_eng();
          }
          result[byte_pos] = v & 0xFF;
          v >>= 8;
      }

      return result;
   };

   static constexpr unsigned int start_num_bytes = 128; // 64
   static constexpr unsigned int end_num_bytes   = 256; // 512
   static constexpr unsigned int delta_num_bytes = 128; // 64

   static_assert(start_num_bytes <= end_num_bytes);
   static_assert(delta_num_bytes > 0);
   static_assert((end_num_bytes - start_num_bytes) % delta_num_bytes == 0);

   for (unsigned int n = start_num_bytes, slot = 0; n <= end_num_bytes; n += delta_num_bytes, ++slot) {
      auto base     = generate_random_bytes(r, n);
      auto exponent = generate_random_bytes(r, n);
      auto modulus  = generate_random_bytes(r, n);

      auto f = [&]() {
         fc::modexp(base, exponent, modulus);
      };

      benchmarking(std::to_string(n*8) + " bit width", f);
   }

   // Running the above benchmark (using commented values for num_trials and *_num_bytes) with a release build on an AMD 3.4 GHz CPU
   // provides average durations for executing mod_exp for increasing bit sizes for the value.

   // For example: with 512-bit values, the average duration is approximately 40 microseconds; with 1024-bit values, the average duration
   // is approximately 260 microseconds; with 2048-bit values, the average duration is approximately 2 milliseconds; and, with 4096-bit 
   // values, the average duration is approximately 14 milliseconds.

   // It appears that a model of the average time that scales quadratically with the bit size fits the empirically generated data well.
   // TODO: See if theoretical analysis of the modular exponentiation algorithm also justifies quadratic scaling.
}

} // benchmark
