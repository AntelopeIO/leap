#include <iostream>
#include <bn256.h>
#include <benchmark.hpp>

namespace benchmark {

   void benchmark_bn_256_g1() {
      const auto x = int512_t("17482507071749278111455282041915610272829864719113987536544577255487650163890");
      const auto expected = std::string("bn256.g1(1c2ff934f0d17ab82faefc7c8bd7056968647b89cb7a631db9180fedff70d6da, 221da2562039cd3f7d7c529c64433101b347dc8a2570519875679db212bb857)");
      auto f = [&]() {
         bn256::g1 g1{};
         auto result = g1.scalar_base_mult(x);
         if (result.string() != expected) {
            std::cout << "bn_256_g1: " << result.string() << " != " << expected << std::endl;
         }
      };

      benchmarking("bn_256_g1", f);
   }

   void benchmark_bn_256_g2() {
      const auto x = int512_t("14506523411943850241455301787384885005987154472366374992538170185465884650319");
      const auto expected = std::string("bn256.g2((0208e3703108a05763cd453f14f48b1b37dbb5031888967a2b6d2cbddd5c8aa2, 0c6c15af999be25e54bca027e85dc18f99ba10cb5bb4a7f8446bca966b1b0072), (1970779c7a0fb7d759eb35e44c540d9591e8031376b0a58d56b7f152a2f42867, 031a24f5f2acaed52c4211a1216ca6c4bd286fd2a5ce2f98e58cc1736d74b3aa))");
      auto f = [&]() {
         bn256::g2 g2{};
         auto result = g2.scalar_base_mult(x);
         if (result.string() != expected) {
            std::cout << "bn_256_g2: " << result.string() << " != " << expected << std::endl;
         }
      };

      benchmarking("bn_256_g2", f);
   }

   void benchmark_bn_256_pair() {
      const auto expected = std::string("bn256.gt(((2b8f1d5dfd20c55b67e42a0bc2ced9723511ed44fd0d8598d34bab373157aa84, 2a2341538eaee95cda7903229312ca0f5091cc0581334e54988ae2485b36cf53), (210e437dfc43d951de7e5aa2181f138e7b0591d3d080da677002907c28ebfe11, 1ffef4581607fc376a90a35fa03dfaa52fdd826b796e0f358975b68a2bab1f9c), (1614817a84c162919a9983c82e401a9fb17540bd2a9e5adb9458abcb56d24998, 13495c08e5d415c5746d9990cb12b27e4c4c9fe1cadefa951bb0ce0def1b82a1)),((0cbea85ee0b236cc644e4dcf1f01ff0ace0394312bceeb55f16c96d081754cdb, 197cda6cc3e206360df2027bf1de17a7c799dc487a0b27535cf9cc917da86724), (1b158f3c2fae9b18ae0b22c0bbb0f602ddf5bc7b7ffb5ac0172d1f257a4d598e, 076ea6f18435864aa2ff062a4a77e736465f6072d4023bf42306e4312363b991), (1832226987c434fc14d585f1a45ba647d618018ea58e4add2e02a64acbd60549, 12adf27ccb29382a5ef208445f5f6f3723a59ac167bcf363c556f62b2a98671d)))");
      bn256::g1 g1{bn256::curve_gen};
      bn256::g2 g2{bn256::twist_gen};
      auto f = [&]() {
         auto result = bn256::pair(g1, g2);
         if (result.string() != expected) {
            std::cout << "bn_256_pair: " << result.string() << " != " << expected << std::endl;
         }
      };

      benchmarking("bn_256_pair", f);
   }

   void bn_256_benchmarking() {
      benchmark_bn_256_g1();
      benchmark_bn_256_g2();
      benchmark_bn_256_pair();
   }
}