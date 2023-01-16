#pragma once

#include <deque>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <variant>
#include <vector>
#include "for_each_field.hpp"
#include "stream.hpp"

namespace eosio
{
   template <typename S>
   bool to_bin(std::string_view sv, S& stream, std::string_view&);

   template <typename S>
   bool to_bin(const std::string& s, S& stream, std::string_view&);

   template <typename T, typename S>
   bool to_bin(const std::vector<T>& obj, S& stream, std::string_view&);

   template <typename T, typename S>
   bool to_bin(const std::optional<T>& obj, S& stream, std::string_view&);

   template <typename... Ts, typename S>
   bool to_bin(const std::variant<Ts...>& obj, S& stream, std::string_view&);

   template <typename... Ts, typename S>
   bool to_bin(const std::tuple<Ts...>& obj, S& stream, std::string_view&);

   template <typename T, typename S>
   bool to_bin(const T& obj, S& stream, std::string_view&);

   template <typename S>
   void varuint32_to_bin(uint64_t val, S& stream)
   {
      check(!(val >> 32), convert_stream_error(stream_error::varuint_too_big));
      do
      {
         uint8_t b = val & 0x7f;
         val >>= 7;
         b |= ((val > 0) << 7);
         stream.write(b);
      } while (val);
   }

   // signed leb128 encoding
   template <typename S>
   void sleb64_to_bin(int64_t val, S& stream)
   {
      bool done = false;
      while (!done)
      {
         uint8_t b = val & 0x7f;
         done = (val >> 6) == (val >> 7);
         val >>= 7;
         b |= (!done << 7);
         stream.write(b);
      }
   }

   inline void push_varuint32(std::vector<char>& bin, uint32_t v)
   {
      vector_stream st{bin};
      varuint32_to_bin(v, st);
   }

   template <typename S>
   void to_bin(std::string_view sv, S& stream)
   {
      varuint32_to_bin(sv.size(), stream);
      stream.write(sv.data(), sv.size());
   }

   template <typename S>
   void to_bin(const std::string& s, S& stream)
   {
      to_bin(std::string_view{s}, stream);
   }

   template <typename T, typename S>
   void to_bin_range(const T& obj, S& stream)
   {
      varuint32_to_bin(obj.size(), stream);
      for (auto& x : obj)
      {
         to_bin(x, stream);
      }
   }

   template <typename T, std::size_t N, typename S>
   void to_bin(const T (&obj)[N], S& stream)
   {
      varuint32_to_bin(N, stream);
      if constexpr (has_bitwise_serialization<T>())
      {
         stream.write(reinterpret_cast<const char*>(&obj), N * sizeof(T));
      }
      else
      {
         for (auto& x : obj)
         {
            to_bin(x, stream);
         }
      }
   }

   template <typename T, typename S>
   void to_bin(const std::vector<T>& obj, S& stream)
   {
      varuint32_to_bin(obj.size(), stream);
      if constexpr (has_bitwise_serialization<T>())
      {
         stream.write(reinterpret_cast<const char*>(obj.data()), obj.size() * sizeof(T));
      }
      else
      {
         for (auto& x : obj)
         {
            to_bin(x, stream);
         }
      }
   }

   template <typename T, typename S>
   void to_bin(const std::list<T>& obj, S& stream)
   {
      to_bin_range(obj, stream);
   }

   template <typename T, typename S>
   void to_bin(const std::deque<T>& obj, S& stream)
   {
      to_bin_range(obj, stream);
   }

   template <typename T, typename S>
   void to_bin(const std::set<T>& obj, S& stream)
   {
      to_bin_range(obj, stream);
   }

   template <typename T, typename U, typename S>
   void to_bin(const std::map<T, U>& obj, S& stream)
   {
      to_bin_range(obj, stream);
   }

   template <typename S>
   void to_bin(const input_stream& obj, S& stream)
   {
      varuint32_to_bin(obj.end - obj.pos, stream);
      stream.write(obj.pos, obj.end - obj.pos);
   }

   template <typename First, typename Second, typename S>
   void to_bin(const std::pair<First, Second>& obj, S& stream)
   {
      to_bin(obj.first, stream);
      return to_bin(obj.second, stream);
   }

   template <typename T, typename S>
   void to_bin(const std::optional<T>& obj, S& stream)
   {
      to_bin(obj.has_value(), stream);
      if (obj)
         to_bin(*obj, stream);
   }

   template <typename... Ts, typename S>
   void to_bin(const std::variant<Ts...>& obj, S& stream)
   {
      varuint32_to_bin(obj.index(), stream);
      std::visit([&](auto& x) { return to_bin(x, stream); }, obj);
   }

   template <int i, typename T, typename S>
   void to_bin_tuple(const T& obj, S& stream)
   {
      if constexpr (i < std::tuple_size_v<T>)
      {
         to_bin(std::get<i>(obj), stream);
         to_bin_tuple<i + 1>(obj, stream);
      }
   }

   template <typename... Ts, typename S>
   void to_bin(const std::tuple<Ts...>& obj, S& stream)
   {
      to_bin_tuple<0>(obj, stream);
   }

   template <typename T, std::size_t N, typename S>
   void to_bin(const std::array<T, N>& obj, S& stream)
   {
      for (const T& elem : obj)
      {
         to_bin(elem, stream);
      }
   }

   template <typename T, typename S>
   void to_bin(const T& obj, S& stream)
   {
      if constexpr (has_bitwise_serialization<T>())
      {
         stream.write(reinterpret_cast<const char*>(&obj), sizeof(obj));
      }
      else
      {
         for_each_field(obj, [&](auto& member) { to_bin(member, stream); });
      }
   }

   template <typename T>
   void convert_to_bin(const T& t, std::vector<char>& bin)
   {
      size_stream ss;
      to_bin(t, ss);
      auto orig_size = bin.size();
      bin.resize(orig_size + ss.size);
      fixed_buf_stream fbs(bin.data() + orig_size, ss.size);
      to_bin(t, fbs);
      check(fbs.pos == fbs.end, convert_stream_error(stream_error::underrun));
   }

   template <typename T>
   std::vector<char> convert_to_bin(const T& t)
   {
      std::vector<char> result;
      convert_to_bin(t, result);
      return result;
   }

}  // namespace eosio
