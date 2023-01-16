#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include "from_json.hpp"
#include "hex.hpp"
#include "operators.hpp"
#include "reflection.hpp"
#include "to_json.hpp"

namespace eosio
{
   /**
    *  @defgroup fixed_bytes Fixed Size Byte Array
    *  @ingroup core
    *  @ingroup types
    *  @brief Fixed size array of bytes sorted lexicographically
    */

   /**
    *  Fixed size byte array sorted lexicographically
    *
    *  @ingroup fixed_bytes
    *  @tparam Size - Size of the fixed_bytes object
    *  @tparam Word - Type to use for storage
    */
   template <std::size_t Size, typename Word = std::uint64_t>
   class fixed_bytes
   {
     private:
      // Returns the minimum number of objects of type T required to hold at least Size bytes.
      // T must be an unsigned integer type.
      template <typename T>
      static constexpr std::size_t count_words()
      {
         return (Size + sizeof(T) - 1) / sizeof(T);
      }
      // Divides a value into words and writes the highest words first.
      // If U is 1 byte, this is equivalent to big-endian encoding.
      // sizeof(T) must be divisible by sizeof(U).
      // Writes up to sizeof(T)/sizeof(U) elements to the range [ptr, end)
      // Returns the end of the range written.
      template <typename U, typename T>
      static constexpr U* write_be(U* ptr, U* end, T t)
      {
         constexpr std::size_t words = sizeof(T) / sizeof(U);
         for (std::size_t i = 0; i < words && ptr < end; ++i)
         {
            *ptr++ = static_cast<U>(t >> std::numeric_limits<U>::digits * (words - i - 1));
         }
         return ptr;
      }
      // The opposite of write_be.  If there are insufficient elements in [ptr, end),
      // fills `out` as if the missing elements were 0.
      template <typename U, typename T>
      static constexpr const U* read_be(const U* ptr, const U* end, T& out)
      {
         constexpr std::size_t words = sizeof(T) / sizeof(U);
         T result = 0;
         for (std::size_t i = 0; i < words && ptr < end; ++i, ++ptr)
         {
            result |= static_cast<T>(*ptr) << (std::numeric_limits<U>::digits * (words - i - 1));
         }
         out = result;
         return ptr;
      }
      // Either splits or combines words depending on whether
      // T is larger than U.
      // Both arrays must hold the minimum number of elements
      // required to store Size bytes.
      template <typename T, typename U>
      static constexpr void convert_array(const T* t, U* u)
      {
         constexpr std::size_t t_elems = count_words<T>();
         constexpr std::size_t u_elems = count_words<U>();
         if constexpr (sizeof(T) > sizeof(U))
         {
            U* const end = u + u_elems;
            for (std::size_t i = 0; i < t_elems; ++i)
            {
               u = write_be(u, end, t[i]);
            }
         }
         else
         {
            const T* const end = t + t_elems;
            for (std::size_t i = 0; i < u_elems; ++i)
            {
               t = read_be(t, end, u[i]);
            }
         }
      }

      template <typename T, typename U>
      static constexpr std::array<T, count_words<T>()> convert_array(const U* u)
      {
         std::array<T, count_words<T>()> result{0};
         convert_array(u, result.data());
         return result;
      }

      template <typename T, typename U>
      static constexpr std::array<T, count_words<T>()> convert_array(const U* u, const U* end)
      {
         std::array<U, count_words<U>()> tmp{0};
         std::size_t count = std::min(static_cast<std::size_t>(end - u), tmp.size());
         for (std::size_t i = 0; i < count; ++i)
         {
            tmp[i] = u[i];
         }
         return convert_array<T>(tmp.data());
      }

      template <typename T>
      using require_word = std::enable_if_t<std::is_unsigned_v<T>>;

     public:
      using word_t = Word;
      /**
       * Get number of words contained in this fixed_bytes object. A word is defined to be 16 bytes
       * in size
       */
      static constexpr std::size_t num_words() { return count_words<Word>(); }

      /**
       * Get number of padded bytes contained in this fixed_bytes object. Padded bytes are the
       * remaining bytes inside the fixed_bytes object after all the words are allocated
       */
      static constexpr size_t padded_bytes() { return num_words() * sizeof(Word) - Size; }

      /**
       * Default constructor to fixed_bytes object which initializes all bytes to zero
       */
      constexpr fixed_bytes() = default;

      /**
       * Constructor to fixed_bytes object from initializer list of bytes.
       */
      constexpr fixed_bytes(std::initializer_list<std::uint8_t> il)
          : value(convert_array<Word>(il.begin(), il.end()))
      {
      }

      /**
       * Constructor to fixed_bytes object from std::array of num_words() word_t types
       *
       * @param arr    data
       */
      constexpr fixed_bytes(const std::array<Word, num_words()>& arr) : value(arr) {}

      /**
       * Constructor to fixed_bytes object from std::array of unsigned integral types.
       *
       * @param arr - Source data.  arr cannot hold more words than are required to fill Size bytes.
       * If it contains fewer than Size bytes, the remaining bytes will be zero-filled.
       */
      template <typename T,
                std::size_t N,
                typename Enable = std::enable_if_t<std::is_unsigned_v<T> && N <= count_words<T>()>>
      constexpr fixed_bytes(const std::array<T, N>& arr)
          : value(convert_array<Word>(arr.begin(), arr.end()))
      {
      }

      /**
       * Constructor to fixed_bytes object from fixed-sized C array of unsigned integral types.
       *
       * @param arr - Source data.  arr cannot hold more words than are required to fill Size bytes.
       * If it contains fewer than Size bytes, the remaining bytes will be zero-filled.
       */
      template <typename T,
                std::size_t N,
                typename Enable = std::enable_if_t<std::is_unsigned_v<T> && N <= count_words<T>()>>
      constexpr fixed_bytes(const T (&arr)[N]) : value(convert_array<Word>(&arr[0], &arr[0] + N))
      {
      }

      /**
       *  Create a new fixed_bytes object from a sequence of words
       *
       *  @tparam T - The type of the words.  T must be specified explicitly.
       *  @param a - The words in the sequence.   All the parameters must have type T.  The number
       * of parameters must be equal to the number of values of type T required to fill Size bytes.
       */
      template <typename T,
                typename... A,
                typename Enable =
                    std::enable_if_t<(std::is_unsigned_v<T> && (std::is_same_v<T, A> && ...) &&
                                      (count_words<T>() == sizeof...(A)))>>
      static constexpr fixed_bytes make_from_word_sequence(A... a)
      {
         T args[count_words<T>()] = {a...};
         return fixed_bytes(args);
      }
      /**
       * Extract the contained data as an array of words
       *
       * @tparam T - The word type to return.  T must be an unsigned integral type.
       */
      template <typename T>
      constexpr auto extract_as_word_array() const
      {
         return convert_array<T>(data());
      }
      /**
       * Extract the contained data as an array of bytes
       *
       * @return - the extracted data as array of bytes
       */
      constexpr std::array<std::uint8_t, Size> extract_as_byte_array() const
      {
         return extract_as_word_array<std::uint8_t>();
      }
      /**
       * Get the underlying data of the contained std::array
       */
      constexpr Word* data() { return value.data(); }
      /**
       * Get the underlying data of the contained std::array
       */
      constexpr const Word* data() const { return value.data(); }
      constexpr std::size_t size() const { return value.size(); }
      /**
       * Get the contained std::array
       */
      constexpr const auto& get_array() const { return value; }
      std::array<Word, count_words<Word>()> value{0};
   };

   // This is only needed to make eosio.cdt/tests/unit/fixed_bytes_tests.cpp pass.
   // Everything else should be using one of the typedefs below.
   template <std::size_t Size, typename Word, typename F>
   void eosio_for_each_field(fixed_bytes<Size, Word>*, F&& f)
   {
      f("value", [](auto* p) -> decltype(&std::decay_t<decltype(*p)>::value) {
         return &std::decay_t<decltype(*p)>::value;
      });
   }

   template <std::size_t Size, typename Word>
   EOSIO_COMPARE(fixed_bytes<Size, Word>);

   using checksum160 = fixed_bytes<20, uint32_t>;
   using checksum256 = fixed_bytes<32>;
   using checksum512 = fixed_bytes<64>;

   EOSIO_REFLECT(checksum160, value);
   EOSIO_REFLECT(checksum256, value);
   EOSIO_REFLECT(checksum512, value);

   template <typename T, std::size_t Size, typename S>
   void from_bin(fixed_bytes<Size, T>& obj, S& stream)
   {
      std::array<std::uint8_t, Size> bytes;
      from_bin(bytes, stream);
      obj = fixed_bytes<Size, T>(bytes);
   }

   template <typename T, std::size_t Size, typename S>
   void to_bin(const fixed_bytes<Size, T>& obj, S& stream)
   {
      to_bin(obj.extract_as_byte_array(), stream);
   }

   template <typename T, std::size_t Size, typename S>
   void to_key(const fixed_bytes<Size, T>& obj, S& stream)
   {
      to_bin(obj.extract_as_byte_array(), stream);
   }

   template <typename T, std::size_t Size, typename S>
   void from_json(fixed_bytes<Size, T>& obj, S& stream)
   {
      std::vector<char> v;
      eosio::from_json_hex(v, stream);
      check(v.size() == Size,
            convert_json_error(eosio::from_json_error::hex_string_incorrect_length));
      std::array<uint8_t, Size> bytes;
      std::memcpy(bytes.data(), v.data(), Size);
      obj = fixed_bytes<Size, T>(bytes);
   }

   template <typename T, std::size_t Size, typename S>
   void to_json(const fixed_bytes<Size, T>& obj, S& stream)
   {
      auto bytes = obj.extract_as_byte_array();
      eosio::to_json_hex((const char*)bytes.data(), bytes.size(), stream);
   }

   template <typename T, std::size_t Size>
   std::string to_string(const fixed_bytes<Size, T>& obj)
   {
      auto bytes = obj.extract_as_byte_array();
      return hex(bytes.begin(), bytes.end());
   }
}  // namespace eosio
