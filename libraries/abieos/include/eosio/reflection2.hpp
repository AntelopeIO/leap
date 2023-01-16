#include <eosio/reflection.hpp>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/facilities/check_empty.hpp>
#include <boost/preprocessor/logical/bitand.hpp>
#include <boost/preprocessor/logical/compl.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/push_front.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#define EOSIO_REFLECT2_MATCH_CHECK_N(x, n, r, ...) \
   BOOST_PP_BITAND(n, BOOST_PP_COMPL(BOOST_PP_CHECK_EMPTY(r)))
#define EOSIO_REFLECT2_MATCH_CHECK(...) EOSIO_REFLECT2_MATCH_CHECK_N(__VA_ARGS__, 0, )
#define EOSIO_REFLECT2_MATCH(base, x) EOSIO_REFLECT2_MATCH_CHECK(BOOST_PP_CAT(base, x))

#define EOSIO_REFLECT2_FIRST(a, ...) a
#define EOSIO_REFLECT2_APPLY_FIRST(a) EOSIO_REFLECT2_FIRST(a)
#define EOSIO_REFLECT2_SKIP_SECOND(a, b, ...) (a, __VA_ARGS__)

#define EOSIO_REFLECT2_KNOWN_ITEM(STRUCT, item)                                                   \
   EOSIO_REFLECT2_FIRST EOSIO_REFLECT2_APPLY_FIRST(BOOST_PP_CAT(EOSIO_REFLECT2_MATCH_ITEM, item)) \
       EOSIO_REFLECT2_SKIP_SECOND BOOST_PP_TUPLE_PUSH_FRONT(                                      \
           EOSIO_REFLECT2_APPLY_FIRST(BOOST_PP_CAT(EOSIO_REFLECT2_MATCH_ITEM, item)), STRUCT)
#define EOSIO_REFLECT2_MATCH_ITEM(r, STRUCT, item)                                  \
   BOOST_PP_IIF(BOOST_PP_CHECK_EMPTY(item), ,                                       \
                BOOST_PP_IIF(EOSIO_REFLECT2_MATCH(EOSIO_REFLECT2_MATCH_ITEM, item), \
                             EOSIO_REFLECT2_KNOWN_ITEM, EOSIO_REFLECT2_MEMBER)(STRUCT, item))

#define EOSIO_REFLECT2_MEMBER(STRUCT, member)                               \
   f(#member, [](auto p) -> decltype(&std::decay_t<decltype(*p)>::member) { \
      return &std::decay_t<decltype(*p)>::member;                           \
   });

#define EOSIO_REFLECT2_MATCH_ITEMbase(...) (EOSIO_REFLECT2_base, __VA_ARGS__), 1
#define EOSIO_REFLECT2_base(STRUCT, base)                                                         \
   static_assert(std::is_base_of_v<base, STRUCT>,                                                 \
                 BOOST_PP_STRINGIZE(base) " is not a base class of " BOOST_PP_STRINGIZE(STRUCT)); \
   eosio_for_each_field((base*)nullptr, f);

#define EOSIO_REFLECT2_MATCH_ITEMmethod(...) (EOSIO_REFLECT2_method, __VA_ARGS__), 1
#define EOSIO_REFLECT2_method(STRUCT, member, ...)                   \
   f(                                                                \
       BOOST_PP_STRINGIZE(member),                                   \
       [](auto p) -> decltype(&std::decay_t<decltype(*p)>::member) { \
          return &std::decay_t<decltype(*p)>::member;                \
       },                                                            \
       __VA_ARGS__);

/**
 * EOSIO_REFLECT2(<struct>, <member or base spec>...)
 * Each parameter may be one of the following:
 *    * ident:                         non-static data member or method
 *    * base(ident):                   base class
 *    * method(ident, "arg1", ...):    method
 */
#define EOSIO_REFLECT2(STRUCT, ...)                                               \
   [[maybe_unused]] inline const char* get_type_name(STRUCT*) { return #STRUCT; } \
   template <typename F>                                                          \
   constexpr void eosio_for_each_field(STRUCT*, F f)                              \
   {                                                                              \
      BOOST_PP_SEQ_FOR_EACH(EOSIO_REFLECT2_MATCH_ITEM, STRUCT,                    \
                            BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))                \
   }

#define EOSIO_REFLECT2_FOR_EACH_FIELD(STRUCT, ...) \
   BOOST_PP_SEQ_FOR_EACH(EOSIO_REFLECT2_MATCH_ITEM, STRUCT, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))
