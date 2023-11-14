#include <fc/crypto/public_key.hpp>
#include <fc/crypto/private_key.hpp>
#include <fc/crypto/signature.hpp>
#include <fc/crypto/k1_recover.hpp>

#include <benchmark.hpp>

using namespace fc::crypto;
using namespace fc;
using namespace std::literals;

namespace eosio::benchmark {

void k1_sign_benchmarking() {
   auto payload = "Test Cases";
   auto digest = sha256::hash(payload, const_strlen(payload));
   auto private_key_string = std::string("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3");
   auto key = private_key(private_key_string);

   auto sign_non_canonical_f = [&]() {
      key.sign(digest, false);
   };
   benchmarking("k1_sign_non_canonical", sign_non_canonical_f);
}

void k1_recover_benchmarking() {
   auto signature = to_bytes( "1b323dd47a1dd5592c296ee2ee12e0af38974087a475e99098a440284f19c1f7642fa0baa10a8a3ab800dfdbe987dee68a09b6fa3db45a5cc4f3a5835a1671d4dd");
   auto digest    = to_bytes( "92390316873c5a9d520b28aba61e7a8f00025ac069acd9c4d2a71d775a55fa5f");

   auto recover_f = [&]() {
      fc::k1_recover(signature, digest);
   };
   benchmarking("k1_recover", recover_f);
}

void k1_benchmarking() {
   k1_sign_benchmarking();
   k1_recover_benchmarking();
}

void r1_benchmarking() {
   auto payload = "Test Cases";
   auto digest = sha256::hash(payload, const_strlen(payload));
   auto key = private_key::generate<r1::private_key_shim>();

   auto sign_f = [&]() {
      key.sign(digest);
   };
   benchmarking("r1_sign", sign_f);

   auto sig = key.sign(digest);
   auto recover_f = [&]() {
      public_key(sig, digest);;
   };
   benchmarking("r1_recover", recover_f);
}

static fc::crypto::webauthn::signature make_webauthn_sig(const fc::crypto::r1::private_key& priv_key,
                                                         std::vector<uint8_t>& auth_data,
                                                         const std::string& json) {

   //webauthn signature is sha256(auth_data || client_data_hash)
   fc::sha256 client_data_hash = fc::sha256::hash(json);
   fc::sha256::encoder e;
   e.write((char*)auth_data.data(), auth_data.size());
   e.write(client_data_hash.data(), client_data_hash.data_size());

   r1::compact_signature sig = priv_key.sign_compact(e.result());

   char buff[8192];
   datastream<char*> ds(buff, sizeof(buff));
   fc::raw::pack(ds, sig);
   fc::raw::pack(ds, auth_data);
   fc::raw::pack(ds, json);
   ds.seekp(0);

   fc::crypto::webauthn::signature ret;
   fc::raw::unpack(ds, ret);

   return ret;
}

void wa_benchmarking() {
   static const r1::private_key priv = fc::crypto::r1::private_key::generate();
   static const fc::sha256 d = fc::sha256::hash("sup"s);
   static const fc::sha256 origin_hash = fc::sha256::hash("fctesting.invalid"s);
   std::string json = "{\"origin\":\"https://fctesting.invalid\",\"type\":\"webauthn.get\", \"challenge\":\"" + fc::base64url_encode(d.data(), d.data_size()) + "\"}";
   std::vector<uint8_t> auth_data(37);
   memcpy(auth_data.data(), origin_hash.data(), sizeof(origin_hash));

   auto sign = [&]() {
      make_webauthn_sig(priv, auth_data, json);
   };
   benchmarking("webauthn_sign", sign);

   auto sig = make_webauthn_sig(priv, auth_data, json);
   auto recover= [&]() {
      sig.recover(d, true);
   };
   benchmarking("webauthn_recover", recover);
}

void key_benchmarking() {
   k1_benchmarking();
   r1_benchmarking();
   wa_benchmarking();
}

} // benchmark
