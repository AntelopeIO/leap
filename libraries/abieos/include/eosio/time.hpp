#pragma once
#include <stdint.h>
#include <string>
#include "chain_conversions.hpp"
#include "check.hpp"
#include "from_json.hpp"
#include "operators.hpp"
#include "reflection.hpp"

namespace eosio
{
   /**
    *  @defgroup time
    *  @ingroup core
    *  @brief Classes for working with time.
    */

   class microseconds
   {
     public:
      microseconds() = default;

      explicit microseconds(int64_t c) : _count(c) {}

      /// @cond INTERNAL
      static microseconds maximum() { return microseconds(0x7fffffffffffffffll); }
      friend microseconds operator+(const microseconds& l, const microseconds& r)
      {
         return microseconds(l._count + r._count);
      }
      friend microseconds operator-(const microseconds& l, const microseconds& r)
      {
         return microseconds(l._count - r._count);
      }

      microseconds& operator+=(const microseconds& c)
      {
         _count += c._count;
         return *this;
      }
      microseconds& operator-=(const microseconds& c)
      {
         _count -= c._count;
         return *this;
      }
      int64_t count() const { return _count; }
      int64_t to_seconds() const { return _count / 1000000; }

      int64_t _count = 0;
      /// @endcond
   };

   EOSIO_REFLECT(microseconds, _count);
   EOSIO_COMPARE(microseconds);

   inline microseconds seconds(int64_t s) { return microseconds(s * 1000000); }
   inline microseconds milliseconds(int64_t s) { return microseconds(s * 1000); }
   inline microseconds minutes(int64_t m) { return seconds(60 * m); }
   inline microseconds hours(int64_t h) { return minutes(60 * h); }
   inline microseconds days(int64_t d) { return hours(24 * d); }

   /**
    *  High resolution time point in microseconds
    *
    *  @ingroup time
    */
   class time_point
   {
     public:
      time_point() = default;
      explicit time_point(microseconds e) : elapsed(e) {}
      const microseconds& time_since_epoch() const { return elapsed; }
      uint32_t sec_since_epoch() const { return uint32_t(elapsed.count() / 1000000); }

      static time_point max() { return time_point(microseconds::maximum()); }

      /// @cond INTERNAL
      time_point& operator+=(const microseconds& m)
      {
         elapsed += m;
         return *this;
      }
      time_point& operator-=(const microseconds& m)
      {
         elapsed -= m;
         return *this;
      }
      time_point operator+(const microseconds& m) const { return time_point(elapsed + m); }
      time_point operator+(const time_point& m) const { return time_point(elapsed + m.elapsed); }
      time_point operator-(const microseconds& m) const { return time_point(elapsed - m); }
      microseconds operator-(const time_point& m) const
      {
         return microseconds(elapsed.count() - m.elapsed.count());
      }
      microseconds elapsed;
      /// @endcond
   };

   EOSIO_REFLECT(time_point, elapsed);
   EOSIO_COMPARE(time_point);

   template <typename S>
   void from_string(time_point& obj, S& stream)
   {
      auto pos = stream.pos;
      auto end = stream.end;
      uint64_t utc_microseconds;
      if (!eosio::string_to_utc_microseconds(utc_microseconds, pos, end, false) ||
          !(pos == end || pos + 1 == end && *pos == 'Z'))
      {
         check(false, convert_json_error(eosio::from_json_error::expected_time_point));
      }
      obj = time_point(microseconds(utc_microseconds));
   }

   template <typename S>
   void from_json(time_point& obj, S& stream)
   {
      auto s = stream.get_string();
      eosio::input_stream stream2{s.data(), s.end()};
      from_string(obj, stream2);
   }

   template <typename Base>
   struct time_point_include_z_stream : Base
   {
      using Base::Base;
   };

   template <typename S>
   constexpr bool time_point_include_z(const S*)
   {
      return false;
   }

   template <typename Base>
   constexpr bool time_point_include_z(const time_point_include_z_stream<Base>*)
   {
      return true;
   }

   template <typename S>
   void to_json(const time_point& obj, S& stream)
   {
      if constexpr (time_point_include_z((S*)nullptr))
         return to_json(eosio::microseconds_to_str(obj.elapsed._count) + "Z", stream);
      else
         return to_json(eosio::microseconds_to_str(obj.elapsed._count), stream);
   }

   /**
    *  A lower resolution time_point accurate only to seconds from 1970
    *
    *  @ingroup time
    */
   class time_point_sec
   {
     public:
      time_point_sec() : utc_seconds(0) {}

      explicit time_point_sec(uint32_t seconds) : utc_seconds(seconds) {}

      time_point_sec(const time_point& t)
          : utc_seconds(uint32_t(t.time_since_epoch().count() / 1000000ll))
      {
      }

      static time_point_sec maximum() { return time_point_sec(0xffffffff); }
      static time_point_sec min() { return time_point_sec(0); }

      operator time_point() const { return time_point(eosio::seconds(utc_seconds)); }
      uint32_t sec_since_epoch() const { return utc_seconds; }

      /// @cond INTERNAL
      time_point_sec operator=(const eosio::time_point& t)
      {
         utc_seconds = uint32_t(t.time_since_epoch().count() / 1000000ll);
         return *this;
      }
      time_point_sec& operator+=(uint32_t m)
      {
         utc_seconds += m;
         return *this;
      }
      time_point_sec& operator+=(microseconds m)
      {
         utc_seconds += m.to_seconds();
         return *this;
      }
      time_point_sec& operator+=(time_point_sec m)
      {
         utc_seconds += m.utc_seconds;
         return *this;
      }
      time_point_sec& operator-=(uint32_t m)
      {
         utc_seconds -= m;
         return *this;
      }
      time_point_sec& operator-=(microseconds m)
      {
         utc_seconds -= m.to_seconds();
         return *this;
      }
      time_point_sec& operator-=(time_point_sec m)
      {
         utc_seconds -= m.utc_seconds;
         return *this;
      }
      time_point_sec operator+(uint32_t offset) const
      {
         return time_point_sec(utc_seconds + offset);
      }
      time_point_sec operator-(uint32_t offset) const
      {
         return time_point_sec(utc_seconds - offset);
      }

      friend time_point operator+(const time_point_sec& t, const microseconds& m)
      {
         return time_point(t) + m;
      }
      friend time_point operator-(const time_point_sec& t, const microseconds& m)
      {
         return time_point(t) - m;
      }
      friend microseconds operator-(const time_point_sec& t, const time_point_sec& m)
      {
         return time_point(t) - time_point(m);
      }
      friend microseconds operator-(const time_point& t, const time_point_sec& m)
      {
         return time_point(t) - time_point(m);
      }
      uint32_t utc_seconds;

      /// @endcond
   };

   EOSIO_REFLECT(time_point_sec, utc_seconds);
   EOSIO_COMPARE(time_point);

   template <typename S>
   void from_json(time_point_sec& obj, S& stream)
   {
      auto s = stream.get_string();
      const char* p = s.data();
      if (!eosio::string_to_utc_seconds(obj.utc_seconds, p, s.data() + s.size(), true, true))
      {
         check(false, convert_json_error(from_json_error::expected_time_point));
      }
   }

   template <typename S>
   void to_json(const time_point_sec& obj, S& stream)
   {
      return to_json(eosio::microseconds_to_str(uint64_t(obj.utc_seconds) * 1'000'000), stream);
   }

   /**
    *  This class is used in the block headers to represent the block time
    *  It is a parameterised class that takes an Epoch in milliseconds and
    *  and an interval in milliseconds and computes the number of slots.
    *
    *  @ingroup time
    **/
   class block_timestamp
   {
     public:
      block_timestamp() = default;

      explicit block_timestamp(uint32_t s) : slot(s) {}

      block_timestamp(const time_point& t) { set_time_point(t); }

      block_timestamp(const time_point_sec& t) { set_time_point(t); }

      // This incorrect definition is in the CDT. Replacing with a new function (max)
      // so contracts don't accidentally change behavior.
      // static block_timestamp maximum() { return block_timestamp(0xffff); }

      static block_timestamp max() { return block_timestamp(~uint32_t(0)); }
      static block_timestamp min() { return block_timestamp(0); }

      block_timestamp next() const
      {
         eosio::check(std::numeric_limits<uint32_t>::max() - slot >= 1, "block timestamp overflow");
         auto result = block_timestamp(*this);
         result.slot += 1;
         return result;
      }

      time_point to_time_point() const { return (time_point)(*this); }

      operator time_point() const
      {
         int64_t msec = slot * (int64_t)block_interval_ms;
         msec += block_timestamp_epoch;
         return time_point(milliseconds(msec));
      }

      /// @cond INTERNAL
      void operator=(const time_point& t) { set_time_point(t); }

      bool operator>(const block_timestamp& t) const { return slot > t.slot; }
      bool operator>=(const block_timestamp& t) const { return slot >= t.slot; }
      bool operator<(const block_timestamp& t) const { return slot < t.slot; }
      bool operator<=(const block_timestamp& t) const { return slot <= t.slot; }
      bool operator==(const block_timestamp& t) const { return slot == t.slot; }
      bool operator!=(const block_timestamp& t) const { return slot != t.slot; }
      uint32_t slot = 0;
      static constexpr int32_t block_interval_ms = 500;
      static constexpr int64_t block_timestamp_epoch = 946684800000ll;  // epoch is year 2000
                                                                        /// @endcond
     private:
      void set_time_point(const time_point& t)
      {
         int64_t micro_since_epoch = t.time_since_epoch().count();
         int64_t msec_since_epoch = micro_since_epoch / 1000;
         slot = uint32_t((msec_since_epoch - block_timestamp_epoch) / int64_t(block_interval_ms));
      }

      void set_time_point(const time_point_sec& t)
      {
         int64_t sec_since_epoch = t.sec_since_epoch();
         slot = uint32_t((sec_since_epoch * 1000 - block_timestamp_epoch) / block_interval_ms);
      }
   };  // block_timestamp

   /**
    *  @ingroup time
    */
   typedef block_timestamp block_timestamp_type;

   EOSIO_REFLECT(block_timestamp_type, slot);

   template <typename S>
   void from_string(block_timestamp& obj, S& stream)
   {
      time_point tp;
      from_string(tp, stream);
      obj = block_timestamp(tp);
   }

   template <typename S>
   void from_json(block_timestamp& obj, S& stream)
   {
      time_point tp;
      from_json(tp, stream);
      obj = block_timestamp(tp);
   }

   template <typename S>
   void to_json(const block_timestamp& obj, S& stream)
   {
      return to_json(time_point(obj), stream);
   }

}  // namespace eosio
