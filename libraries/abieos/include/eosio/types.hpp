#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace eosio
{
   template <class T>
   using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

   template <typename... Ts>
   struct type_list
   {
      static constexpr int size = sizeof...(Ts);
   };

   template <typename... Ts>
   std::tuple<Ts...> tuple_from_type_list_impl(type_list<Ts...>&&);
   template <typename T>
   using tuple_from_type_list = decltype(tuple_from_type_list_impl(std::declval<T>()));

   template <typename T>
   struct member_fn;

   template <typename R, typename T, typename... Args>
   struct member_fn<R (T::*)(Args...)>
   {
      static constexpr bool is_const = false;
      static constexpr int num_args = sizeof...(Args);
      using type = R (T::*)(Args...);
      using class_type = T;
      using return_type = R;
      using arg_types = type_list<Args...>;
   };

   template <typename R, typename T, typename... Args>
   struct member_fn<R (T::*)(Args...) const>
   {
      static constexpr bool is_const = true;
      static constexpr int num_args = sizeof...(Args);
      using type = R (T::*)(Args...) const;
      using class_type = T;
      using return_type = R;
      using arg_types = type_list<Args...>;
   };

   template <typename T>
   struct is_non_const_member_fn
   {
      constexpr operator bool()
      {
         if constexpr (std::is_member_function_pointer_v<T>)
            return !eosio::member_fn<T>::is_const;
         else
            return false;
      }
   };

   template <typename F, typename... Args, typename... Names>
   void for_each_named_type(F&& f, type_list<Args...>, Names... names)
   {
      (f((remove_cvref_t<Args>*)nullptr, names), ...);
   }

   template <typename T>
   struct is_serializable_container : std::false_type
   {
   };

   template <typename T>
   struct is_serializable_container<std::vector<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct is_serializable_container<std::list<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct is_serializable_container<std::deque<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct is_serializable_container<std::set<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename K, typename V>
   struct is_serializable_container<std::map<K, V>> : std::true_type
   {
      using value_type = typename std::map<K, V>::value_type;
   };

   template <typename T>
   struct is_std_optional : std::false_type
   {
   };

   template <typename T>
   struct is_std_optional<std::optional<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct is_std_unique_ptr : std::false_type
   {
   };

   template <typename T>
   struct is_std_unique_ptr<std::unique_ptr<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct is_std_reference_wrapper : std::false_type
   {
   };

   template <typename T>
   struct is_std_reference_wrapper<std::reference_wrapper<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct is_binary_extension : std::false_type
   {
   };

   template <typename T>
   struct might_not_exist;

   template <typename T>
   struct is_binary_extension<might_not_exist<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct binary_extension;

   template <typename T>
   struct is_binary_extension<binary_extension<T>> : std::true_type
   {
      using value_type = T;
   };

   template <typename T>
   struct is_std_variant : std::false_type
   {
   };

   template <typename... Ts>
   struct is_std_variant<std::variant<Ts...>> : std::true_type
   {
      using types = type_list<Ts...>;
   };

   constexpr const char* get_type_name(bool*) { return "bool"; }
   constexpr const char* get_type_name(std::int8_t*) { return "int8"; }
   constexpr const char* get_type_name(std::uint8_t*) { return "uint8"; }
   constexpr const char* get_type_name(std::int16_t*) { return "int16"; }
   constexpr const char* get_type_name(std::uint16_t*) { return "uint16"; }
   constexpr const char* get_type_name(std::int32_t*) { return "int32"; }
   constexpr const char* get_type_name(std::uint32_t*) { return "uint32"; }
   constexpr const char* get_type_name(std::int64_t*) { return "int64"; }
   constexpr const char* get_type_name(std::uint64_t*) { return "uint64"; }
   constexpr const char* get_type_name(float*) { return "float32"; }
   constexpr const char* get_type_name(double*) { return "float64"; }
   constexpr const char* get_type_name(std::string*) { return "string"; }

#ifndef ABIEOS_NO_INT128
   constexpr const char* get_type_name(__int128*) { return "int128"; }
   constexpr const char* get_type_name(unsigned __int128*) { return "uint128"; }
#endif

#ifdef __eosio_cdt__
   constexpr const char* get_type_name(long double*) { return "float128"; }
#endif

   template <std::size_t N, std::size_t M>
   constexpr std::array<char, N + M> array_cat(std::array<char, N> lhs, std::array<char, M> rhs)
   {
      std::array<char, N + M> result{};
      for (int i = 0; i < N; ++i)
      {
         result[i] = lhs[i];
      }
      for (int i = 0; i < M; ++i)
      {
         result[i + N] = rhs[i];
      }
      return result;
   }

   template <std::size_t N>
   constexpr std::array<char, N> to_array(std::string_view s)
   {
      std::array<char, N> result{};
      for (int i = 0; i < N; ++i)
      {
         result[i] = s[i];
      }
      return result;
   }

   template <typename T, std::size_t N>
   constexpr auto append_type_name(const char (&suffix)[N])
   {
      constexpr std::string_view name = get_type_name((T*)nullptr);
      return array_cat(to_array<name.size()>(name), to_array<N>({suffix, N}));
   }

   template <typename T>
   constexpr auto vector_type_name = append_type_name<T>("[]");

   template <typename T>
   constexpr auto optional_type_name = append_type_name<T>("?");

   template <typename T>
   constexpr const char* get_type_name(std::vector<T>*)
   {
      return vector_type_name<T>.data();
   }

   template <typename T>
   constexpr const char* get_type_name(std::optional<T>*)
   {
      return optional_type_name<T>.data();
   }

   struct variant_type_appender
   {
      char* buf;
      constexpr variant_type_appender operator+(std::string_view s)
      {
         *buf++ = '_';
         for (auto ch : s)
            *buf++ = ch;
         return *this;
      }
   };

   template <typename... T>
   constexpr auto get_variant_type_name()
   {
      constexpr std::size_t size =
          sizeof("variant") + ((std::string_view(get_type_name((T*)nullptr)).size() + 1) + ...);
      std::array<char, size> buffer{'v', 'a', 'r', 'i', 'a', 'n', 't'};
      (variant_type_appender{buffer.data() + 7} + ... +
       std::string_view(get_type_name((T*)nullptr)));
      buffer[buffer.size() - 1] = '\0';
      return buffer;
   }

   template <typename... T>
   constexpr auto variant_type_name = get_variant_type_name<T...>();

   template <typename... T>
   constexpr const char* get_type_name(std::variant<T...>*)
   {
      return variant_type_name<T...>.data();
   }

}  // namespace eosio
