#include <boost/test/unit_test.hpp>

#include <fc/exception/exception.hpp>

#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <fc/crypto/sha256.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

using std::cout;

using namespace fc::crypto::blslib;

BOOST_AUTO_TEST_SUITE(bls_test)

// can we use BLS stuff?

// Example seed, used to generate private key. Always use
// a secure RNG with sufficient entropy to generate a seed (at least 32 bytes).
std::vector<uint8_t> seed_1 = {  0,  50, 6,  244, 24,  199, 1,  25,  52,  88,  192,
                            19, 18, 12, 89,  6,   220, 18, 102, 58,  209, 82,
                            12, 62, 89, 110, 182, 9,   44, 20,  254, 22};

std::vector<uint8_t> seed_2 = {  6,  51, 22,  89, 11,  15, 4,  61,  127,  241,  79,
                            26, 88, 52, 1,  6,   18, 79, 10, 8, 36, 182,
                            154, 35, 75, 156, 215, 41,   29, 90,  125, 233};

std::vector<uint8_t> message_1 = { 51, 23, 56, 93, 212, 129, 128, 27, 
                            251, 12, 42, 129, 210, 9, 34, 98};  // Message is passed in as a byte vector


std::vector<uint8_t> message_2 = { 16, 38, 54, 125, 71, 214, 217, 78, 
                            73, 23, 127, 235, 8, 94, 41, 53};  // Message is passed in as a byte vector

fc::sha256 message_3 = fc::sha256("1097cf48a15ba1c618237d3d79f3c684c031a9844c27e6b95c6d27d8a5f401a1");


//test a single key signature + verification
BOOST_AUTO_TEST_CASE(bls_sig_verif) try {

  bls_private_key sk = bls_private_key(seed_1);
  bls_public_key pk = sk.get_public_key();

  bls_signature signature = sk.sign(message_1);

  // Verify the signature
  bool ok = verify(pk, message_1, signature);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();

//test a single key signature + verification of digest_type
BOOST_AUTO_TEST_CASE(bls_sig_verif_digest) try {

  bls_private_key sk = bls_private_key(seed_1);
  bls_public_key pk = sk.get_public_key();

  std::vector<unsigned char> v = std::vector<unsigned char>(message_3.data(), message_3.data() + 32);

  bls_signature signature = sk.sign(v);

  // Verify the signature
  bool ok = verify(pk, v, signature);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


//test a single key signature + verification of hotstuff tuple
BOOST_AUTO_TEST_CASE(bls_sig_verif_hotstuff_types) try {

  bls_private_key sk = bls_private_key(seed_1);
  bls_public_key pk = sk.get_public_key();

  std::string cmt = "cm_prepare";
  uint32_t view_number = 264;

  std::string s_view_number = std::to_string(view_number);
  std::string c_s = cmt + s_view_number;

  fc::sha256 h1 = fc::sha256::hash(c_s);
  fc::sha256 h2 = fc::sha256::hash( std::make_pair( h1, message_3 ) );

  std::vector<unsigned char> v = std::vector<unsigned char>(h2.data(), h2.data() + 32);

  bls_signature signature = sk.sign(v);

  bls12_381::g1 agg_pk = pk.jacobian_montgomery_le();
  bls_signature agg_signature = signature;
   
  for (int i = 1 ; i< 21 ;i++){
    agg_pk = bls12_381::aggregate_public_keys(std::array{agg_pk, pk.jacobian_montgomery_le()});
    agg_signature = aggregate(std::array{agg_signature, signature});
  }

  // Verify the signature
  bool ok = bls12_381::verify(agg_pk, v, agg_signature._sig); //jacobian_montgomery_le());

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


//test public keys + signatures aggregation + verification
BOOST_AUTO_TEST_CASE(bls_agg_sig_verif) try {

  bls_private_key sk1 = bls_private_key(seed_1);
  bls_public_key pk1 = sk1.get_public_key();

  bls_signature sig1 = sk1.sign(message_1);

  bls_private_key sk2 = bls_private_key(seed_2);
  bls_public_key pk2 = sk2.get_public_key();

  bls_signature sig2 = sk2.sign(message_1);

  bls12_381::g1 agg_key = bls12_381::aggregate_public_keys(std::array{pk1.jacobian_montgomery_le(), pk2.jacobian_montgomery_le()});
  bls_signature agg_sig = aggregate(std::array{sig1, sig2});

  // Verify the signature
  bool ok = bls12_381::verify(agg_key, message_1, agg_sig._sig);//.jacobian_montgomery_le());

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


//test signature aggregation + aggregate tree verification
BOOST_AUTO_TEST_CASE(bls_agg_tree_verif) try {

  bls_private_key sk1 = bls_private_key(seed_1);
  bls_public_key pk1 = sk1.get_public_key();

  bls_signature sig1 = sk1.sign(message_1);

  bls_private_key sk2 = bls_private_key(seed_2);
  bls_public_key pk2 = sk2.get_public_key();

  bls_signature sig2 = sk2.sign(message_2);

  bls_signature aggSig = aggregate(std::array{sig1, sig2});

  std::vector<bls12_381::g1> pubkeys = {pk1.jacobian_montgomery_le(), pk2.jacobian_montgomery_le()};
  std::vector<std::vector<uint8_t>> messages = {message_1, message_2};
  
  // Verify the signature
  bool ok = bls12_381::aggregate_verify(pubkeys, messages, aggSig._sig); //jacobian_montgomery_le());

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();

//test random key generation, signature + verification
BOOST_AUTO_TEST_CASE(bls_key_gen) try {

  bls_private_key sk = bls_private_key::generate();
  bls_public_key pk = sk.get_public_key();

  bls_signature signature = sk.sign(message_1);

  // Verify the signature
  bool ok = verify(pk, message_1, signature);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


//test wrong key and wrong signature
BOOST_AUTO_TEST_CASE(bls_bad_sig_verif) try {

  bls_private_key sk1 = bls_private_key(seed_1);
  bls_public_key pk1 = sk1.get_public_key();

  bls_signature sig1 = sk1.sign(message_1);

  bls_private_key sk2 = bls_private_key(seed_2);
  bls_public_key pk2 = sk2.get_public_key();

  bls_signature sig2 = sk2.sign(message_1);

  // Verify the signature
  bool ok1 = verify(pk1, message_1, sig2); //verify wrong key / signature
  bool ok2 = verify(pk2, message_1, sig1); //verify wrong key / signature

  BOOST_CHECK_EQUAL(ok1, false);
  BOOST_CHECK_EQUAL(ok2, false);


} FC_LOG_AND_RETHROW();

//test bls private key base58 encoding / decoding / serialization / deserialization
BOOST_AUTO_TEST_CASE(bls_private_key_serialization) try {

  bls_private_key sk = bls_private_key(seed_1);

  bls_public_key pk = sk.get_public_key();

  std::string priv_base58_str = sk.to_string();

  bls_private_key sk2 = bls_private_key(priv_base58_str);

  bls_signature signature = sk2.sign(message_1);

  // Verify the signature
  bool ok = verify(pk, message_1, signature);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


//test bls public key and bls signature base58 encoding / decoding / serialization / deserialization
BOOST_AUTO_TEST_CASE(bls_pub_key_sig_serialization) try {

  bls_private_key sk = bls_private_key(seed_1);
  bls_public_key pk = sk.get_public_key();

  bls_signature signature = sk.sign(message_1);

  std::string pk_string = pk.to_string();
  std::string signature_string = signature.to_string();

  bls_public_key pk2 = bls_public_key(pk_string);
  bls_signature signature2 = bls_signature(signature_string);

  bool ok = verify(pk2, message_1, signature2);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_CASE(bls_binary_keys_encoding_check) try {

  bls_private_key sk = bls_private_key(seed_1);

  bool ok1 = bls_private_key(sk.to_string()) == sk;

  std::string priv_str = sk.to_string();

  bool ok2 = bls_private_key(priv_str).to_string() == priv_str;

  bls_public_key pk = sk.get_public_key();

  bool ok3 = bls_public_key(pk.to_string()).equal(pk);

  std::string pub_str = pk.to_string();

  bool ok4 = bls_public_key(pub_str).to_string() == pub_str;

  bls_signature sig = sk.sign(message_1);

  bool ok5 = bls_signature(sig.to_string()).equal(sig);

  std::string sig_str = sig.to_string();

  bool ok6 = bls_signature(sig_str).to_string() == sig_str;

  bool ok7 = verify(pk, message_1, bls_signature(sig.to_string()));
  bool ok8 = verify(pk, message_1, sig);

  BOOST_CHECK_EQUAL(ok1, true); //succeeds
  BOOST_CHECK_EQUAL(ok2, true); //succeeds
  BOOST_CHECK_EQUAL(ok3, true); //succeeds
  BOOST_CHECK_EQUAL(ok4, true); //succeeds
  BOOST_CHECK_EQUAL(ok5, true); //fails
  BOOST_CHECK_EQUAL(ok6, true); //succeeds
  BOOST_CHECK_EQUAL(ok7, true); //succeeds
  BOOST_CHECK_EQUAL(ok8, true); //succeeds

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_regenerate_check) try {

  bls_private_key sk1 = bls_private_key(seed_1);
  bls_private_key sk2 = bls_private_key(seed_1);

  BOOST_CHECK_EQUAL(sk1.to_string(), sk2.to_string());

  bls_public_key pk1 = sk1.get_public_key();
  bls_public_key pk2 = sk2.get_public_key();

  BOOST_CHECK_EQUAL(pk1.to_string(), pk2.to_string());

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_prefix_encoding_check) try {

  //test no_throw for correctly encoded keys
  BOOST_CHECK_NO_THROW(bls_private_key("PVT_BLS_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"));
  BOOST_CHECK_NO_THROW(bls_public_key("PUB_BLS_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"));
  BOOST_CHECK_NO_THROW(bls_signature("SIG_BLS_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"));

  //test no pivot delimiter
  BOOST_CHECK_THROW(bls_private_key("PVTBLSvh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUBBLS82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIGBLSRrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"), fc::assert_exception);

  //test first prefix validation
  BOOST_CHECK_THROW(bls_private_key("XYZ_BLS_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("XYZ_BLS_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("XYZ_BLS_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"), fc::assert_exception);

  //test second prefix validation
  BOOST_CHECK_THROW(bls_private_key("PVT_XYZ_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_XYZ_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_XYZ_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"), fc::assert_exception);

  //test missing prefix
  BOOST_CHECK_THROW(bls_private_key("vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"), fc::assert_exception);

  //test incomplete prefix
  BOOST_CHECK_THROW(bls_private_key("PVT_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_private_key("BLS_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("BLS_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("BLS_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"), fc::assert_exception);

  //test invalid data / invalid checksum
  BOOST_CHECK_THROW(bls_private_key("PVT_BLS_wh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_BLS_92P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_BLS_SrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_private_key("PVT_BLS_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5zc"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_BLS_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhdg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_BLS_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJug"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_private_key("PVT_BLS_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yd"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_BLS_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhTg"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_BLS_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJUg"), fc::assert_exception);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_variant) try {
     bls_private_key prk("PVT_BLS_vh0bYgBLOLxs_h9zvYNtj20yj8UJxWeFFAtDUW2_pG44e5yc");
     bls_public_key pk("PUB_BLS_82P3oM1u0IEv64u9i4vSzvg1-QDl4Fb2n50Mp8Sk7Fr1Tz0MJypzL39nSd5VPFgFC9WqrjopRbBm1Pf0RkP018fo1k2rXaJY7Wtzd9RKlE8PoQ6XhDm4PyZlIupQg_gOuiMhcg");
     bls_signature sig("SIG_BLS_RrwvP79LxfahskX-ceZpbgrJ1aUkSSIzE2sMFj0twuhK8QwjcGMvT2tZ_-QMHvAV83tWZYOs7SEvoyteCKGD_Tk6YySkw1HONgvVeNWM8ZwuNgonOHkegNNPIXSIvWMTczfkg2lEtEh-ngBa5t9-4CvZ6aOjg29XPVvu6dimzHix-9E0M53YkWZ-gW5GDkkOLoN2FMxjXaELmhuI64xSeSlcWLFfZa6TMVTctBFWsHDXm1ZMkURoB83dokKHEi4OQTbJtg");

      fc::variant v;
      std::string s;
      v = prk;
      s = fc::json::to_string(v, {});
      BOOST_CHECK_EQUAL(s, "\"" + prk.to_string() + "\"");

      v = pk;
      s = fc::json::to_string(v, {});
      BOOST_CHECK_EQUAL(s, "\"" + pk.to_string() + "\"");

      v = sig;
      s = fc::json::to_string(v, {});
      BOOST_CHECK_EQUAL(s, "\"" + sig.to_string({}) + "\"");
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
