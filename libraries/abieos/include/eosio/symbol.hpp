/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#pragma once

#include "chain_conversions.hpp"
#include "check.hpp"
#include "from_json.hpp"
#include "name.hpp"
#include "operators.hpp"
#include "reflection.hpp"

#include <limits>
#include <string_view>
#include <tuple>

namespace eosio
{
   /**
    *  @defgroup symbol Symbol
    *  @ingroup core
    *  @brief Defines C++ API for managing symbols
    */

   /**
    *  Stores the symbol code as a uint64_t value
    *
    *  @ingroup symbol
    */
   class symbol_code
   {
     public:
      /**
       * Default constructor, construct a new symbol_code
       *
       * @brief Construct a new symbol_code object defaulting to a value of 0
       *
       */
      constexpr symbol_code() : value(0) {}

      /**
       * Construct a new symbol_code given a scoped enumerated type of raw (uint64_t).
       *
       * @brief Construct a new symbol_code object initialising value with raw
       * @param raw - The raw value which is a scoped enumerated type of unit64_t
       *
       */
      constexpr explicit symbol_code(uint64_t raw) : value(raw) {}

      /**
       * Construct a new symbol_code given an string.
       *
       * @brief Construct a new symbol_code object initialising value with str
       * @param str - The string value which validated then converted to unit64_t
       *
       */
      constexpr explicit symbol_code(std::string_view str) : value(0)
      {
         if (str.size() > 7)
         {
            eosio::check(false, "string is too long to be a valid symbol_code");
         }
         for (auto itr = str.rbegin(); itr != str.rend(); ++itr)
         {
            if (*itr < 'A' || *itr > 'Z')
            {
               eosio::check(false, "only uppercase letters allowed in symbol_code string");
            }
            value <<= 8;
            value |= *itr;
         }
      }

      /**
       * Checks if the symbol code is valid
       * @return true - if symbol is valid
       */
      constexpr bool is_valid() const
      {
         auto sym = value;
         for (int i = 0; i < 7; i++)
         {
            char c = (char)(sym & 0xFF);
            if (!('A' <= c && c <= 'Z'))
               return false;
            sym >>= 8;
            if (!(sym & 0xFF))
            {
               do
               {
                  sym >>= 8;
                  if ((sym & 0xFF))
                     return false;
                  i++;
               } while (i < 7);
            }
         }
         return true;
      }

      /**
       * Returns the character length of the provided symbol
       *
       * @return length - character length of the provided symbol
       */
      constexpr uint32_t length() const
      {
         auto sym = value;
         uint32_t len = 0;
         while (sym & 0xFF && len <= 7)
         {
            len++;
            sym >>= 8;
         }
         return len;
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
       * Casts a symbol code to raw
       *
       * @return Returns an instance of raw based on the value of a symbol_code
       */
      constexpr uint64_t raw() const { return value; }

      /**
       * Explicit cast to bool of the symbol_code
       *
       * @return Returns true if the symbol_code is set to the default value of 0 else true.
       */
      constexpr explicit operator bool() const { return value != 0; }

      /**
       *  Returns the name value as a string by calling write_as_string() and returning the buffer
       * produced by write_as_string()
       */
      std::string to_string() const { return symbol_code_to_string(value); }

      uint64_t value = 0;
   };

   EOSIO_REFLECT(symbol_code, value);
   EOSIO_COMPARE(symbol_code);

   template <typename S>
   void to_json(const symbol_code& obj, S& stream)
   {
      to_json(symbol_code_to_string(obj.value), stream);
   }

   template <typename S>
   void from_json(symbol_code& obj, S& stream)
   {
      auto s = stream.get_string();
      check(string_to_symbol_code(obj.value, s.data(), s.data() + s.size()),
            convert_json_error(eosio::from_json_error::expected_symbol_code));
   }

   /**
    *  Stores information about a symbol, the symbol can be 7 characters long.
    *
    *  @ingroup symbol
    */
   class symbol
   {
     public:
      /**
       * Construct a new symbol object defaulting to a value of 0
       */
      constexpr symbol() : value(0) {}

      /**
       * Construct a new symbol given a scoped enumerated type of raw (uint64_t).
       *
       * @param raw - The raw value which is a scoped enumerated type of unit64_t
       */
      constexpr explicit symbol(uint64_t raw) : value(raw) {}

      /**
       * Construct a new symbol given a symbol_code and a uint8_t precision.
       *
       * @param sc - The symbol_code
       * @param precision - The number of decimal places used for the symbol
       */
      constexpr symbol(symbol_code sc, uint8_t precision)
          : value((sc.raw() << 8) | static_cast<uint64_t>(precision))
      {
      }

      /**
       * Construct a new symbol given a string and a uint8_t precision.
       *
       * @param ss - The string containing the symbol
       * @param precision - The number of decimal places used for the symbol
       */
      constexpr symbol(std::string_view ss, uint8_t precision)
          : value((symbol_code(ss).raw() << 8) | static_cast<uint64_t>(precision))
      {
      }

      /**
       * Is this symbol valid
       */
      constexpr bool is_valid() const { return code().is_valid(); }

      /**
       * This symbol's precision
       */
      constexpr uint8_t precision() const { return static_cast<uint8_t>(value & 0xFFull); }

      /**
       * Returns representation of symbol name
       */
      constexpr symbol_code code() const { return symbol_code{value >> 8}; }

      /**
       * Returns uint64_t repreresentation of the symbol
       */
      constexpr uint64_t raw() const { return value; }

      constexpr explicit operator bool() const { return value != 0; }

      std::string to_string() const { return symbol_to_string(value); }

      uint64_t value = 0;
   };

   EOSIO_REFLECT(symbol, value);
   EOSIO_COMPARE(symbol);

   template <typename S>
   void to_json(const symbol& obj, S& stream)
   {
      to_json(symbol_to_string(obj.value), stream);
   }

   template <typename S>
   void from_json(symbol& obj, S& stream)
   {
      auto s = stream.get_string();
      check(string_to_symbol(obj.value, s.data(), s.data() + s.size()),
            convert_json_error(eosio::from_json_error::expected_symbol));
   }

   /**
    *  Extended asset which stores the information of the owner of the symbol
    *
    *  @ingroup symbol
    */
   class extended_symbol
   {
     public:
      /**
       * Default constructor, construct a new extended_symbol
       */
      constexpr extended_symbol() {}

      /**
       * Construct a new symbol_code object initialising symbol and contract with the passed in
       * symbol and name
       *
       * @param sym - The symbol
       * @param con - The name of the contract
       */
      constexpr extended_symbol(symbol s, name con) : sym(s), contract(con) {}

      /**
       * Returns the symbol in the extended_contract
       *
       * @return symbol
       */
      constexpr symbol get_symbol() const { return sym; }

      /**
       * Returns the name of the contract in the extended_symbol
       *
       * @return name
       */
      constexpr name get_contract() const { return contract; }

      symbol sym;     ///< the symbol
      name contract;  ///< the token contract hosting the symbol
   };

   EOSIO_REFLECT(extended_symbol, sym, contract);
   EOSIO_COMPARE(extended_symbol);
}  // namespace eosio
