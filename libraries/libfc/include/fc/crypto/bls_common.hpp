#pragma once
#include <fc/crypto/common.hpp>

namespace fc::crypto::blslib {

   template <typename Container>
   static Container serialize_base64(const std::string& data_str)
   {

      using wrapper = checksummed_data<Container>;

      wrapper wrapped;

      auto bin = fc::base64_decode(data_str);
      fc::datastream<const char*> unpacker(bin.data(), bin.size());
      fc::raw::unpack(unpacker, wrapped);
      FC_ASSERT(!unpacker.remaining(), "decoded base64 length too long");
      auto checksum = wrapper::calculate_checksum(wrapped.data, nullptr);
      FC_ASSERT(checksum == wrapped.check);

      return wrapped.data;
   }

   template <typename Container>
   static std::string deserialize_base64( Container data, const yield_function_t& yield) {

      using wrapper = checksummed_data<Container>;
      
      wrapper wrapped;

      wrapped.data = data;
      yield();
      wrapped.check = wrapper::calculate_checksum(wrapped.data, nullptr);
      yield();
      auto packed = raw::pack( wrapped );
      yield();
      auto data_str = fc::base64_encode( packed.data(), packed.size());
      yield();

      return data_str;
   }

}  // fc::crypto::blslib
