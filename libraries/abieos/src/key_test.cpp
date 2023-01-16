#include <eosio/to_key.hpp>
#include "abieos.hpp"

int error_count;

void report_error(const char* assertion, const char* file, int line)
{
   if (error_count <= 20)
   {
      printf("%s:%d: failed %s\n", file, line, assertion);
   }
   ++error_count;
}

#define CHECK(...)                                       \
   do                                                    \
   {                                                     \
      if (__VA_ARGS__)                                   \
      {                                                  \
      }                                                  \
      else                                               \
      {                                                  \
         report_error(#__VA_ARGS__, __FILE__, __LINE__); \
      }                                                  \
   } while (0)

using abieos::asset;
using abieos::block_timestamp;
using abieos::bytes;
using abieos::checksum160;
using abieos::checksum256;
using abieos::checksum512;
using abieos::float128;
using abieos::int128;
using abieos::private_key;
using abieos::public_key;
using abieos::signature;
using abieos::symbol;
using abieos::symbol_code;
using abieos::time_point;
using abieos::time_point_sec;
using abieos::uint128;
using abieos::varint32;
using abieos::varuint32;
using eosio::name;

using vec_type = std::vector<int>;
struct struct_type
{
   std::vector<int> v;
   std::optional<int> o;
   std::variant<int, double> va;
};
EOSIO_REFLECT(struct_type, v, o, va);
EOSIO_COMPARE(struct_type);

// Verifies that the ordering of keys is the same as the ordering of the original objects
template <typename T>
void test_key(const T& x, const T& y)
{
   auto keyx = eosio::convert_to_key(x);
   auto keyy = eosio::convert_to_key(y);
   CHECK(std::lexicographical_compare(keyx.begin(), keyx.end(), keyy.begin(), keyy.end(),
                                      std::less<unsigned char>()) == (x < y));
   CHECK(std::lexicographical_compare(keyy.begin(), keyy.end(), keyx.begin(), keyx.end(),
                                      std::less<unsigned char>()) == (y < x));
}

enum class enum_u8 : unsigned char
{
   v0,
   v1,
   v2 = 255,
};
enum class enum_s8 : signed char
{
   v0,
   v1,
   v2 = -1,
};

enum class enum_u16 : std::uint16_t
{
   v0,
   v1,
   v2 = 65535,
};
enum class enum_s16 : std::int16_t
{
   v0,
   v1,
   v2 = -1,
};

template <typename T>
std::size_t key_size(const T& obj)
{
   eosio::size_stream ss;
   to_key(obj, ss);
   return ss.size;
}

void test_compare()
{
   test_key(true, true);
   test_key(false, false);
   test_key(false, true);
   test_key(true, false);
   test_key(int8_t(0), int8_t(0));
   test_key(int8_t(-128), int8_t(0));
   test_key(int8_t(-128), int8_t(127));
   test_key(uint8_t(0), uint8_t(0));
   test_key(uint8_t(0), uint8_t(255));
   test_key(uint32_t(0), uint32_t(0));
   test_key(uint32_t(0), uint32_t(1));
   test_key(uint32_t(0xFF000000), uint32_t(0xFF));
   test_key(int32_t(0), int32_t(0));
   test_key(int32_t(0), int32_t(1));
   test_key(int32_t(0), int32_t(-1));
   test_key(int32_t(0x7F000000), int32_t(0x100000FF));
   test_key(0.f, -0.f);
   test_key(1.f, 0.f);
   test_key(-std::numeric_limits<float>::infinity(), 0.f);
   test_key(std::numeric_limits<float>::infinity(), 0.f);
   test_key(-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
   test_key(0., -0.);
   test_key(1., 0.);
   test_key(-std::numeric_limits<double>::infinity(), 0.);
   test_key(std::numeric_limits<double>::infinity(), 0.);
   test_key(-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity());
   using namespace eosio::literals;
   test_key("a"_n, "a"_n);
   test_key(name(), name());
   test_key("a"_n, "b"_n);
   test_key("ab"_n, "a"_n);
   test_key(checksum256(), checksum256());
   test_key(checksum256(), checksum256{std::array{0xffffffffffffffffull, 0xffffffffffffffffull,
                                                  0xffffffffffffffffull, 0xffffffffffffffffull}});
   test_key(checksum256(std::array{0x00ffffffffffffffull, 0xffffffffffffffffull,
                                   0xffffffffffffffffull, 0xffffffffffffffffull}),
            checksum256(std::array{0xffffffffffffffffull, 0xffffffffffffffffull,
                                   0xffffffffffffffffull, 0xffffffffffffff00ull}));
   test_key(checksum256(std::array{0xffffffffffffffffull, 0xffffffffffffff00ull,
                                   0xffffffffffffffffull, 0xffffffffffffffffull}),
            checksum256(std::array{0xffffffffffffffffull, 0x00ffffffffffffffull,
                                   0xffffffffffffffffull, 0xffffffffffffffffull}));
   test_key(public_key(), public_key());
   test_key(public_key(std::in_place_index<0>, eosio::ecc_public_key{1}),
            public_key(std::in_place_index<1>));
   test_key(public_key(eosio::webauthn_public_key{
                {}, eosio::webauthn_public_key::user_presence_t::USER_PRESENCE_NONE, "b"}),
            public_key(eosio::webauthn_public_key{
                {}, eosio::webauthn_public_key::user_presence_t::USER_PRESENCE_PRESENT, "a"}));

   using namespace std::literals;
   test_key(""s, ""s);
   test_key(""s, "a"s);
   test_key("a"s, "b"s);
   test_key("aaaaa"s, "aaaaa"s);
   test_key("\0"s, "\xFF"s);
   test_key("\0"s, ""s);
   test_key("\0\0\0"s, "\0\0"s);

   test_key(std::vector<int>{}, std::vector<int>{});
   test_key(std::vector<int>{}, std::vector<int>{0});
   test_key(std::vector<int>{0}, std::vector<int>{1});

   test_key(std::vector<char>{}, std::vector<char>{'\0'});
   test_key(std::vector<char>{'\0'}, std::vector<char>{'\xFF'});
   test_key(std::vector<char>{'\1'}, std::vector<char>{'\xFF'});
   test_key(std::vector<char>{'b'}, std::vector<char>{'a'});

   test_key(std::vector<signed char>{}, std::vector<signed char>{'\0'});
   test_key(std::vector<signed char>{'\0'}, std::vector<signed char>{'\xFF'});
   test_key(std::vector<signed char>{'\1'}, std::vector<signed char>{'\xFF'});
   test_key(std::vector<signed char>{'b'}, std::vector<signed char>{'a'});

   test_key(std::vector<unsigned char>{}, std::vector<unsigned char>{'\0'});
   test_key(std::vector<unsigned char>{'\0'}, std::vector<unsigned char>{255});
   test_key(std::vector<unsigned char>{'\1'}, std::vector<unsigned char>{255});
   test_key(std::vector<unsigned char>{'b'}, std::vector<unsigned char>{'a'});

   test_key(std::vector<bool>{}, std::vector<bool>{true});
   test_key(std::vector<bool>{false}, std::vector<bool>{true});
   test_key(std::vector<bool>{false}, std::vector<bool>{false, true});

   test_key(std::list<int>{}, std::list<int>{1});
   test_key(std::list<int>{0}, std::list<int>{1});
   test_key(std::deque<int>{}, std::deque<int>{1});
   test_key(std::deque<int>{0}, std::deque<int>{1});
   test_key(std::set<int>{}, std::set<int>{1});
   test_key(std::set<int>{0}, std::set<int>{1});
   test_key(std::map<int, int>{}, std::map<int, int>{{1, 0}});
   test_key(std::map<int, int>{{0, 0}}, std::map<int, int>{{1, 0}});

   test_key(enum_u8::v0, enum_u8::v1);
   test_key(enum_u8::v0, enum_u8::v2);
   test_key(enum_u8::v1, enum_u8::v2);

   test_key(enum_s8::v0, enum_s8::v1);
   test_key(enum_s8::v0, enum_s8::v2);
   test_key(enum_s8::v1, enum_s8::v2);

   test_key(enum_u16::v0, enum_u16::v1);
   test_key(enum_u16::v0, enum_u16::v2);
   test_key(enum_u16::v1, enum_u16::v2);

   test_key(enum_s16::v0, enum_s16::v1);
   test_key(enum_s16::v0, enum_s16::v2);
   test_key(enum_s16::v1, enum_s16::v2);

   test_key(varuint32(0), varuint32(0));
   test_key(varuint32(0), varuint32(1));
   test_key(varuint32(1), varuint32(0xFF));
   test_key(varuint32(1), varuint32(0xFFFF));
   test_key(varuint32(1), varuint32(0xFFFFFF));
   test_key(varuint32(1), varuint32(0x7FFFFFFF));
   test_key(varuint32(0x7FFFFF00), varuint32(0x7FFF00FF));
   CHECK(key_size(varuint32(0)) == 1);
   CHECK(key_size(varuint32(0xFF)) == 2);

#if 0
   test_key(varint32(0), varint32(0));
   test_key(varint32(0), varint32(1));
   test_key(varint32(1), varint32(0xFF));
   test_key(varint32(1), varint32(0xFFFF));
   test_key(varint32(1), varint32(0xFFFFFF));
   test_key(varint32(1), varint32(-1));
   test_key(varint32(1), varint32(0x7FFFFFFF));
   test_key(varint32(0x7FFFFF00), varint32(0x7FFF00FF));
   test_key(varint32(-0x7FFFF), varint32(-0x7FFFFFFF));
   test_key(varint32(-0x80000000), varint32(-0x7FFFFFFF));
   test_key(varint32(-0x7F00FF01), varint32(0x7FFF0100));
   CHECK(key_size(varint32(-1)) == 1);
   CHECK(key_size(varint32(0)) == 1);
   CHECK(key_size(varint32(0xFF)) == 2);
#endif

   test_key(struct_type{{}, {}, {0}}, struct_type{{}, {}, {0}});
   test_key(struct_type{{0, 1, 2}, {}, {0}}, struct_type{{}, {}, {0.0}});
   test_key(struct_type{{0, 1, 2}, {}, {0}}, struct_type{{0, 1, 2}, 0, {0}});
   test_key(struct_type{{0, 1, 2}, 0, {0}}, struct_type{{0, 1, 2}, 0, {0.0}});
}

int main()
{
   test_compare();
   if (error_count)
      return 1;
}
