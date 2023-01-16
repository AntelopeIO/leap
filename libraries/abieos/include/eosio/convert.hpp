#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include "for_each_field.hpp"
#include "stream.hpp"

namespace eosio
{
   struct no_conversion
   {
      using reverse = no_conversion;
   };
   struct widening_conversion;
   // Fields must match exactly
   struct strict_conversion
   {
      using reverse = strict_conversion;
   };
   // Can discard some fields
   struct narrowing_conversion
   {
      using reverse = widening_conversion;
   };
   // Can default construct some fields
   struct widening_conversion
   {
      using reverse = narrowing_conversion;
   };

   no_conversion conversion_kind(...);
   void serialize_as(...);

   template <typename T>
   using serialization_type = decltype(serialize_as(std::declval<T>()));

   template <typename T, typename U>
   using conversion_kind_t = std::conditional_t<
       std::is_same_v<decltype(conversion_kind(std::declval<T>(), std::declval<U>())),
                      no_conversion>,
       typename decltype(conversion_kind(std::declval<U>(), std::declval<T>()))::reverse,
       decltype(conversion_kind(std::declval<T>(), std::declval<U>()))>;

   template <typename Field, typename T, typename U, typename F>
   auto convert_impl(Field field, const T& src, U& dst, F&& f, int)
       -> std::void_t<decltype(field(&src)), decltype(field(&dst))>
   {
      convert(field(&src), field(&dst), f);
   }

   template <typename Field, typename T, typename U, typename F>
   auto convert_impl(Field field, const T& src, U& dst, F&& f, long)
   {
      static_assert(!std::is_same_v<conversion_kind_t<T, U>, strict_conversion>,
                    "Member not found");
      static_assert(!std::is_same_v<conversion_kind_t<T, U>, widening_conversion>,
                    "Member not found");
   }

   inline constexpr auto choose_first = [](auto src, auto dest) { return src; };
   inline constexpr auto choose_second = [](auto src, auto dest) { return dest; };

   // TODO: add some validation

   template <typename T, typename U, typename F>
   void convert(const T& src, U& dst, F&& chooser)
   {
      if constexpr (std::is_same_v<T, U>)
      {
         dst = src;
      }
      else
      {
         static_assert(!std::is_same_v<conversion_kind_t<T, U>, no_conversion>,
                       "Conversion not defined");
         for_each_field<std::decay_t<decltype(*chooser((T*)nullptr, (U*)nullptr))>>(
             [&](const char*, auto field) { convert_impl(field, src, dst, chooser, 0); });
      }
   }

   template <typename... T, typename U, typename F>
   void convert(const std::variant<T...>& src, U& dst, F&& chooser)
   {
      std::visit([&](auto& src) { return convert(src, dst, chooser); }, src);
   }

   template <typename T, typename U, typename F>
   void convert(const std::vector<T>& src, std::vector<U>& dst, F&& chooser)
   {
      dst.resize(src.size());
      for (std::size_t i = 0; i < src.size(); ++i)
      {
         convert(src[i], dst[i], chooser);
      }
   }

   template <typename T, typename U, typename F>
   void convert(const std::optional<T>& src, std::optional<U>& dst, F&& chooser)
   {
      if (src)
      {
         dst.emplace();
         convert(*src, *dst, chooser);
      }
      else
      {
         dst = std::nullopt;
      }
   }

   struct stream;
   template <typename F>
   void convert(const input_stream& src, std::vector<char>& dst, F&& chooser)
   {
      dst.assign(src.pos, src.end);
   }
}  // namespace eosio
