#include <fc/crypto/blake2.hpp>

#include <benchmark.hpp>

namespace eosio::benchmark {

void blake2_benchmarking() {
   uint32_t _rounds    = 0x0C;
   bytes    _h         = to_bytes( "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b");
   bytes    _m         = to_bytes("6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
   bytes    _t0_offset = to_bytes("0300000000000000");
   bytes    _t1_offset = to_bytes("0000000000000000");
   bool     _f         = false;

   auto blake2_f = [&]() {
      fc::blake2b(_rounds, _h, _m, _t0_offset, _t1_offset, _f, [](){});;
   };
   benchmarking("blake2", blake2_f);
}

} // benchmark
