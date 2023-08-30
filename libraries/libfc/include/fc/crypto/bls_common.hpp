#pragma once
#include <fc/crypto/common.hpp>

namespace fc::crypto::blslib {

   template <typename Container>
   static Container serialize_base58(const std::string& data_str)
   {

      using wrapper = checksummed_data<Container>;

      wrapper wrapped;

      auto bin = fc::from_base58(data_str);
      fc::datastream<const char*> unpacker(bin.data(), bin.size());
      fc::raw::unpack(unpacker, wrapped);
      FC_ASSERT(!unpacker.remaining(), "decoded base58 length too long");
      auto checksum = wrapper::calculate_checksum(wrapped.data, nullptr);
      FC_ASSERT(checksum == wrapped.check);

      return wrapped.data;
   }

   template <typename Container>
   static std::string deserialize_base58( Container data, const yield_function_t& yield) {

      using wrapper = checksummed_data<Container>;
      
      wrapper wrapped;

      wrapped.data = data;
      yield();
      wrapped.check = wrapper::calculate_checksum(wrapped.data, nullptr);
      yield();
      auto packed = raw::pack( wrapped );
      yield();
      auto data_str = to_base58( packed.data(), packed.size(), yield );
      yield();

      return data_str;
   }

}  // fc::crypto::blslib
