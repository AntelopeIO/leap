#include <boost/test/unit_test.hpp>
#include <list>

#include <eosio/trace_api/compressed_file.hpp>
#include <eosio/trace_api/test_common.hpp>
#include <fc/io/cfile.hpp>

using namespace eosio;
using namespace eosio::trace_api;


struct temp_file_fixture {
   std::string create_temp_file( const std::string& contents ) {
      auto& tmp = temps.emplace_back("w");
      auto& file = tmp.file();
      file.write(contents.data(), contents.size());
      file.close();
      return file.get_file_path().string();
   }

   std::string create_temp_file( const void* data, size_t size ) {
      auto& tmp = temps.emplace_back("wb");
      auto& file = tmp.file();
      if (data && size)
         file.write(reinterpret_cast<const char*>(data), size);
      file.close();
      return file.get_file_path().string();
   }

   std::list<fc::temp_cfile> temps;
};

typedef std::tuple<uint64_t, std::array<char, 6733>> test_types;

namespace {

   template<typename T>
   struct is_std_array {
      static constexpr bool value = false;
   };

   template<typename T, size_t S>
   struct is_std_array<std::array<T, S>> {
      static constexpr bool value = true;
   };

   template<typename T>
   constexpr bool is_std_array_v = is_std_array<T>::value;

   template<typename T>
   T convert_to(uint64_t value) {
      if constexpr (is_std_array_v<T>) {
         T result({0});
         std::memcpy(result.data(), &value, std::min<size_t>(sizeof(value), result.size()));
         return result;
      } else {
         return T(value);
      }
   }

   template<typename T>
   T make_random() {
      if constexpr (is_std_array_v<T>) {
         constexpr size_t input_byte_size = (std::tuple_size_v<T> * sizeof(typename T::value_type));
         constexpr size_t temp_size = (input_byte_size / sizeof(uint32_t)) + 1;

         std::array<uint32_t, temp_size> temp;
         std::generate(temp.begin(), temp.end(), []() {
            return (uint32_t)std::rand();
         });

         T result;
         std::memcpy(result.data(), temp.data(), input_byte_size);
         return result;
      } else {
         return (T)std::rand();
      }
   }
}

namespace std {
   template<typename T, size_t S>
   std::ostream& operator<<(std::ostream &os, const std::array<T,S> &array) {
      os << fc::to_hex(reinterpret_cast<const char*>(array.data()), S * sizeof(T));
      return os;
   }
}

BOOST_AUTO_TEST_SUITE(compressed_file_tests)

BOOST_FIXTURE_TEST_CASE_TEMPLATE(random_access_test, T, test_types, temp_file_fixture) {
   // generate a large dataset where ever 8 bytes is the offset to that 8 bytes of data
   auto data = std::vector<T>(128);
   std::generate(data.begin(), data.end(), [offset=0ULL]() mutable {
      auto result = offset;
      offset+=sizeof(T);
      return convert_to<T>(result);
   });

   auto uncompressed_filename = create_temp_file(data.data(), data.size() * sizeof(T));
   auto compressed_filename = create_temp_file(nullptr, 0);

   BOOST_TEST(compressed_file::process(uncompressed_filename, compressed_filename, 512));

   // test that you can read all of the offsets from the compressed form by opening and seeking to them
   for (size_t i = 0; i < data.size(); i++) {
      const auto& entry = data.at(i);
      auto compf = compressed_file(compressed_filename);
      compf.open();
      T value;
      compf.seek((long)i * sizeof(T));
      compf.read(reinterpret_cast<char*>(&value), sizeof(T));
      BOOST_TEST(value == entry);
      compf.close();
   }
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(sequential_access, T, test_types, temp_file_fixture) {
   // generate a large dataset where ever 8 bytes is the offset to that 8 bytes of data
   auto data = std::vector<T>(128);
   std::generate(data.begin(), data.end(), [offset=0ULL]() mutable {
      auto result = offset;
      offset+=sizeof(T);
      return convert_to<T>(result);
   });

   auto uncompressed_filename = create_temp_file(data.data(), data.size() * sizeof(T));
   auto compressed_filename = create_temp_file(nullptr, 0);

   BOOST_TEST(compressed_file::process(uncompressed_filename, compressed_filename, 512));

   // test that you can read all of the offsets from the compressed form sequentially
   auto compf = compressed_file(compressed_filename);
   compf.open();
   for( const auto& entry : data ) {
      T value;
      compf.read(reinterpret_cast<char*>(&value), sizeof(value));
      BOOST_TEST(value == entry);
   }
   compf.close();
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(blob_access, T, test_types, temp_file_fixture) {
   // generate a large dataset where ever 8 bytes is the offset to that 8 bytes of data
   auto data = std::vector<T>(128);
   std::generate(data.begin(), data.end(), []() {
      return make_random<T>();
   });

   auto uncompressed_filename = create_temp_file(data.data(), data.size() * sizeof(T));
   auto compressed_filename = create_temp_file(nullptr, 0);

   BOOST_TEST(compressed_file::process(uncompressed_filename, compressed_filename, 512));

   // test that you can read all of the offsets from the compressed form through the end of the file
   for (size_t i = 0; i < data.size(); i++) {
      auto actual_data = std::vector<T>(128);
      auto compf = compressed_file(compressed_filename);
      compf.open();
      compf.seek(i * sizeof(T));
      compf.read(reinterpret_cast<char*>(actual_data.data()), (actual_data.size() - i) * sizeof(T));
      compf.close();
      BOOST_REQUIRE_EQUAL_COLLECTIONS(data.begin() + i, data.end(), actual_data.begin(), actual_data.end() - i);
   }
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(blob_access_no_seek_points, T, test_types, temp_file_fixture) {
   // generate a large dataset where ever 8 bytes is the offset to that 8 bytes of data
   auto data = std::vector<T>(32);
   std::generate(data.begin(), data.end(), []() {
      return make_random<T>();
   });

   auto uncompressed_size = data.size() * sizeof(T);
   auto uncompressed_filename = create_temp_file(data.data(), uncompressed_size);
   auto compressed_filename = create_temp_file(nullptr, 0);

   // set a stride of the whole file which should result in no seek points
   BOOST_TEST(compressed_file::process(uncompressed_filename, compressed_filename, uncompressed_size));

   // verify that no seek points were created
   fc::cfile compressed;
   compressed.set_file_path(compressed_filename);
   compressed.open("r");
   compressed.seek(std::filesystem::file_size(compressed_filename) - 2);
   const uint16_t expected_seek_point_count = 0;
   uint16_t actual_seek_point_count = std::numeric_limits<uint16_t>::max();
   compressed.read(reinterpret_cast<char*>(&actual_seek_point_count), 2);
   BOOST_REQUIRE_EQUAL(expected_seek_point_count, actual_seek_point_count);

   // test that you can read all of the offsets from the compressed form through the end of the file
   for (size_t i = 0; i < data.size(); i++) {
      auto actual_data = std::vector<T>(32);
      auto compf = compressed_file(compressed_filename);
      compf.open();
      compf.seek(i * sizeof(T));
      compf.read(reinterpret_cast<char*>(actual_data.data()), (actual_data.size() - i) * sizeof(T));
      compf.close();
      BOOST_REQUIRE_EQUAL_COLLECTIONS(data.begin() + i, data.end(), actual_data.begin(), actual_data.end() - i);
   }
}


BOOST_AUTO_TEST_SUITE_END()