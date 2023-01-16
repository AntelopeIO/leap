#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include "operators.hpp"
#include "reflection.hpp"

namespace eosio
{
   /**
    *  @defgroup public_key Public Key Type
    *  @ingroup core
    *  @ingroup types
    *  @brief Specifies public key type
    */

   /**
    *  EOSIO ECC public key data
    *
    *  Fixed size representation of either a K1 or R1 compressed public key

    *  @ingroup public_key
    */
   using ecc_public_key = std::array<uint8_t, 33>;

   /**
    *  EOSIO WebAuthN public key
    *
    *  @ingroup public_key
    */
   struct webauthn_public_key
   {
      /**
       * Enumeration of the various results of a Test of User Presence
       * @see https://w3c.github.io/webauthn/#test-of-user-presence
       */
      enum class user_presence_t : uint8_t
      {
         USER_PRESENCE_NONE,
         USER_PRESENCE_PRESENT,
         USER_PRESENCE_VERIFIED
      };

      /**
       * The ECC key material
       */
      ecc_public_key key;

      /**
       * expected result of the test of user presence for a valid signature
       * @see https://w3c.github.io/webauthn/#test-of-user-presence
       */
      user_presence_t user_presence;

      /**
       * the Relying Party Identifier for WebAuthN
       * @see https://w3c.github.io/webauthn/#relying-party-identifier
       */
      std::string rpid;
   };
   EOSIO_REFLECT(webauthn_public_key, key, user_presence, rpid);
   EOSIO_COMPARE(webauthn_public_key);

   /**
    *  EOSIO Public Key
    *
    *  A public key is a variant of
    *   0 : a ECC K1 public key
    *   1 : a ECC R1 public key
    *   2 : a WebAuthN public key (requires the host chain to activate the WEBAUTHN_KEY consensus
    * upgrade)
    *
    *  @ingroup public_key
    */
   using public_key = std::variant<ecc_public_key, ecc_public_key, webauthn_public_key>;

   using ecc_private_key = std::array<uint8_t, 32>;
   using private_key = std::variant<ecc_private_key, ecc_private_key>;

   /**
    *  EOSIO ECC signature data
    *
    *  Fixed size representation of either a K1 or R1 ECC compact signature

    *  @ingroup signature
    */
   using ecc_signature = std::array<uint8_t, 65>;

   struct webauthn_signature
   {
      /**
       * The ECC signature data
       */
      ecc_signature compact_signature;

      /**
       * The Encoded Authenticator Data returned from WebAuthN ceremony
       * @see https://w3c.github.io/webauthn/#sctn-authenticator-data
       */
      std::vector<std::uint8_t> auth_data;

      /**
       * the JSON encoded Collected Client Data from a WebAuthN ceremony
       * @see https://w3c.github.io/webauthn/#dictdef-collectedclientdata
       */
      std::string client_json;
   };

   EOSIO_REFLECT(webauthn_signature, compact_signature, auth_data, client_json);
   EOSIO_COMPARE(webauthn_signature);

   using signature = std::variant<ecc_signature, ecc_signature, webauthn_signature>;
   constexpr const char* get_type_name(public_key*) { return "public_key"; }
   constexpr const char* get_type_name(private_key*) { return "private_key"; }
   constexpr const char* get_type_name(signature*) { return "signature"; }

   std::string public_key_to_string(const public_key& obj);
   public_key public_key_from_string(std::string_view s);
   std::string private_key_to_string(const private_key& obj);
   private_key private_key_from_string(std::string_view s);
   std::string signature_to_string(const signature& obj);
   signature signature_from_string(std::string_view s);

   template <typename S>
   void to_json(const public_key& obj, S& stream)
   {
      to_json(public_key_to_string(obj), stream);
   }
   template <typename S>
   void from_json(public_key& obj, S& stream)
   {
      auto s = stream.get_string();
      obj = public_key_from_string(s);
   }
   template <typename S>
   void to_json(const private_key& obj, S& stream)
   {
      to_json(private_key_to_string(obj), stream);
   }
   template <typename S>
   void from_json(private_key& obj, S& stream)
   {
      obj = private_key_from_string(stream.get_string());
   }
   template <typename S>
   void to_json(const signature& obj, S& stream)
   {
      return to_json(signature_to_string(obj), stream);
   }
   template <typename S>
   void from_json(signature& obj, S& stream)
   {
      obj = signature_from_string(stream.get_string());
   }

   std::string to_base58(const uint8_t* d, size_t s);
   std::vector<uint8_t> from_base58(const std::string_view& s);

}  // namespace eosio
