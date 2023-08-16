#pragma once

#include <eosio/eosio.hpp>

using bls_scalar = uint8_t[32];
using bls_fp = uint8_t[48];
using bls_fp2 = bls_fp[2];
using bls_g1 = uint8_t[144];
using bls_g2 = uint8_t[288];
using bls_gt = uint8_t[576];

namespace eosio {
    namespace internal_use_do_not_use {
        extern "C" {
            __attribute__((eosio_wasm_import))
            int32_t bls_g1_add(const char* op1, uint32_t op1_len, const char* op2, uint32_t op2_len, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_g2_add(const char* op1, uint32_t op1_len, const char* op2, uint32_t op2_len, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_g1_mul(const char* point, uint32_t point_len, const char* scalar, uint32_t scalar_len, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_g2_mul(const char* point, uint32_t point_len, const char* scalar, uint32_t scalar_len, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_g1_exp(const char* points, uint32_t points_len, const char* scalars, uint32_t scalars_len, uint32_t n, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_g2_exp(const char* points, uint32_t points_len, const char* scalars, uint32_t scalars_len, uint32_t n, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_pairing(const char* g1_points, uint32_t g1_points_len, const char* g2_points, uint32_t g2_points_len, uint32_t n, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_g1_map(const char* e, uint32_t e_len, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_g2_map(const char* e, uint32_t e_len, char* res, uint32_t res_len);

            __attribute__((eosio_wasm_import))
            int32_t bls_fp_mod(const char* s, uint32_t s_len, char* res, uint32_t res_len);
        }
    }
}

class [[eosio::contract]] bls_primitives_test : public eosio::contract {
public:
   using eosio::contract::contract;

   [[eosio::action]]
   void testg1add(const std::vector<uint8_t>& op1, const std::vector<uint8_t>& op2, const std::vector<uint8_t>& res, int32_t expected_error);

   [[eosio::action]]
   void testg2add(const std::vector<uint8_t>& op1, const std::vector<uint8_t>& op2, const std::vector<uint8_t>& res, int32_t expected_error);

   [[eosio::action]]
   void testg1mul(const std::vector<uint8_t>& point, const std::vector<uint8_t>& scalar, const std::vector<uint8_t>& res, int32_t expected_error);

   [[eosio::action]]
   void testg2mul(const std::vector<uint8_t>& point, const std::vector<uint8_t>& scalar, const std::vector<uint8_t>& res, int32_t expected_error);

   [[eosio::action]]
   void testg1exp(const std::vector<uint8_t>& points, const std::vector<uint8_t>& scalars, const uint32_t num, const std::vector<uint8_t>& res, int32_t expected_error);

   [[eosio::action]]
   void testg2exp(const std::vector<uint8_t>& points, const std::vector<uint8_t>& scalars, const uint32_t num, const std::vector<uint8_t>& res, int32_t expected_error);

   [[eosio::action]]
   void testpairing(const std::vector<uint8_t>& g1_points, const std::vector<uint8_t>& g2_points, const uint32_t num, const std::vector<uint8_t>& res, int32_t expected_error);

   [[eosio::action]]
   void testg1map(const std::vector<uint8_t>& e, const std::vector<uint8_t>& res, int32_t expected_error);
         
   [[eosio::action]]
   void testg2map(const std::vector<uint8_t>& e, const std::vector<uint8_t>& res, int32_t expected_error);
};
