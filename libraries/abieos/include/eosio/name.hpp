#pragma once

#include <string>
#include "chain_conversions.hpp"
#include "check.hpp"
#include "murmur.hpp"
#include "operators.hpp"
#include "reflection.hpp"

namespace eosio
{
   struct name
   {
      enum class raw : uint64_t
      {
      };
      uint64_t value = 0;

      constexpr name() = default;
      constexpr explicit name(uint64_t value) : value{value} {}
      constexpr explicit name(name::raw value) : value{static_cast<uint64_t>(value)} {}
      constexpr explicit name(std::string_view str) : value{string_to_name_strict(str)} {}

      constexpr name(const name&) = default;

      constexpr operator raw() const { return static_cast<raw>(value); }
      explicit operator std::string() const { return eosio::name_to_string(value); }
      std::string to_string() const { return std::string(*this); }
      /**
       * Explicit cast to bool of the uint64_t value of the name
       *
       * @return Returns true if the name is set to the default value of 0 else true.
       */
      constexpr explicit operator bool() const { return value != 0; }

      /**
       *  Converts a %name Base32 symbol into its corresponding value
       *
       *  @param c - Character to be converted
       *  @return constexpr char - Converted value
       */
      static constexpr uint8_t char_to_value(char c)
      {
         if (c == '.')
            return 0;
         else if (c >= '1' && c <= '5')
            return (c - '1') + 1;
         else if (c >= 'a' && c <= 'z')
            return (c - 'a') + 6;
         else
            eosio::check(false, "character is not in allowed character set for names");

         return 0;  // control flow will never reach here; just added to suppress warning
      }

      /**
       *  Returns the length of the %name
       */
      constexpr uint8_t length() const
      {
         constexpr uint64_t mask = 0xF800000000000000ull;

         if (value == 0)
            return 0;

         uint8_t l = 0;
         uint8_t i = 0;
         for (auto v = value; i < 13; ++i, v <<= 5)
         {
            if ((v & mask) > 0)
            {
               l = i;
            }
         }

         return l + 1;
      }

      /**
       *  Returns the suffix of the %name
       */
      constexpr name suffix() const
      {
         uint32_t remaining_bits_after_last_actual_dot = 0;
         uint32_t tmp = 0;
         for (int32_t remaining_bits = 59; remaining_bits >= 4; remaining_bits -= 5)
         {  // Note: remaining_bits must remain signed integer
            // Get characters one-by-one in name in order from left to right (not including the 13th
            // character)
            auto c = (value >> remaining_bits) & 0x1Full;
            if (!c)
            {  // if this character is a dot
               tmp = static_cast<uint32_t>(remaining_bits);
            }
            else
            {  // if this character is not a dot
               remaining_bits_after_last_actual_dot = tmp;
            }
         }

         uint64_t thirteenth_character = value & 0x0Full;
         if (thirteenth_character)
         {  // if 13th character is not a dot
            remaining_bits_after_last_actual_dot = tmp;
         }

         if (remaining_bits_after_last_actual_dot ==
             0)  // there is no actual dot in the %name other than potentially leading dots
            return name{value};

         // At this point remaining_bits_after_last_actual_dot has to be within the range of 4 to 59
         // (and restricted to increments of 5).

         // Mask for remaining bits corresponding to characters after last actual dot, except for 4
         // least significant bits (corresponds to 13th character).
         uint64_t mask = (1ull << remaining_bits_after_last_actual_dot) - 16;
         uint32_t shift = 64 - remaining_bits_after_last_actual_dot;

         return name{((value & mask) << shift) + (thirteenth_character << (shift - 1))};
      }

      /**
       *  Returns the prefix of the %name
       */
      constexpr name prefix() const
      {
         uint64_t result = value;
         bool not_dot_character_seen = false;
         uint64_t mask = 0xFull;

         // Get characters one-by-one in name in order from right to left
         for (int32_t offset = 0; offset <= 59;)
         {
            auto c = (value >> offset) & mask;

            if (!c)
            {  // if this character is a dot
               if (not_dot_character_seen)
               {  // we found the rightmost dot character
                  result = (value >> offset) << offset;
                  break;
               }
            }
            else
            {
               not_dot_character_seen = true;
            }

            if (offset == 0)
            {
               offset += 4;
               mask = 0x1Full;
            }
            else
            {
               offset += 5;
            }
         }

         return name{result};
      }
   };

   // TODO this seems weird and the name is misleading
   // and I don't think this has ever been truly constexpr
   inline constexpr uint64_t hash_name(std::string_view str)
   {
      auto r = try_string_to_name_strict(str);
      if (r)
         return r.value();
      return murmur64(str.data(), str.size());
   }

   EOSIO_REFLECT(name, value);
   EOSIO_COMPARE(name);

   template <typename S>
   void from_string(name& obj, S& stream)
   {
      obj = name(hash_name(std::string_view{stream.pos, stream.remaining()}));
   }

   template <typename S>
   void from_json(name& obj, S& stream)
   {
      auto r = stream.get_string();
      obj = name(hash_name(r));
   }

   template <typename S>
   void to_json(const name& obj, S& stream)
   {
      to_json(eosio::name_to_string(obj.value), stream);
   }

   inline namespace literals
   {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-string-literal-operator-template"
      template <typename T, T... Str>
      inline constexpr name operator""_n()
      {
         return name(string_to_name_strict<Str...>());
      }
      inline constexpr name operator""_h(const char* s, size_t) { return name(hash_name(s)); }
#pragma clang diagnostic pop
   }  // namespace literals

}  // namespace eosio
