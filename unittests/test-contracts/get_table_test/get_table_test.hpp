/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#pragma once

#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>


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

using namespace eosio;

class [[eosio::contract]] get_table_test : public eosio::contract {
    public:
    using eosio::contract::contract;

    // Number object
    struct [[eosio::table]] numobj {
        uint64_t        key;
        uint64_t        sec64;
        uint128_t       sec128;
        double          secdouble;
        long double     secldouble;

        uint64_t primary_key() const { return key; }
        uint64_t sec64_key() const { return sec64; }
        uint128_t sec128_key() const { return sec128; }
        double secdouble_key() const { return secdouble; }
        long double secldouble_key() const { return secldouble; }
    };

    // Hash object
    struct [[eosio::table]] hashobj {
        uint64_t        key;
        std::string     hash_input;
        checksum256     sec256;
        checksum160     sec160;

        uint64_t primary_key() const { return key; }
        checksum256 sec256_key() const { return sec256; }
        checksum256 sec160_key() const { return checksum256(sec160.get_array()); }
    };

    typedef eosio::multi_index< "numobjs"_n, numobj, 
                                indexed_by<"bysec1"_n, const_mem_fun<numobj, uint64_t, &numobj::sec64_key>>,
                                indexed_by<"bysec2"_n, const_mem_fun<numobj, uint128_t, &numobj::sec128_key>>,
                                indexed_by<"bysec3"_n, const_mem_fun<numobj, double, &numobj::secdouble_key>>,
                                indexed_by<"bysec4"_n, const_mem_fun<numobj, long double, &numobj::secldouble_key>>
                                > numobjs;

    typedef eosio::multi_index< "hashobjs"_n, hashobj, 
                            indexed_by<"bysec1"_n, const_mem_fun<hashobj, checksum256, &hashobj::sec256_key>>,
                            indexed_by<"bysec2"_n, const_mem_fun<hashobj, checksum256, &hashobj::sec160_key>>
                            > hashobjs;

   [[eosio::action]]
   void addnumobj(uint64_t input);

   [[eosio::action]]
   void modifynumobj(uint64_t id);

   [[eosio::action]]
   void erasenumobj(uint64_t id);


   [[eosio::action]]
   void addhashobj(std::string hashinput);


};