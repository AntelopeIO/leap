#pragma once

#include <cstdint>
#include <vector>

//contracts that need to be available by native contract unit testing's libtester need to be embedded
// in to the library as the build directory may not exist after being 'make install'ed.
#define MAKE_EMBD_WASM_ABI(CN)                                                              \
   static std::vector<std::uint8_t> CN ## _wasm();                                          \
   static std::vector<char> CN ## _abi();

namespace eosio {
   namespace testing {
      struct contracts {
         // Contracts in `libraries/testing/contracts' directory
         MAKE_EMBD_WASM_ABI(eosio_bios)

         MAKE_EMBD_WASM_ABI(before_producer_authority_eosio_bios)
         MAKE_EMBD_WASM_ABI(before_preactivate_eosio_bios)
      };
   } /// eosio::testing
}  /// eosio
