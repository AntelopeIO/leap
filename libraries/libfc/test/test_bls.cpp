#include <boost/test/unit_test.hpp>

#include <fc/exception/exception.hpp>


#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <fc/crypto/sha256.hpp>

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

  //cout << "pk : " << pk.to_string() << "\n";
  //cout << "signature : " << signature.to_string() << "\n";

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

  //cout << "pk : " << pk.to_string() << "\n";
  //cout << "signature : " << signature.to_string() << "\n";

  // Verify the signature
  bool ok = verify(pk, v, signature);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


//test a single key signature + verification of hotstuff tuple
BOOST_AUTO_TEST_CASE(bls_sig_verif_hotstuff_types) try {

  bls_private_key sk = bls_private_key(seed_1);
  bls_public_key pk = sk.get_public_key();

  string cmt = "cm_prepare";
  uint32_t view_number = 264;

  string s_view_number = to_string(view_number);
  string c_s = cmt + s_view_number;

  fc::sha256 h1 = fc::sha256::hash(c_s);
  fc::sha256 h2 = fc::sha256::hash( std::make_pair( h1, message_3 ) );

  std::vector<unsigned char> v = std::vector<unsigned char>(h2.data(), h2.data() + 32);

  bls_signature signature = sk.sign(v);

  bls_public_key agg_pk = pk;
  bls_signature agg_signature = signature;
   
  for (int i = 1 ; i< 21 ;i++){
    agg_pk = aggregate({agg_pk, pk});
    agg_signature = aggregate({agg_signature, signature});
  }

  //cout << "pk : " << pk.to_string() << "\n";
  //cout << "signature : " << signature.to_string() << "\n";

  // Verify the signature
  bool ok = verify(agg_pk, v, agg_signature);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();

//test serialization / deserialization of private key, public key and signature
BOOST_AUTO_TEST_CASE(bls_serialization_test) try {

  bls_private_key sk = bls_private_key(seed_1);
  bls_public_key pk = sk.get_public_key();

  bls_signature signature = sk.sign(message_1);

  std::string pk_string = pk.to_string();
  std::string signature_string = signature.to_string();

  //cout << pk_string << "\n";
  //cout << signature_string << "\n";

  bls_public_key pk2 = bls_public_key(pk_string);
  bls_signature signature2 = bls_signature(signature_string);

  //cout << pk2.to_string() << "\n";
  //cout << signature2.to_string() << "\n";

  bool ok = verify(pk2, message_1, signature2);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();

//test public keys + signatures aggregation + verification
BOOST_AUTO_TEST_CASE(bls_agg_sig_verif) try {

  bls_private_key sk1 = bls_private_key(seed_1);
  bls_public_key pk1 = sk1.get_public_key();

  bls_signature sig1 = sk1.sign(message_1);

  //cout << "pk1 : " << pk1.to_string() << "\n";
  //cout << "sig1 : " << sig1.to_string() << "\n";

  bls_private_key sk2 = bls_private_key(seed_2);
  bls_public_key pk2 = sk2.get_public_key();

  bls_signature sig2 = sk2.sign(message_1);

  //cout << "pk2 : "  << pk2.to_string() << "\n";
  //cout << "sig2 : "  << sig2.to_string() << "\n";

  bls_public_key aggKey = aggregate({pk1, pk2});
  bls_signature aggSig = aggregate({sig1, sig2});

 // cout << "aggKey : "  << aggKey.to_string() << "\n";
  //cout << "aggSig : "  << aggSig.to_string() << "\n";

  // Verify the signature
  bool ok = verify(aggKey, message_1, aggSig);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();


//test signature aggregation + aggregate tree verification
BOOST_AUTO_TEST_CASE(bls_agg_tree_verif) try {

  bls_private_key sk1 = bls_private_key(seed_1);
  bls_public_key pk1 = sk1.get_public_key();

  bls_signature sig1 = sk1.sign(message_1);

  //cout << "pk1 : " << pk1.to_string() << "\n";
  //cout << "sig1 : " << sig1.to_string() << "\n";

  bls_private_key sk2 = bls_private_key(seed_2);
  bls_public_key pk2 = sk2.get_public_key();

  bls_signature sig2 = sk2.sign(message_2);

  //cout << "pk2 : "  << pk2.to_string() << "\n";
  //cout << "sig2 : "  << sig2.to_string() << "\n";

  bls_signature aggSig = aggregate({sig1, sig2});

  //cout << "aggSig : "  << aggSig.to_string() << "\n";

  vector<bls_public_key> pubkeys = {pk1, pk2};
  vector<vector<uint8_t>> messages = {message_1, message_2};
  
  // Verify the signature
  bool ok = aggregate_verify(pubkeys, messages, aggSig);

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

//test private key base58 encoding
BOOST_AUTO_TEST_CASE(bls_private_key_string_encoding) try {

  bls_private_key sk = bls_private_key(seed_1);

  bls_public_key pk = sk.get_public_key();
  
  std::string priv_base58_str = sk.to_string();

  //cout << "priv_base58_str : " << priv_base58_str << "\n";

  bls_private_key sk2 = bls_private_key(priv_base58_str);

  //cout << "sk2 : " << sk2.to_string() << "\n";

  bls_signature signature = sk2.sign(message_1);

  //cout << "pk : " << pk.to_string() << "\n";
  //cout << "signature : " << signature.to_string() << "\n";

  // Verify the signature
  bool ok = verify(pk, message_1, signature);

  BOOST_CHECK_EQUAL(ok, true);

} FC_LOG_AND_RETHROW();



BOOST_AUTO_TEST_SUITE_END()
