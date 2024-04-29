#pragma once
#include <fc/crypto/common.hpp>

namespace fc::crypto::blslib {

   template <typename Container>
   static Container deserialize_base64url(const std::string& data_str) {
      using wrapper = checksummed_data<Container>;
      wrapper wrapped;

      auto bin = fc::base64url_decode(data_str);
      fc::datastream<const char*> unpacker(bin.data(), bin.size());
      fc::raw::unpack(unpacker, wrapped);
      FC_ASSERT(!unpacker.remaining(), "decoded base64url length too long");
      auto checksum = wrapper::calculate_checksum(wrapped.data, nullptr);
      FC_ASSERT(checksum == wrapped.check);

      return wrapped.data;
   }

   template <typename Container>
   static std::string serialize_base64url(const Container& data) {
      using wrapper = checksummed_data<Container>;
      wrapper wrapped;

      wrapped.data = data;
      wrapped.check = wrapper::calculate_checksum(wrapped.data, nullptr);
      auto packed = raw::pack( wrapped );
      auto data_str = fc::base64url_encode( packed.data(), packed.size());
 
      return data_str;
   }

}  // fc::crypto::blslib
