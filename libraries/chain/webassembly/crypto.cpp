#include <eosio/chain/webassembly/interface.hpp>
#include <eosio/chain/protocol_state_object.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/apply_context.hpp>

namespace eosio { namespace chain { namespace webassembly {

   void interface::assert_recover_key( legacy_ptr<const fc::sha256> digest,
                                       legacy_span<const char> sig,
                                       legacy_span<const char> pub ) const {
      fc::crypto::signature s;
      fc::crypto::public_key p;
      datastream<const char*> ds( sig.data(), sig.size() );
      datastream<const char*> pubds ( pub.data(), pub.size() );

      fc::raw::unpack( ds, s );
      fc::raw::unpack( pubds, p );

      EOS_ASSERT(static_cast<unsigned>(s.which()) < context.db.get<protocol_state_object>().num_supported_key_types, unactivated_signature_type,
        "Unactivated signature type used during assert_recover_key");
      EOS_ASSERT(static_cast<unsigned>(p.which()) < context.db.get<protocol_state_object>().num_supported_key_types, unactivated_key_type,
        "Unactivated key type used when creating assert_recover_key");

      if(context.control.is_producing_block())
         EOS_ASSERT(s.variable_size() <= context.control.configured_subjective_signature_length_limit(),
                    sig_variable_size_limit_exception, "signature variable length component size greater than subjective maximum");

      auto check = fc::crypto::public_key( s, *digest, false );
      EOS_ASSERT( check == p, crypto_api_exception, "Error expected key different than recovered key" );
   }

   int32_t interface::recover_key( legacy_ptr<const fc::sha256> digest,
                                   legacy_span<const char> sig,
                                   legacy_span<char> pub ) const {
      fc::crypto::signature s;
      datastream<const char*> ds( sig.data(), sig.size() );
      fc::raw::unpack(ds, s);

      EOS_ASSERT(static_cast<unsigned>(s.which()) < context.db.get<protocol_state_object>().num_supported_key_types, unactivated_signature_type,
                 "Unactivated signature type used during recover_key");

      if(context.control.is_producing_block())
         EOS_ASSERT(s.variable_size() <= context.control.configured_subjective_signature_length_limit(),
                    sig_variable_size_limit_exception, "signature variable length component size greater than subjective maximum");


      auto recovered = fc::crypto::public_key(s, *digest, false);

      // the key types newer than the first 2 may be varible in length
      if (static_cast<unsigned>(s.which()) >= config::genesis_num_supported_key_types ) {
         EOS_ASSERT(pub.size() >= 33, wasm_execution_error,
                    "destination buffer must at least be able to hold an ECC public key");
         auto packed_pubkey = fc::raw::pack(recovered);
         auto copy_size = std::min<size_t>(pub.size(), packed_pubkey.size());
         std::memcpy(pub.data(), packed_pubkey.data(), copy_size);
         return packed_pubkey.size();
      } else {
         // legacy behavior, key types 0 and 1 always pack to 33 bytes.
         // this will do one less copy for those keys while maintaining the rules of
         //    [0..33) dest sizes: assert (asserts in fc::raw::pack)
         //    [33..inf) dest sizes: return packed size (always 33)
         datastream<char*> out_ds( pub.data(), pub.size() );
         fc::raw::pack(out_ds, recovered);
         return out_ds.tellp();
      }
   }

   void interface::assert_sha256(legacy_span<const char> data, legacy_ptr<const fc::sha256> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::sha256>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::assert_sha1(legacy_span<const char> data, legacy_ptr<const fc::sha1> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::sha1>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::assert_sha512(legacy_span<const char> data, legacy_ptr<const fc::sha512> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::sha512>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::assert_ripemd160(legacy_span<const char> data, legacy_ptr<const fc::ripemd160> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::ripemd160>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::sha1(legacy_span<const char> data, legacy_ptr<fc::sha1> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::sha1>( data.data(), data.size() );
   }

   void interface::sha256(legacy_span<const char> data, legacy_ptr<fc::sha256> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::sha256>( data.data(), data.size() );
   }

   void interface::sha512(legacy_span<const char> data, legacy_ptr<fc::sha512> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::sha512>( data.data(), data.size() );
   }

   void interface::ripemd160(legacy_span<const char> data, legacy_ptr<fc::ripemd160> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::ripemd160>( data.data(), data.size() );
   }

   /**
    * WAX specific
    *
    * signature, exponent and modulus must be hexadecimal strings
    */
   int32_t interface::verify_rsa_sha256_sig(legacy_span<const char> message,
                                            legacy_span<const char> signature,
                                            legacy_span<const char> exponent,
                                            legacy_span<const char> modulus)
   {
       using std::string;
       using namespace std::string_literals;
       using namespace boost::multiprecision;

       const auto errPrefix = "[ERROR] verify_rsa_sha256_sig: "s;

       try {
          size_t message_len = message.size_bytes();
          size_t signature_len = signature.size_bytes();
          size_t exponent_len = exponent.size_bytes();
          size_t modulus_len = modulus.size_bytes();
          if (message_len && signature_len && exponent_len &&
              modulus_len == signature_len && (modulus_len % 2 == 0))
          {
             fc::sha256 msg_sha256 = context.trx_context.hash_with_checktime<fc::sha256>( message.data(), message.size() );

             auto pkcs1_encoding =
                    "3031300d060960864801650304020105000420"s +
                    fc::to_hex(msg_sha256.data(), msg_sha256.data_size());

             auto emLen = modulus_len / 2;
             auto tLen = pkcs1_encoding.size() / 2;

             if (emLen >= tLen + 11) {
                pkcs1_encoding = "0001"s + string(2*(emLen - tLen - 3), 'f') + "00"s + pkcs1_encoding;

                const cpp_int signature_int { "0x"s + string{signature.data(), signature_len} };
                const cpp_int exponent_int  { "0x"s + string{exponent.data(), exponent_len} };
                const cpp_int modulus_int   { "0x"s + string{modulus.data(), modulus_len} };

                const cpp_int decoded = powm(signature_int, exponent_int, modulus_int);

                return cpp_int{"0x"s + pkcs1_encoding} == decoded;
             }
             else
                context.console_append(errPrefix + "Intended encoding message lenght too short\n");
          }
          else
             context.console_append(errPrefix + "At least 1 param has an invalid length\n");
        }
        catch(const std::exception& e) {
           context.console_append(errPrefix + e.what() + "\n");
        }
        catch(...) {
           context.console_append(errPrefix + "Unknown exception\n");
        }

        return false;
    }
}}} // ns eosio::chain::webassembly
