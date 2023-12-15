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

  bls_public_key agg_pk = pk;
  bls_signature agg_signature = signature;
   
  for (int i = 1 ; i< 21 ;i++){
    agg_pk = aggregate({agg_pk, pk});
    agg_signature = aggregate({agg_signature, signature});
  }

  // Verify the signature
  bool ok = verify(agg_pk, v, agg_signature);

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

  bls_public_key aggKey = aggregate({pk1, pk2});
  bls_signature aggSig = aggregate({sig1, sig2});

  // Verify the signature
  bool ok = verify(aggKey, message_1, aggSig);

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

  bls_signature aggSig = aggregate({sig1, sig2});

  std::vector<bls_public_key> pubkeys = {pk1, pk2};
  std::vector<std::vector<uint8_t>> messages = {message_1, message_2};
  
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
  BOOST_CHECK_NO_THROW(bls_private_key("PVT_BLS_O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"));
  BOOST_CHECK_NO_THROW(bls_public_key("PUB_BLS_dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="));
  BOOST_CHECK_NO_THROW(bls_signature("SIG_BLS_prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="));

  //test no pivot delimiter
  BOOST_CHECK_THROW(bls_private_key("PVTBLSO+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUBBLSdEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIGBLSprHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="), fc::assert_exception);

  //test first prefix validation
  BOOST_CHECK_THROW(bls_private_key("XYZ_BLS_O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("XYZ_BLS_dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("XYZ_BLS_prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="), fc::assert_exception);

  //test second prefix validation
  BOOST_CHECK_THROW(bls_private_key("PVT_XYZ_O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_XYZ_dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_XYZ_prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="), fc::assert_exception);

  //test missing prefix
  BOOST_CHECK_THROW(bls_private_key("O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="), fc::assert_exception);

  //test incomplete prefix
  BOOST_CHECK_THROW(bls_private_key("PVT_O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_private_key("BLS_O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("BLS_dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("BLS_prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="), fc::assert_exception);

  //test invalid data / invalid checksum
  BOOST_CHECK_THROW(bls_private_key("PVT_BLS_O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8a"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_BLS_dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxaQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_BLS_prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlyA=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_private_key("PVT_BLS_p+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8D"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_BLS_cEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxpQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_BLS_rrHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0DlzA=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_private_key("PVT_BLS_O+gS5yNxVtSwL0/Uhl1IVqu/Y7Gq4qdrtB66EIb192ENfK8B"), fc::assert_exception);
  BOOST_CHECK_THROW(bls_public_key("PUB_BLS_dEvut0ydHevDGP6Ef3O4Iq6QXf9jUcMUT1nCJRX+JRYlFYrO/qKt/x439vUJ2DkZ32Od6AdJZ+S9dWRE9Sy+7Q6bNjpoIOP0cWzkKC1DqmhfE3paW+KThA3noLkV8SsILcfxsQ=="), fc::assert_exception);
  BOOST_CHECK_THROW(bls_signature("SIG_BLS_prHR3PtNGJQLEhqcuHqj5Ty6FkNFo+ih32+ZHh6LH74+SKlTgq4PWtudoYt8heEZjPyNDRrqfRoYoTlAZ1mpW0QzgyGRXU+lfZ27M9Bg1mNS0MI6wWL4ZG9E8bchiMUVWijpX66sc11t60m/g8/vJIf1tIuFLhKCcX57OVCoXisciI7D21b3tKjb7VAlc2oNEoJx17XOafIWvcH1YKAc2uv9T/ocAlE3VQNprXKuGaZYA9Q5yzaOhVgGYxrjv/wNv0Dlzb=="), fc::assert_exception);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(bls_variant) try {
      bls_private_key prk("PVT_BLS_nv6z13d5yfQk4Mq07Fdmpvzsd+hgGAeL4wBQQH1cCAlB7Nka");
      bls_public_key pk("PUB_BLS_MPPeebAPxt/ibL2XPuZVGpADjGn+YEVPPoYmTZeBD6Ok2E19M8SnmDGSdZBf2qwSuJim+8H83EsTpEn3OiStWBiFeJYfVRLlEsZuSF0SYYwtVteY48n+KeE1IWzlSAkSyBqiGA==");
      bls_signature sig("SIG_BLS_UVb0SXln6xkg7X+y3ATkxoSOouIxgYLR/tf+UBz2VXeA0ujahQFRTux/e9/eifkJ7TguHKjMxNMv+tVDIn03DFlav468CagmW/if+lJJjT5ZD/Uhj1OvddUOR6gzD7sLuwL3bQ52L8HXaaWM2ksonwhD03JO3GeZj3j43naG0GstBVaCPpE84WBFyqTBFkcMnLO3LGkJXs5l2VZmtYpI8Z/UlerI0+jiYOzA+p9LTfjfng5HHx367WpMYiK2hyoEiILS1A==");

      fc::variant v;
      std::string s;
      v = prk;
      s = fc::json::to_string(v, {});
      BOOST_CHECK_EQUAL(s, "\"" + prk.to_string({}) + "\"");

      v = pk;
      s = fc::json::to_string(v, {});
      BOOST_CHECK_EQUAL(s, "\"" + pk.to_string({}) + "\"");

      v = sig;
      s = fc::json::to_string(v, {});
      BOOST_CHECK_EQUAL(s, "\"" + sig.to_string({}) + "\"");
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
