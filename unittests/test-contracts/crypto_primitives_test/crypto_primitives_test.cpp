#include "crypto_primitives_test.hpp"
#include <eosio/transaction.hpp>

using namespace eosio;
void crypto_primitives_test::testadd(const bytes& op1, const bytes& op2, int32_t expected_error, const bytes& expected_result) {
   bytes ret(64, '\0');
   int32_t errorCode = eosio::internal_use_do_not_use::alt_bn128_add( op1.data(), op1.size(), op2.data(), op2.size(), (char*)ret.data(), 64);
   check( errorCode == expected_error, "Error does not match");
   check( ret == expected_result, "Result does not match");
}

[[eosio::action]]
void crypto_primitives_test::testmul(const bytes& point, const bytes& scalar, int32_t expected_error, const bytes& expected_result) {
   bytes ret(64, '\0');
   int32_t errorCode = eosio::internal_use_do_not_use::alt_bn128_mul( point.data(), point.size(), scalar.data(), scalar.size(), (char*)ret.data(), 64);
   check( errorCode == expected_error, "Error does not match");
   check( ret == expected_result, "Result does not match");
}

[[eosio::action]]
void crypto_primitives_test::testpair(const bytes& g1_g2_pairs, int32_t expected) {
   int32_t  res = eosio::internal_use_do_not_use::alt_bn128_pair( g1_g2_pairs.data(), g1_g2_pairs.size());

   eosio::print("alt_bn128_pair: ", uint64_t(res));

   check( res == expected, "Result does not match expected");
}

[[eosio::action]]
void crypto_primitives_test::testmodexp(const bytes& base, const bytes& exp, const bytes& modulo, int32_t expected_error, const bytes& expected_result) {
   bytes ret(modulo.size(), '\0');
   int32_t errorCode = eosio::internal_use_do_not_use::mod_exp( base.data(), base.size(), exp.data(), exp.size(), modulo.data(), modulo.size(), ret.data(), ret.size());
   check( errorCode == expected_error, "Error does not match");
   check( ret == expected_result, "Result does not match");
}

[[eosio::action]]
void crypto_primitives_test::testblake2f( uint32_t rounds, const bytes& state, const bytes& message, const bytes& t0, const bytes& t1, bool final, int32_t expected_error, const bytes& expected_result) {
   bytes ret(64);
   int32_t errorCode = eosio::internal_use_do_not_use::blake2_f( rounds, 
                                             state.data(), state.size(),
                                             message.data(), message.size(),
                                             t0.data(), t0.size(),
                                             t1.data(), t1.size(), 
                                             final,
                                             (char*)ret.data(), 64);
   check( errorCode == expected_error, "Error does not match");
   check( ret == expected_result, "Result does not match");
}

[[eosio::action]]
void crypto_primitives_test::testsha3(const bytes& input, const bytes& expected_result) {
   bytes ret(32);
   eosio::internal_use_do_not_use::sha3(input.data(), input.size(), (char*)ret.data(), ret.size(), false);
   check( ret == expected_result , "result does not match");
}

[[eosio::action]]
void crypto_primitives_test::testkeccak(const bytes& input, const bytes& expected_result) {
   bytes ret(32);
   eosio::internal_use_do_not_use::sha3(input.data(), input.size(), (char*)ret.data(), ret.size(), true);
   check( ret == expected_result , "result does not match");
}

[[eosio::action]]
void crypto_primitives_test::testecrec(const bytes& signature, const bytes& digest, int32_t expected_error, const bytes& expected_result) {
   bytes ret(65);
   int32_t errorCode = eosio::internal_use_do_not_use::k1_recover(signature.data(), signature.size(), digest.data(), digest.size(), (char*)ret.data(), ret.size());
   check( errorCode == expected_error, "Error does not match");
   check( ret == expected_result, "Result does not match");
}
