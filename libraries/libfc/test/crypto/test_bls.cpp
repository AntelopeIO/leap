#include <boost/test/unit_test.hpp>

#include <bls12-381.hpp>
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

    string pk_string = bytesToHex<144>(pk.toJacobianBytesBE());
    string signature_string = bytesToHex<288>(signature.toJacobianBytesBE());
    cout << pk_string << std::endl;
    cout << signature_string << std::endl;

    g1 pk2 = g1::fromJacobianBytesBE(hexToBytes(pk_string)).value();
    g2 signature2 = g2::fromJacobianBytesBE(hexToBytes(signature_string)).value();
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

    g2 aggSig = aggregate_signatures({sig1, sig2});

    bool ok = aggregate_verify({pk1, pk2}, {message_1, message_2}, aggSig);
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

    g2 aggSig = aggregate_signatures({sig1, sig2});

    array<uint64_t, 4> sk3 = secret_key(seed_3);
    g1 pk3 = public_key(sk3);
    g2 sig3 = sign(sk3, message_3);

    g2 aggSigFinal = aggregate_signatures({aggSig, sig3});

    bool ok = aggregate_verify({pk1, pk2, pk3}, {message_1, message_2, message_3}, aggSigFinal);
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

    g2 sigAgg = aggregate_signatures({sig1, sig2, sig3});
    g1 pkAgg = aggregate_public_keys({pk1, pk2, pk3});

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

    g2 aggsig = aggregate_signatures({sig1, sig2});
    bool ok = pop_fast_aggregate_verify({pk1, pk2}, message_1, aggsig);
    BOOST_CHECK_EQUAL(ok, true);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(g1_add_garbage) try {
    fp x({0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    g1 p({x, x, x});
    BOOST_CHECK_EQUAL(x.isValid(), false);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    p = p.add(p);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    g1 p_res = g1::fromJacobianBytesBE(hexToBytes<144>("0x16ebb8f4fc6d887a8de3892d7765b224e3be0f36357a686712241e5767c245ec7d9fc4130046ed883e31ec7d2400d69b02c2a8b22ceaac76c93d771a681011c66189e08d3a16e69aa7484528ffe9d89fbe1664fdff95578c830e0fbfc72447800ffc7c19987633398fa120983552fa3ecab80aa3bdcc0913014c80513279e56ce11624eaffddf5f82fa804b27016e591"), false, true).value();
    BOOST_CHECK_EQUAL(p.equal(p_res), true);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(g2_add_garbage) try {
    fp x({0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    g2 p({fp2({x, x}), fp2({x, x}), fp2({x, x})});
    BOOST_CHECK_EQUAL(x.isValid(), false);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    p = p.add(p);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    g2 p_res = g2::fromJacobianBytesBE(hexToBytes<288>("0x121776a6107dd86184188133433092b521527d235a298207529d4ca1679f9794cd3cb7b659cdccbfea32ada2d46fdf3ef7f0b08b6d3cfbad209ba461e8bdc55aadc7da5ac22f4e67b5a88062646f2ece0934d01ca6485f299f47cd132da484600df7cabe551c79ec8622ec6c73e03e2635ee50e36584b13b7f371b634bc00910932bd543a35b45dc33d90bc36d38c88202988dd47f01acf772efd5446c81949ebdc19ca53273a1f07a449b084faf4c8c329179e392dd49ffd4d0c81ce02ae50b35ef56f72b6d4b067b495bc80cfce0eb0d3e6d9aebea696b61e198f9b8bb2394ae2049e1c3c7ebf2d5590964e030cb27000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"), false, true).value();
    BOOST_CHECK_EQUAL(p.equal(p_res), true);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(g1_mul_garbage) try {
    fp x({0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    g1 p({x, x, x});
    BOOST_CHECK_EQUAL(x.isValid(), false);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    array<uint64_t, 4> s = {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
    p = p.mulScalar(s);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    g1 p_res = g1::fromJacobianBytesBE(hexToBytes<144>("0x0cf5e7694dd3cbfd944aa8a1412826451b247cc74148a1c289831a869c2bf644d8eacf23970af6d167fe0efe4e79b8b61183d39242b00320670c7474c28aeda64187e877d9972619702fc9459876563ea9f8054a4a22262a3566e3af5a4970510e9213062adcdd95878b09e3901d27f47b77a2dc03923eb313856cf2991eb7ec1f76d8da7a832bfc4db4735821ff9081"), false, true).value();
    BOOST_CHECK_EQUAL(p.equal(p_res), true);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(g2_mul_garbage) try {
    fp x({0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    g2 p({fp2({x, x}), fp2({x, x}), fp2({x, x})});
    BOOST_CHECK_EQUAL(x.isValid(), false);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    array<uint64_t, 4> s = {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
    p = p.mulScalar(s);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    g2 p_res = g2::fromJacobianBytesBE(hexToBytes<288>("0x1203754ff2c1cd33f92b7fbad909540237721c0311f3935762719feca1d4e8d5006824434283611b87fadcc93b41b79318f1bb3b6a6ce403bfac295e096ea17a61d553fbed89f453a78232e88eab2767907eb9f75e9e325db106abd65f5de13d013ed4f63b9142ecdaf225888e13285adb14384fb623ce33a640e04dadcb38090f60d99767be09abe35b3c2337819e50038f9df049cbf0ee1c481560d7fe03be89e3fa68a5f69aab20a40ac2c522ecd89e5e5859753dfa4ecbde951b2e5ae732146f8f94d30becf0c33b7833728f9a0e8292f574d85fd1bf82fef8cb79ff1b5e6bf15e3000027fa9e9e6f670f956220b02fb798444358ffed2efa8999e5ffc27a57a08c8cc44c02ee47cc2ee4e535c046217196095c26de1f4a5ba9866c15c93"), false, true).value();
    BOOST_CHECK_EQUAL(p.equal(p_res), true);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(g1_exp_garbage) try {
    fp x({0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    g1 p({x, x, x});
    BOOST_CHECK_EQUAL(x.isValid(), false);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    array<uint64_t, 4> s = {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
    p = g1::multiExp({p}, {s}).value();
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    g1 p_res = g1::fromJacobianBytesBE(hexToBytes<144>("0x181b676153b877407d2622e91af6057f5ff445f160c178517828841670debdd61957f8d5376ddeeb1ba0a204eb1eafb007f9d1417540591155acddd91f1fb9c97da24d6eecae002c50a779372dfc247efb1823e27abbdae09fb515f390e982311239b452c1ef85156c979f981ac69208f6fd0014fa9dd66a1999df7fa4a0a4234a4cc14ec62291fd3f924b8353b326b9"), false, true).value();
    BOOST_CHECK_EQUAL(p.equal(p_res), true);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(g2_exp_garbage) try {
    fp x({0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF});
    g2 p({fp2({x, x}), fp2({x, x}), fp2({x, x})});
    BOOST_CHECK_EQUAL(x.isValid(), false);
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    array<uint64_t, 4> s = {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF};
    p = g2::multiExp({p}, {s}).value();
    BOOST_CHECK_EQUAL(p.isOnCurve(), false);
    BOOST_CHECK_EQUAL(p.inCorrectSubgroup(), false);
    g2 p_res = g2::fromJacobianBytesBE(hexToBytes<288>("0x158a2a1e3ce68c49f9795908aa3779c6919ed5de5cbcd1d2a331d0742d1eb3cb28014006b5f686204adb5fdca73aea570ee0f0d58880907c8de5867dd99b6b7306b2c3de4a1537e6d042f2b8e44c8086853728cc246726016b0fcf993db3d759005f8ac0cb55113c857c5cf3f83d9b624ce9a2a0a00a1206777cf935721c857b322a611ed0703cf3e922bfb8b19a1f5e10a341b2191ab5a15d35f69850d2adb633e5425eecb7f38dd486a95b3f74d60f3ee6cf692b3c76813407710630763f7605b3828c19203f661732a02f7f546ab354694128bbe5a792a9db4a443c0fe10af0df2bc1b8d07aee99bd6f8c6b26847011aa31634f42f722d52022c736369db470576687fdf819cf15a0db4c01a0bd7028ee17cefdf6d66557d47fb725b6d00f"), false, true).value();
    BOOST_CHECK_EQUAL(p.equal(p_res), true);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()

