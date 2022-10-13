#include <iostream>
#include <bn256.h>
#include <benchmark.hpp>

namespace benchmark {

   void benchmark_bn_256_g1() {
      const auto x = int512_t("17482507071749278111455282041915610272829864719113987536544577255487650163890");
      auto f = [&]() {
         bn256::g1 g1{};
         g1.scalar_base_mult(x);
      };

      benchmarking("bn_256_g1", f);
   }

   void benchmark_bn_256_g2() {
      const auto x = int512_t("14506523411943850241455301787384885005987154472366374992538170185465884650319");
      auto f = [&]() {
         bn256::g2 g2{};
         g2.scalar_base_mult(x);
      };

      benchmarking("bn_256_g2", f);
   }

   void benchmark_bn_256_pair() {
      bn256::g1 g1{bn256::g1::curve_gen};
      bn256::g2 g2{bn256::g2::twist_gen};
      auto f = [&]() {
         bn256::pair(g1, g2);
      };

      benchmarking("bn_256_pair", f);
   }

   void bn_256_benchmarking() {
      benchmark_bn_256_g1();
      benchmark_bn_256_g2();
      benchmark_bn_256_pair();
   }
}