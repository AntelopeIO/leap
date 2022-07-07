#pragma once

#include <eosio/eosio.hpp>

using bytes = std::vector<char>;

namespace eosio {
   namespace internal_use_do_not_use {
    extern "C" {
      __attribute__((eosio_wasm_import)) 
      int32_t alt_bn128_add( const char* op1_data, uint32_t op1_length,
                           const char* op2_data, uint32_t op2_length, 
                           char* result , uint32_t result_length);

      __attribute__((eosio_wasm_import)) 
      int32_t alt_bn128_mul( const char* op1_data, uint32_t op1_length,
                           const char* op2_data, uint32_t op2_length, 
                           char* result , uint32_t result_length);

      __attribute__((eosio_wasm_import)) 
      int32_t alt_bn128_pair( const char* op1_data, uint32_t op1_length);

      __attribute__((eosio_wasm_import)) 
      int32_t mod_exp(const char* base_data, uint32_t base_length,
                      const char* exp_data, uint32_t exp_length,
                      const char* mod_data, uint32_t mod_length,
                      char* result, uint32_t result_length);      

      __attribute__((eosio_wasm_import))
      int32_t blake2_f( uint32_t rounds, 
                        const char* state, uint32_t len_state,
                        const char* message, uint32_t len_message, 
                        const char* t0_offset, uint32_t len_t0_offset, 
                        const char* t1_offset, uint32_t len_t1_offset, 
                        int32_t final, 
                        char* result, uint32_t len_result);

      __attribute__((eosio_wasm_import))
      void sha3( const char* input_data, uint32_t input_length,
                 char* output_data, uint32_t output_length, int32_t keccak);

      __attribute__((eosio_wasm_import))
      int32_t k1_recover( const char* signature_data, uint32_t signature_length,
                         const char* digest_data, uint32_t digest_length,
                         char* output_data, uint32_t output_length);
      }
   }
}

class [[eosio::contract]] crypto_primitives_test : public eosio::contract {
public:
   using eosio::contract::contract;

   [[eosio::action]]
   void testadd(const bytes& op1, const bytes& op2, int32_t expected_error, const bytes& expected_result);

   [[eosio::action]]
   void testmul(const bytes& point, const bytes& scalar, int32_t expected_error, const bytes& expected_result);

   [[eosio::action]]
   void testpair(const bytes& g1_g2_pairs, int32_t expected_error);

   [[eosio::action]]
   void testmodexp(const bytes& base, const bytes& exp, const bytes& modulo, int32_t expected_error, const bytes& expected_result);
         
   [[eosio::action]]
   void testblake2f(uint32_t rounds, const bytes& state, const bytes& message, const bytes& t0, const bytes& t1, bool final, int32_t expected_error, const bytes& expected_result);
   
   [[eosio::action]]
   void testsha3(const bytes& input, const bytes& expected_result);

   [[eosio::action]]
   void testkeccak(const bytes& input, const bytes& expected_result);

   [[eosio::action]]
   void testecrec(const bytes& signature, const bytes& digest, int32_t expected_error, const bytes& expected_result);
};
