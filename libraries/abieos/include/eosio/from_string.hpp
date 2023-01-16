#pragma once

#include <string_view>
#include "stream.hpp"

namespace eosio
{
   // TODO: replace stream interface from from_string(obj, stream) with string_view

   template <typename T, typename S>
   T from_string(S& stream)
   {
      T obj;
      from_string(obj, stream);
      return obj;
   }

   template <typename T>
   void convert_from_string(T& obj, std::string_view s)
   {
      input_stream stream{s};
      from_string(obj, stream);
   }

   template <typename T>
   T convert_from_string(std::string_view s)
   {
      T obj;
      convert_from_string(obj, s);
      return obj;
   }

}  // namespace eosio
