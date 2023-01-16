#pragma once

#include <stdint.h>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "stream.hpp"

namespace eosio
{
   // TODO remove in c++20
   namespace
   {
      using days =
          std::chrono::duration<int,
                                std::ratio_multiply<std::ratio<24>, std::chrono::hours::period>>;

      using weeks = std::chrono::duration<int, std::ratio_multiply<std::ratio<7>, days::period>>;

      using years =
          std::chrono::duration<int, std::ratio_multiply<std::ratio<146097, 400>, days::period>>;

      using months = std::chrono::duration<int, std::ratio_divide<years::period, std::ratio<12>>>;

      struct day
      {
         inline explicit day(uint32_t d) : d(d) {}
         uint32_t d;
      };
      struct month
      {
         inline explicit month(uint32_t m) : m(m) {}
         uint32_t m;
      };
      struct month_day
      {
         inline month_day(eosio::month m, eosio::day d) : m(m), d(d) {}
         inline auto month() const { return m; }
         inline auto day() const { return d; }
         struct month m;
         struct day d;
      };
      struct year
      {
         inline explicit year(uint32_t y) : y(y) {}
         uint32_t y;
      };

      template <class Duration>
      using sys_time = std::chrono::time_point<std::chrono::system_clock, Duration>;

      using sys_days = sys_time<days>;
      using sys_seconds = sys_time<std::chrono::seconds>;

      typedef year year_t;
      typedef month month_t;
      typedef day day_t;
      struct year_month_day
      {
         inline auto from_days(days ds)
         {
            const auto z = ds.count() + 719468;
            const auto era = (z >= 0 ? z : z - 146096) / 146097;
            const auto doe = static_cast<uint32_t>(z - era * 146097);
            const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const auto y = static_cast<days::rep>(yoe) + era * 400;
            const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const auto mp = (5 * doy + 2) / 153;
            const auto d = doy - (153 * mp + 2) / 5 + 1;
            const auto m = mp < 10 ? mp + 3 : mp - 9;
            return year_month_day{year_t{static_cast<uint32_t>(y + (m <= 2))}, month_t(m),
                                  day_t(d)};
         }
         inline auto to_days() const
         {
            const auto _y = static_cast<int>(y.y) - (m.m <= month_t{2}.m);
            const auto _m = static_cast<uint32_t>(m.m);
            const auto _d = static_cast<uint32_t>(d.d);
            const auto era = (_y >= 0 ? _y : _y - 399) / 400;
            const auto yoe = static_cast<uint32_t>(_y - era * 400);
            const auto doy = (153 * (_m > 2 ? _m - 3 : _m + 9) + 2) / 5 + _d - 1;
            const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return days{era * 146097 + static_cast<int>(doe) - 719468};
         }
         inline year_month_day(const year_t& y, const month_t& m, const day_t& d) : y(y), m(m), d(d)
         {
         }
         inline year_month_day(const year_month_day&) = default;
         inline year_month_day(year_month_day&&) = default;
         inline year_month_day(sys_days ds) : year_month_day(from_days(ds.time_since_epoch())) {}
         inline auto year() const { return y.y; }
         inline auto month() const { return m.m; }
         inline auto day() const { return d.d; }
         year_t y;
         month_t m;
         day_t d;
      };
   }  // namespace

   inline constexpr uint64_t char_to_name_digit(char c)
   {
      if (c >= 'a' && c <= 'z')
         return (c - 'a') + 6;
      if (c >= '1' && c <= '5')
         return (c - '1') + 1;
      return 0;
   }

   inline constexpr uint64_t string_to_name(const char* str, int size)
   {
      uint64_t name = 0;
      int i = 0;
      for (; i < size && i < 12; ++i)
         name |= (char_to_name_digit(str[i]) & 0x1f) << (64 - 5 * (i + 1));
      if (i < size)
         name |= char_to_name_digit(str[i]) & 0x0F;
      return name;
   }

   inline constexpr uint64_t string_to_name(const char* str)
   {
      int len = 0;
      while (str[len])
         ++len;
      return string_to_name(str, len);
   }

   inline constexpr uint64_t string_to_name(std::string_view str)
   {
      return string_to_name(str.data(), str.size());
   }

   inline uint64_t string_to_name(const std::string& str)
   {
      return string_to_name(str.data(), str.size());
   }

   constexpr inline bool is_valid_char(char c)
   {
      return (c >= 'a' && c <= 'z') || (c >= '1' && c <= '5') || (c == '.');
   }

   template <char C>
   constexpr inline uint64_t char_to_name_digit_strict()
   {
      static_assert(is_valid_char(C), "character is not for an eosio name");
      if constexpr (C >= 'a' && C <= 'z')
         return (C - 'a') + 6;
      else if constexpr (C >= '1' && C <= '5')
         return (C - '1') + 1;
      else if constexpr (C == '.')
         return 0;
   }

   [[nodiscard]] inline constexpr bool char_to_name_digit_strict(char c, uint64_t& result)
   {
      if (c >= 'a' && c <= 'z')
      {
         result = (c - 'a') + 6;
         return true;
      }
      if (c >= '1' && c <= '5')
      {
         result = (c - '1') + 1;
         return true;
      }
      if (c == '.')
      {
         result = 0;
         return true;
      }
      else
      {
         return false;
      }
   }

   template <std::size_t N, uint64_t ValueSoFar, char C, char... Rest>
   constexpr inline uint64_t string_to_name_strict_impl()
   {
      if constexpr (N == 12)
         static_assert((char_to_name_digit_strict<C>() & 0xf) == char_to_name_digit_strict<C>(),
                       "eosio name 13th character cannot be a letter after j");
      if constexpr (sizeof...(Rest) > 0)
         return string_to_name_strict_impl<
             N + 1, ValueSoFar | (char_to_name_digit_strict<C>() & 0x1f) << (64 - 5 * (N + 1)),
             Rest...>();
      else
         return ValueSoFar | (char_to_name_digit_strict<C>() & 0x1f)
                                 << (64 + (N == 12) - 5 * (N + 1));
   }

   template <char... Str>
   constexpr inline uint64_t string_to_name_strict()
   {
      static_assert(sizeof...(Str) <= 13, "eosio name string is too long");
      if constexpr (sizeof...(Str) == 0)
         return 0;
      else
         return string_to_name_strict_impl<0, 0, Str...>();
   }

   // std::optional is killing constexpr'ness
   namespace detail
   {
      struct simple_optional
      {
         explicit constexpr inline simple_optional(stream_error e) : valid(e) {}
         explicit constexpr inline simple_optional(uint64_t v) : val(v) {}
         explicit constexpr inline operator bool() const { return valid == stream_error::no_error; }
         constexpr inline auto value() const { return val; }
         stream_error valid = stream_error::no_error;
         uint64_t val = 0;
      };
   }  // namespace detail

   [[nodiscard]] constexpr inline detail::simple_optional try_string_to_name_strict(
       std::string_view str)
   {
      uint64_t name = 0;
      unsigned i = 0;
      for (; i < str.size() && i < 12; ++i)
      {
         uint64_t x = 0;
         if (!char_to_name_digit_strict(str[i], x))
            return detail::simple_optional{stream_error::invalid_name_char};
         name |= (x & 0x1f) << (64 - 5 * (i + 1));
      }
      if (i < str.size() && i == 12)
      {
         uint64_t x = 0;
         if (!char_to_name_digit_strict(str[i], x))
            return detail::simple_optional{stream_error::invalid_name_char};

         if (x != (x & 0xf))
            return detail::simple_optional{stream_error::invalid_name_char13};
         name |= x;
         ++i;
      }
      if (i < str.size())
         return detail::simple_optional{stream_error::name_too_long};
      return detail::simple_optional{name};
   }

   constexpr inline uint64_t string_to_name_strict(std::string_view str)
   {
      if (auto r = try_string_to_name_strict(str))
         return r.val;
      else
         check(false, convert_stream_error(r.valid));
      __builtin_unreachable();
   }

   inline std::string name_to_string(uint64_t name)
   {
      static const char* charmap = ".12345abcdefghijklmnopqrstuvwxyz";
      std::string str(13, '.');

      uint64_t tmp = name;
      for (uint32_t i = 0; i <= 12; ++i)
      {
         char c = charmap[tmp & (i == 0 ? 0x0f : 0x1f)];
         str[12 - i] = c;
         tmp >>= (i == 0 ? 4 : 5);
      }

      const auto last = str.find_last_not_of('.');
      return str.substr(0, last + 1);
   }

   inline std::string microseconds_to_str(uint64_t microseconds)
   {
      std::string result;

      auto append_uint = [&result](uint32_t value, int digits) {
         char s[20];
         char* ch = s;
         while (digits--)
         {
            *ch++ = '0' + (value % 10);
            value /= 10;
         };
         std::reverse(s, ch);
         result.insert(result.end(), s, ch);
      };

      std::chrono::microseconds us{microseconds};
      sys_days sd(std::chrono::floor<days>(us));
      auto ymd = year_month_day{sd};
      uint32_t ms =
          (std::chrono::floor<std::chrono::milliseconds>(us) - sd.time_since_epoch()).count();
      us -= sd.time_since_epoch();
      append_uint((int)ymd.year(), 4);
      result.push_back('-');
      append_uint((unsigned)ymd.month(), 2);
      result.push_back('-');
      append_uint((unsigned)ymd.day(), 2);
      result.push_back('T');
      append_uint(ms / 3600000 % 60, 2);
      result.push_back(':');
      append_uint(ms / 60000 % 60, 2);
      result.push_back(':');
      append_uint(ms / 1000 % 60, 2);
      result.push_back('.');
      append_uint(ms % 1000, 3);
      return result;
   }

   [[nodiscard]] inline bool string_to_utc_seconds(uint32_t& result,
                                                   const char*& s,
                                                   const char* end,
                                                   bool eat_fractional,
                                                   bool require_end)
   {
      auto parse_uint = [&](uint32_t& result, int digits) {
         result = 0;
         while (digits--)
         {
            if (s != end && *s >= '0' && *s <= '9')
               result = result * 10 + *s++ - '0';
            else
               return false;
         }
         return true;
      };
      uint32_t y, m, d, h, min, sec;
      if (!parse_uint(y, 4))
         return false;
      if (s == end || *s++ != '-')
         return false;
      if (!parse_uint(m, 2))
         return false;
      if (s == end || *s++ != '-')
         return false;
      if (!parse_uint(d, 2))
         return false;
      if (s == end || *s++ != 'T')
         return false;
      if (!parse_uint(h, 2))
         return false;
      if (s == end || *s++ != ':')
         return false;
      if (!parse_uint(min, 2))
         return false;
      if (s == end || *s++ != ':')
         return false;
      if (!parse_uint(sec, 2))
         return false;
      result = sys_days(year_month_day{year_t{y}, month_t{m}, day_t{d}}.to_days())
                       .time_since_epoch()
                       .count() *
                   86400 +
               h * 3600 + min * 60 + sec;
      if (eat_fractional && s != end && *s == '.')
      {
         ++s;
         while (s != end && *s >= '0' && *s <= '9')
            ++s;
      }
      return s == end || !require_end;
   }

   [[nodiscard]] inline bool string_to_utc_seconds(uint32_t& result, const char* s, const char* end)
   {
      return string_to_utc_seconds(result, s, end, true, true);
   }

   [[nodiscard]] inline bool string_to_utc_microseconds(uint64_t& result,
                                                        const char*& s,
                                                        const char* end,
                                                        bool require_end)
   {
      uint32_t sec;
      if (!string_to_utc_seconds(sec, s, end, false, false))
         return false;
      result = sec * 1000000ull;
      if (s == end)
         return true;
      if (*s != '.')
         return !require_end;
      ++s;
      uint32_t scale = 100000;
      while (scale >= 1 && s != end && *s >= '0' && *s <= '9')
      {
         result += (*s++ - '0') * scale;
         scale /= 10;
      }
      return s == end || !require_end;
   }

   [[nodiscard]] inline bool string_to_utc_microseconds(uint64_t& result,
                                                        const char* s,
                                                        const char* end)
   {
      return string_to_utc_microseconds(result, s, end, true);
   }

   [[nodiscard]] inline constexpr bool string_to_symbol_code(uint64_t& result,
                                                             const char*& pos,
                                                             const char* end,
                                                             bool require_end)
   {
      while (pos != end && *pos == ' ')
         ++pos;
      result = 0;
      uint32_t i = 0;
      while (pos != end && *pos >= 'A' && *pos <= 'Z')
      {
         if (i >= 7)
            return false;
         result |= uint64_t(*pos++) << (8 * i++);
      }
      return i && (pos == end || !require_end);
   }

   [[nodiscard]] inline constexpr bool string_to_symbol_code(uint64_t& result,
                                                             const char* pos,
                                                             const char* end)
   {
      return string_to_symbol_code(result, pos, end, true);
   }

   inline std::string symbol_code_to_string(uint64_t v)
   {
      std::string result;
      while (v > 0)
      {
         result += char(v & 0xFF);
         v >>= 8;
      }
      return result;
   }

   [[nodiscard]] inline bool string_to_symbol(uint64_t& result,
                                              uint8_t precision,
                                              const char*& pos,
                                              const char* end,
                                              bool require_end)
   {
      if (!eosio::string_to_symbol_code(result, pos, end, require_end))
         return false;
      result = (result << 8) | precision;
      return true;
   }

   [[nodiscard]] inline bool string_to_symbol(uint64_t& result,
                                              const char*& pos,
                                              const char* end,
                                              bool require_end)
   {
      uint8_t precision = 0;
      bool found = false;
      while (pos != end && *pos >= '0' && *pos <= '9')
      {
         precision = precision * 10 + (*pos - '0');
         found = true;
         ++pos;
      }
      if (!found || pos == end || *pos++ != ',')
         return false;
      return string_to_symbol(result, precision, pos, end, require_end);
   }

   [[nodiscard]] inline bool string_to_symbol(uint64_t& result, const char* pos, const char* end)
   {
      return string_to_symbol(result, pos, end, true);
   }

   inline std::string symbol_to_string(uint64_t v)
   {
      return std::to_string(v & 0xff) + "," + eosio::symbol_code_to_string(v >> 8);
   }

   [[nodiscard]] inline constexpr bool string_to_asset(int64_t& amount,
                                                       uint64_t& symbol,
                                                       const char*& s,
                                                       const char* end,
                                                       bool expect_end)
   {
      // todo: check overflow
      while (s != end && *s == ' ')  //
         ++s;
      uint64_t uamount = 0;
      uint8_t precision = 0;
      bool negative = false;
      if (s != end && *s == '-')
      {
         ++s;
         negative = true;
      }
      while (s != end && *s >= '0' && *s <= '9')  //
         uamount = uamount * 10 + (*s++ - '0');
      if (s != end && *s == '.')
      {
         ++s;
         while (s != end && *s >= '0' && *s <= '9')
         {
            uamount = uamount * 10 + (*s++ - '0');
            ++precision;
         }
      }
      if (negative)
         uamount = -uamount;
      amount = uamount;
      uint64_t code = 0;
      if (!eosio::string_to_symbol_code(code, s, end, expect_end))
         return false;
      symbol = (code << 8) | precision;
      return true;
   }

   [[nodiscard]] inline bool string_to_asset(int64_t& amount,
                                             uint64_t& symbol,
                                             const char* s,
                                             const char* end)
   {
      return string_to_asset(amount, symbol, s, end, true);
   }

   inline std::string asset_to_string(int64_t amount, uint64_t symbol)
   {
      std::string result;
      uint64_t uamount;
      if (amount < 0)
         uamount = -amount;
      else
         uamount = amount;
      uint8_t precision = symbol;
      if (precision)
      {
         while (precision--)
         {
            result += '0' + uamount % 10;
            uamount /= 10;
         }
         result += '.';
      }
      do
      {
         result += '0' + uamount % 10;
         uamount /= 10;
      } while (uamount);
      if (amount < 0)
         result += '-';
      std::reverse(result.begin(), result.end());
      return result + ' ' + eosio::symbol_code_to_string(symbol >> 8);
   }

}  // namespace eosio
