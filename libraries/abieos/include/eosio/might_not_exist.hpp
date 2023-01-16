#pragma once

namespace eosio
{
   template <typename T>
   struct might_not_exist
   {
      T value{};
   };

   template <typename T, typename S>
   void from_bin(might_not_exist<T>& obj, S& stream)
   {
      if (stream.remaining())
         return from_bin(obj.value, stream);
   }

   template <typename T, typename S>
   void to_bin(const might_not_exist<T>& obj, S& stream)
   {
      return to_bin(obj.value, stream);
   }

   template <typename T, typename S>
   void from_json(might_not_exist<T>& obj, S& stream)
   {
      return from_json(obj.value, stream);
   }

   template <typename T, typename S>
   void to_json(const might_not_exist<T>& val, S& stream)
   {
      return to_json(val.value, stream);
   }
}  // namespace eosio
