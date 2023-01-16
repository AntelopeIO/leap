#pragma once

#include "chain_conversions.hpp"
#include "check.hpp"
#include "reflection.hpp"
#include "symbol.hpp"

#include <limits>
#include <tuple>

namespace eosio
{
   char* write_decimal(char* begin,
                       char* end,
                       bool dry_run,
                       uint64_t number,
                       uint8_t num_decimal_places,
                       bool negative);

   /**
    *  @defgroup asset Asset
    *  @ingroup core
    *  @brief Defines C++ API for managing assets
    */

   struct no_check_t
   {
   };

   inline constexpr no_check_t no_check;

   /**
    *  Stores information for owner of asset
    *
    *  @ingroup asset
    */
   struct asset
   {
      /**
       * The amount of the asset
       */
      int64_t amount = 0;

      /**
       * The symbol name of the asset
       */
      eosio::symbol symbol;

      /**
       * Maximum amount possible for this asset. It's capped to 2^62 - 1
       */
      static constexpr int64_t max_amount = (1LL << 62) - 1;

      constexpr asset() : symbol{} {}

      /**
       * Construct a new asset given the symbol name and the amount
       *
       * @param a - The amount of the asset
       * @param s - The name of the symbol
       */
      asset(int64_t a, class symbol s) : amount(a), symbol{s}
      {
         eosio::check(is_amount_within_range(), "magnitude of asset amount must be less than 2^62");
         eosio::check(symbol.is_valid(), "invalid symbol name");
      }

      constexpr asset(int64_t a, class symbol s, no_check_t) : amount(a), symbol{s} {}

      constexpr asset(std::string_view s, no_check_t)
      {
         input_stream stream{s};
         (void)eosio::string_to_asset(amount, symbol.value, stream.pos, stream.end, false);
      }

      /**
       * Check if the amount doesn't exceed the max amount
       *
       * @return true - if the amount doesn't exceed the max amount
       * @return false - otherwise
       */
      bool is_amount_within_range() const { return -max_amount <= amount && amount <= max_amount; }

      /**
       * Check if the asset is valid. %A valid asset has its amount <= max_amount and its symbol
       * name valid
       *
       * @return true - if the asset is valid
       * @return false - otherwise
       */
      bool is_valid() const { return is_amount_within_range() && symbol.is_valid(); }

      /**
       * Set the amount of the asset
       *
       * @param a - New amount for the asset
       */
      void set_amount(int64_t a)
      {
         amount = a;
         eosio::check(is_amount_within_range(), "magnitude of asset amount must be less than 2^62");
      }

      /// @cond OPERATORS

      /**
       * Unary minus operator
       *
       * @return asset - New asset with its amount is the negative amount of this asset
       */
      asset operator-() const
      {
         asset r = *this;
         r.amount = -r.amount;
         return r;
      }

      /**
       * Subtraction assignment operator
       *
       * @param a - Another asset to subtract this asset with
       * @return asset& - Reference to this asset
       * @post The amount of this asset is subtracted by the amount of asset a
       */
      asset& operator-=(const asset& a)
      {
         eosio::check(a.symbol == symbol, "attempt to subtract asset with different symbol");
         amount -= a.amount;
         eosio::check(-max_amount <= amount, "subtraction underflow");
         eosio::check(amount <= max_amount, "subtraction overflow");
         return *this;
      }

      /**
       * Addition Assignment  operator
       *
       * @param a - Another asset to subtract this asset with
       * @return asset& - Reference to this asset
       * @post The amount of this asset is added with the amount of asset a
       */
      asset& operator+=(const asset& a)
      {
         eosio::check(a.symbol == symbol, "attempt to add asset with different symbol");
         amount += a.amount;
         eosio::check(-max_amount <= amount, "addition underflow");
         eosio::check(amount <= max_amount, "addition overflow");
         return *this;
      }

      /**
       * Addition operator
       *
       * @param a - The first asset to be added
       * @param b - The second asset to be added
       * @return asset - New asset as the result of addition
       */
      inline friend asset operator+(const asset& a, const asset& b)
      {
         asset result = a;
         result += b;
         return result;
      }

      /**
       * Subtraction operator
       *
       * @param a - The asset to be subtracted
       * @param b - The asset used to subtract
       * @return asset - New asset as the result of subtraction of a with b
       */
      inline friend asset operator-(const asset& a, const asset& b)
      {
         asset result = a;
         result -= b;
         return result;
      }

      /**
       * Multiplication assignment operator, with a number
       *
       * @details Multiplication assignment operator. Multiply the amount of this asset with a
       * number and then assign the value to itself.
       * @param a - The multiplier for the asset's amount
       * @return asset - Reference to this asset
       * @post The amount of this asset is multiplied by a
       */
#ifndef ABIEOS_NO_INT128
      asset& operator*=(int64_t a)
      {
         __int128 tmp = (__int128)amount * (__int128)a;
         eosio::check(tmp <= max_amount, "multiplication overflow");
         eosio::check(tmp >= -max_amount, "multiplication underflow");
         amount = (int64_t)tmp;
         return *this;
      }
#endif

      /**
       * Multiplication operator, with a number proceeding
       *
       * @brief Multiplication operator, with a number proceeding
       * @param a - The asset to be multiplied
       * @param b - The multiplier for the asset's amount
       * @return asset - New asset as the result of multiplication
       */
#ifndef ABIEOS_NO_INT128
      friend asset operator*(const asset& a, int64_t b)
      {
         asset result = a;
         result *= b;
         return result;
      }
#endif

      /**
       * Multiplication operator, with a number preceeding
       *
       * @param a - The multiplier for the asset's amount
       * @param b - The asset to be multiplied
       * @return asset - New asset as the result of multiplication
       */
#ifndef ABIEOS_NO_INT128
      friend asset operator*(int64_t b, const asset& a)
      {
         asset result = a;
         result *= b;
         return result;
      }
#endif

      /**
       * @brief Division assignment operator, with a number
       *
       * @details Division assignment operator. Divide the amount of this asset with a number and
       * then assign the value to itself.
       * @param a - The divisor for the asset's amount
       * @return asset - Reference to this asset
       * @post The amount of this asset is divided by a
       */
      asset& operator/=(int64_t a)
      {
         eosio::check(a != 0, "divide by zero");
         eosio::check(!(amount == std::numeric_limits<int64_t>::min() && a == -1),
                      "signed division overflow");
         amount /= a;
         return *this;
      }

      /**
       * Division operator, with a number proceeding
       *
       * @param a - The asset to be divided
       * @param b - The divisor for the asset's amount
       * @return asset - New asset as the result of division
       */
      friend asset operator/(const asset& a, int64_t b)
      {
         asset result = a;
         result /= b;
         return result;
      }

      /**
       * Division operator, with another asset
       *
       * @param a - The asset which amount acts as the dividend
       * @param b - The asset which amount acts as the divisor
       * @return int64_t - the resulted amount after the division
       * @pre Both asset must have the same symbol
       */
      friend int64_t operator/(const asset& a, const asset& b)
      {
         eosio::check(b.amount != 0, "divide by zero");
         eosio::check(a.symbol == b.symbol,
                      "comparison of assets with different symbols is not allowed");
         return a.amount / b.amount;
      }

      /**
       * Equality operator
       *
       * @param a - The first asset to be compared
       * @param b - The second asset to be compared
       * @return true - if both asset has the same amount
       * @return false - otherwise
       * @pre Both asset must have the same symbol
       */
      friend bool operator==(const asset& a, const asset& b)
      {
         eosio::check(a.symbol == b.symbol,
                      "comparison of assets with different symbols is not allowed");
         return a.amount == b.amount;
      }

      /**
       * Inequality operator
       *
       * @param a - The first asset to be compared
       * @param b - The second asset to be compared
       * @return true - if both asset doesn't have the same amount
       * @return false - otherwise
       * @pre Both asset must have the same symbol
       */
      friend bool operator!=(const asset& a, const asset& b) { return !(a == b); }

      /**
       * Less than operator
       *
       * @param a - The first asset to be compared
       * @param b - The second asset to be compared
       * @return true - if the first asset's amount is less than the second asset amount
       * @return false - otherwise
       * @pre Both asset must have the same symbol
       */
      friend bool operator<(const asset& a, const asset& b)
      {
         eosio::check(a.symbol == b.symbol,
                      "comparison of assets with different symbols is not allowed");
         return a.amount < b.amount;
      }

      /**
       * Less or equal to operator
       *
       * @param a - The first asset to be compared
       * @param b - The second asset to be compared
       * @return true - if the first asset's amount is less or equal to the second asset amount
       * @return false - otherwise
       * @pre Both asset must have the same symbol
       */
      friend bool operator<=(const asset& a, const asset& b)
      {
         eosio::check(a.symbol == b.symbol,
                      "comparison of assets with different symbols is not allowed");
         return a.amount <= b.amount;
      }

      /**
       * Greater than operator
       *
       * @param a - The first asset to be compared
       * @param b - The second asset to be compared
       * @return true - if the first asset's amount is greater than the second asset amount
       * @return false - otherwise
       * @pre Both asset must have the same symbol
       */
      friend bool operator>(const asset& a, const asset& b)
      {
         eosio::check(a.symbol == b.symbol,
                      "comparison of assets with different symbols is not allowed");
         return a.amount > b.amount;
      }

      /**
       * Greater or equal to operator
       *
       * @param a - The first asset to be compared
       * @param b - The second asset to be compared
       * @return true - if the first asset's amount is greater or equal to the second asset amount
       * @return false - otherwise
       * @pre Both asset must have the same symbol
       */
      friend bool operator>=(const asset& a, const asset& b)
      {
         eosio::check(a.symbol == b.symbol,
                      "comparison of assets with different symbols is not allowed");
         return a.amount >= b.amount;
      }

      /// @endcond

      /**
       * %asset to std::string
       *
       * @brief %asset to std::string
       */
      std::string to_string() const { return asset_to_string(amount, symbol.value); }
   };

   EOSIO_REFLECT(asset, amount, symbol);

   template <typename S>
   inline void from_string(asset& result, S& stream)
   {
      int64_t amount;
      uint64_t sym;
      check(eosio::string_to_asset(amount, sym, stream.pos, stream.end, true),
            convert_stream_error(eosio::stream_error::invalid_asset_format));
      result = asset{amount, symbol{sym}};
   }

   template <typename S>
   void to_json(const asset& obj, S& stream)
   {
      to_json(asset_to_string(obj.amount, obj.symbol.value), stream);
   }

   template <typename S>
   void from_json(asset& obj, S& stream)
   {
      auto s = stream.get_string();
      check(string_to_asset(obj.amount, obj.symbol.value, s.data(), s.data() + s.size()),
            convert_json_error(eosio::from_json_error::expected_symbol_code));
   }

   /**
    *  Extended asset which stores the information of the owner of the asset
    *
    *  @ingroup asset
    */
   struct extended_asset
   {
      /**
       * The asset
       */
      asset quantity;

      /**
       * The owner of the asset
       */
      name contract;

      /**
       * Get the extended symbol of the asset
       *
       * @return extended_symbol - The extended symbol of the asset
       */
      extended_symbol get_extended_symbol() const
      {
         return extended_symbol{quantity.symbol, contract};
      }

      /**
       * Default constructor
       */
      extended_asset() = default;

      /**
       * Construct a new extended asset given the amount and extended symbol
       */
      extended_asset(int64_t v, extended_symbol s)
          : quantity(v, s.get_symbol()), contract(s.get_contract())
      {
      }
      /**
       * Construct a new extended asset given the asset and owner name
       */
      extended_asset(asset a, name c) : quantity(a), contract(c) {}

      /// @cond OPERATORS

      // Unary minus operator
      extended_asset operator-() const { return {-quantity, contract}; }

      // Subtraction operator
      friend extended_asset operator-(const extended_asset& a, const extended_asset& b)
      {
         eosio::check(a.contract == b.contract, "type mismatch");
         return {a.quantity - b.quantity, a.contract};
      }

      // Addition operator
      friend extended_asset operator+(const extended_asset& a, const extended_asset& b)
      {
         eosio::check(a.contract == b.contract, "type mismatch");
         return {a.quantity + b.quantity, a.contract};
      }

      /// Addition operator.
      friend extended_asset& operator+=(extended_asset& a, const extended_asset& b)
      {
         eosio::check(a.contract == b.contract, "type mismatch");
         a.quantity += b.quantity;
         return a;
      }

      /// Subtraction operator.
      friend extended_asset& operator-=(extended_asset& a, const extended_asset& b)
      {
         eosio::check(a.contract == b.contract, "type mismatch");
         a.quantity -= b.quantity;
         return a;
      }

      /// Less than operator
      friend bool operator<(const extended_asset& a, const extended_asset& b)
      {
         eosio::check(a.contract == b.contract, "type mismatch");
         return a.quantity < b.quantity;
      }

      /// Comparison operator
      friend bool operator==(const extended_asset& a, const extended_asset& b)
      {
         return std::tie(a.quantity, a.contract) == std::tie(b.quantity, b.contract);
      }

      /// Comparison operator
      friend bool operator!=(const extended_asset& a, const extended_asset& b)
      {
         return std::tie(a.quantity, a.contract) != std::tie(b.quantity, b.contract);
      }

      /// Comparison operator
      friend bool operator<=(const extended_asset& a, const extended_asset& b)
      {
         eosio::check(a.contract == b.contract, "type mismatch");
         return a.quantity <= b.quantity;
      }

      /// Comparison operator
      friend bool operator>=(const extended_asset& a, const extended_asset& b)
      {
         eosio::check(a.contract == b.contract, "type mismatch");
         return a.quantity >= b.quantity;
      }

      std::string to_string() const { return quantity.to_string() + "@" + contract.to_string(); }
      /// @endcond
   };

   EOSIO_REFLECT(extended_asset, quantity, contract);
}  // namespace eosio
