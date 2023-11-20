#include <boost/test/unit_test.hpp>
#include <eosio/chain/database_utils.hpp>

#include <iostream>
#include <algorithm>
#include <filesystem>

namespace bip = boost::interprocess;
namespace fs  = std::filesystem;

using namespace chainbase;

template <class T, class A>
using bip_vector = bip::vector<T, A>;
   
using shared_string_vector = shared_vector<shared_string>;

struct book {
   shared_string        title;
   shared_string_vector authors;

   bool operator==(const book&) const = default;
};

FC_REFLECT(book, (title)(authors))

namespace fc::raw {
   template<typename Stream>
   inline void pack(Stream& s, const book& b) {
      fc::raw::pack(s, b.title);
      fc::raw::pack(s, b.authors);
   }

   template<typename Stream>
   inline void unpack(Stream& s, book& b) {
      fc::raw::unpack(s, b.title);
      fc::raw::unpack(s, b.authors);
   }
}

template <class V>
void check_pack_unpack(V &v, V &v2) {
   v.emplace_back(shared_string("Moby Dick"),               shared_string_vector{ "Herman Melville" });
   v.emplace_back(shared_string("All the President's Men"), shared_string_vector{ "Carl Bernstein", "Bob Woodward" });
   
   BOOST_REQUIRE(v[1].title == "All the President's Men");
   BOOST_REQUIRE(v[1].authors[1] == "Bob Woodward");

   // try pack/unpack
   constexpr size_t buffsz = 4096;
   char buf[buffsz];
   fc::datastream<char*> ds(buf, buffsz);

   fc::raw::pack(ds, v);
   ds.seekp(0);
   fc::raw::unpack(ds, v2);
   BOOST_REQUIRE(v2[1].title == "All the President's Men");
   BOOST_REQUIRE(v2[1].authors[1] == "Bob Woodward");
   BOOST_REQUIRE(v == v2);
}

BOOST_AUTO_TEST_CASE(chainbase_type_heap_alloc) {
   std::vector<book> v, v2;
   check_pack_unpack(v, v2);
      
   // check that objects inside the vectors are allocated within theheap
   BOOST_REQUIRE(!v[1].title.get_allocator());
   BOOST_REQUIRE(!v2[1].authors[0].get_allocator());      
}

BOOST_AUTO_TEST_CASE(chainbase_type_segment_alloc) {
   fc::temp_directory temp_dir;
   fs::path temp = temp_dir.path() / "pinnable_mapped_file_101";

   pinnable_mapped_file pmf(temp, true, 1024 * 1024, false, pinnable_mapped_file::map_mode::mapped);
   chainbase::allocator<book> alloc(pmf.get_segment_manager());
   bip_vector<book, chainbase::allocator<book>> v(alloc);
   bip_vector<book, chainbase::allocator<book>> v2(alloc);

   check_pack_unpack(v, v2);
   auto a  = v[1].title.get_allocator();
   auto a2 = v2[1].authors[0].get_allocator();
      
   // check that objects inside the vectors are allocated within the pinnable_mapped_file segment
   BOOST_REQUIRE(a  && (chainbase::allocator<book>)(*a) == alloc);
   BOOST_REQUIRE(a2 && (chainbase::allocator<book>)(*a2) == alloc);
}
