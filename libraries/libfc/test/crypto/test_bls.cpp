#include <boost/test/unit_test.hpp>

#include <bls12-381/bls12-381.hpp>
#include <fc/exception/exception.hpp>

using namespace std;
using namespace bls12_381;

BOOST_AUTO_TEST_SUITE(bls)

// sample seeds
vector<uint8_t> seed_1 = { 0, 50, 6, 244, 24, 199, 1, 25, 52, 88, 192, 19, 18, 12, 89, 6, 220, 18, 102, 58, 209, 82, 12, 62, 89, 110, 182, 9, 44, 20, 254, 22};
vector<uint8_t> seed_2 = { 6, 51, 22, 89, 11, 15, 4, 61, 127, 241, 79, 26, 88, 52, 1, 6, 18, 79, 10, 8, 36, 182, 154, 35, 75, 156, 215, 41, 29, 90, 125, 233};
vector<uint8_t> seed_3 = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12};
// sample messages
vector<uint8_t> message_1 = { 51, 23, 56, 93, 212, 129, 128, 27, 251, 12, 42, 129, 210, 9, 34, 98};
vector<uint8_t> message_2 = { 16, 38, 54, 125, 71, 214, 217, 78, 73, 23, 127, 235, 8, 94, 41, 53};
vector<uint8_t> message_3 = { 12, 4, 1, 64, 127, 86, 2, 8, 145, 25, 27, 5, 88, 4, 42, 58};

// test a single key signature + verification
BOOST_AUTO_TEST_CASE(bls_sig_verify) try {
    array<uint64_t, 4> sk = secret_key(seed_1);
    g1 pk = public_key(sk);
    g2 signature = sign(sk, message_1);

    bool ok = verify(pk, message_1, signature);
    BOOST_CHECK_EQUAL(ok, true);
} FC_LOG_AND_RETHROW();

// test serialization / deserialization of private key, public key and signature
BOOST_AUTO_TEST_CASE(bls_serialization_test) try {
    array<uint64_t, 4> sk = secret_key(seed_1);
    g1 pk = public_key(sk);
    g2 signature = sign(sk, message_1);

    const array<uint8_t, 144> pk_string = pk.toJacobianBytesBE();
    const array<uint8_t, 288> signature_string = signature.toJacobianBytesBE();
    cout << bytesToHex<144>(pk_string) <<  std::endl;
    cout << bytesToHex<288>(signature_string) << std::endl;

    g1 pk2 = g1::fromJacobianBytesBE(pk_string).value();
    g2 signature2 = g2::fromJacobianBytesBE(signature_string).value();
    bool ok = verify(pk2, message_1, signature2);
    BOOST_CHECK_EQUAL(ok, true);
} FC_LOG_AND_RETHROW();

// test public keys + signatures aggregation + verification
BOOST_AUTO_TEST_CASE(bls_agg_sig_verify) try {
    array<uint64_t, 4> sk1 = secret_key(seed_1);
    g1 pk1 = public_key(sk1);
    g2 sig1 = sign(sk1, message_1);

    array<uint64_t, 4> sk2 = secret_key(seed_2);
    g1 pk2 = public_key(sk2);
    g2 sig2 = sign(sk2, message_2);

    g2 aggSig = aggregate_signatures(vector<g2> {sig1, sig2});

    bool ok = aggregate_verify(vector<g1>{pk1, pk2}, vector<vector<uint8_t>>{message_1, message_2}, aggSig);
    BOOST_CHECK_EQUAL(ok, true);
} FC_LOG_AND_RETHROW();

// test signature aggregation + aggregate tree verification
BOOST_AUTO_TEST_CASE(bls_agg_tree_verify) try {
    array<uint64_t, 4> sk1 = secret_key(seed_1);
    g1 pk1 = public_key(sk1);
    g2 sig1 = sign(sk1, message_1);

    array<uint64_t, 4> sk2 = secret_key(seed_2);
    g1 pk2 = public_key(sk2);
    g2 sig2 = sign(sk2, message_2);

    g2 aggSig = aggregate_signatures(vector<g2> {sig1, sig2});

    array<uint64_t, 4> sk3 = secret_key(seed_3);
    g1 pk3 = public_key(sk3);
    g2 sig3 = sign(sk3, message_3);

    g2 aggSigFinal = aggregate_signatures(vector<g2> {aggSig, sig3});

    bool ok = aggregate_verify(vector<g1>{pk1, pk2, pk3}, vector<vector<uint8_t>>{message_1, message_2, message_3}, aggSigFinal);
    BOOST_CHECK_EQUAL(ok, true);
} FC_LOG_AND_RETHROW();

// test public key aggregation
BOOST_AUTO_TEST_CASE(bls_agg_pk_verify) try {
    array<uint64_t, 4> sk1 = secret_key(seed_1);
    g1 pk1 = public_key(sk1);
    g2 sig1 = sign(sk1, message_1);

    array<uint64_t, 4> sk2 = secret_key(seed_2);
    g1 pk2 = public_key(sk2);
    g2 sig2 = sign(sk2, message_1);

    array<uint64_t, 4> sk3 = secret_key(seed_3);
    g1 pk3 = public_key(sk3);
    g2 sig3 = sign(sk3, message_1);

    g2 sigAgg = aggregate_signatures(vector<g2> {sig1, sig2, sig3});
    g1 pkAgg = aggregate_public_keys(vector<g1> {pk1, pk2, pk3});

    bool ok = verify(pkAgg, message_1, sigAgg);
    BOOST_CHECK_EQUAL(ok, true);
} FC_LOG_AND_RETHROW();

// test wrong key and wrong signature
BOOST_AUTO_TEST_CASE(bls_bad_sig_verify) try {
    array<uint64_t, 4> sk1 = secret_key(seed_1);
    g1 pk1 = public_key(sk1);
    g2 sig1 = sign(sk1, message_1);

    array<uint64_t, 4> sk2 = secret_key(seed_2);
    g1 pk2 = public_key(sk2);
    g2 sig2 = sign(sk2, message_1);

    bool ok1 = verify(pk1, message_1, sig2); // wrong signature
    bool ok2 = verify(pk2, message_1, sig1); // wrong key
    BOOST_CHECK_EQUAL(ok1, false);
    BOOST_CHECK_EQUAL(ok2, false);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_pop_verify) try {
    array<uint64_t, 4> sk1 = secret_key(seed_1);
    g1 pk1 = public_key(sk1);
    g2 sig1 = sign(sk1, message_1);

    array<uint64_t, 4> sk2 = secret_key(seed_2);
    g1 pk2 = public_key(sk2);
    g2 sig2 = sign(sk2, message_1);

    g2 aggsig = aggregate_signatures(vector<g2> {sig1, sig2});
    bool ok = pop_fast_aggregate_verify(vector<g1>{pk1, pk2}, message_1, aggsig);
    BOOST_CHECK_EQUAL(ok, true);
} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_SUITE_END()

