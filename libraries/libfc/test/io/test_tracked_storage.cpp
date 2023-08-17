#include <boost/test/unit_test.hpp>
#include <fc/container/tracked_storage.hpp>
#include <fc/io/persistence_util.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <sstream>

using boost::multi_index_container;
using namespace boost::multi_index;

struct test_size {
   uint64_t key = 0;
   uint64_t s = 0;
};

FC_REFLECT( test_size, (key)(s) )

struct by_key;
typedef multi_index_container<
   test_size,
   indexed_by<
      hashed_unique< tag<by_key>, member<test_size, uint64_t, &test_size::key>, std::hash<uint64_t>>
   >
> test_size_container;

struct test_size2 {
   uint64_t key = 0;
   fc::time_point time; 
   uint64_t s = 0;
};

FC_REFLECT( test_size2, (key)(time)(s) )

namespace fc::tracked {
  template<>
  size_t memory_size(const test_size& t) {
     return t.s;
  }

  template<>
  size_t memory_size(const test_size2& t) {
     return t.s;
  }
}

struct by_time;
typedef multi_index_container<
   test_size2,
   indexed_by<
      hashed_unique< tag<by_key>, member<test_size2, uint64_t, &test_size2::key>, std::hash<uint64_t>>,
      ordered_non_unique< tag<by_time>, member<test_size2, fc::time_point, &test_size2::time> >
   >
> test_size2_container;


BOOST_AUTO_TEST_SUITE(tracked_storage_tests)

BOOST_AUTO_TEST_CASE(track_storage_test) {
   fc::tracked_storage<test_size_container> storage;
   BOOST_CHECK(storage.insert(test_size{ 0, 5 }).second);
   BOOST_CHECK_EQUAL( storage.memory_size(), 5u);
   BOOST_CHECK_EQUAL( storage.index().size(), 1u);
   BOOST_CHECK(storage.insert(test_size{ 1, 4 }).second);
   BOOST_CHECK_EQUAL( storage.memory_size(), 9u);
   BOOST_CHECK_EQUAL( storage.index().size(), 2u);
   BOOST_CHECK(storage.insert(test_size{ 2, 15 }).second);
   BOOST_CHECK_EQUAL( storage.memory_size(), 24u);
   BOOST_CHECK_EQUAL( storage.index().size(), 3u);
   auto to_mod = storage.find(1);
   storage.modify(to_mod, [](test_size& ts) { ts.s = 14; });
   BOOST_CHECK_EQUAL( storage.memory_size(), 34u);
   BOOST_CHECK_EQUAL( storage.index().size(), 3u);
   storage.modify(to_mod, [](test_size& ts) { ts.s = 0; });
   BOOST_CHECK_EQUAL( storage.memory_size(), 20u);
   BOOST_CHECK(!storage.insert(test_size{ 1, 100 }).second);
   BOOST_CHECK_EQUAL( storage.memory_size(), 20u);
   BOOST_CHECK_EQUAL( storage.index().size(), 3u);
   storage.erase(2);
   BOOST_CHECK_EQUAL( storage.memory_size(), 5u);
   BOOST_CHECK_NO_THROW(storage.erase(2));
   BOOST_CHECK_EQUAL( storage.memory_size(), 5u);
   storage.erase( storage.index().find(0) );
   BOOST_CHECK_EQUAL( storage.memory_size(), 0u);
}

BOOST_AUTO_TEST_CASE(simple_write_read_file_storage_test) {
   using tracked_storage1 = fc::tracked_storage<test_size_container>;
   tracked_storage1 storage1_1;
   BOOST_CHECK_EQUAL( storage1_1.memory_size(), 0u);
   BOOST_CHECK_EQUAL( storage1_1.index().size(), 0u);

   fc::temp_directory td;
   auto out = fc::persistence_util::open_cfile_for_write(td.path(), "temp.dat");
   fc::persistence_util::write_persistence_header(out, 0x12345678, 5);
   storage1_1.write(out);
   out.flush();
   out.close();

   auto content = fc::persistence_util::open_cfile_for_read(td.path(), "temp.dat");
   auto version = fc::persistence_util::read_persistence_header(content, 0x12345678, 5, 5);
   BOOST_CHECK_EQUAL( version, 5u );
   auto ds = content.create_datastream();
   tracked_storage1 storage1_2;
   BOOST_CHECK(storage1_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage1_2.index().size(), 0u);
   BOOST_CHECK_EQUAL( storage1_2.memory_size(), 0u);

   const auto tellp = content.tellp();
   content.seek_end(0);
   BOOST_CHECK_EQUAL( content.tellp(), tellp );
}

BOOST_AUTO_TEST_CASE(single_write_read_file_storage_test) { try {
   using tracked_storage1 = fc::tracked_storage<test_size_container>;
   tracked_storage1 storage1_1;
   storage1_1.insert(test_size{ 0, 6 });
   BOOST_CHECK_EQUAL( storage1_1.memory_size(), 6u);
   BOOST_CHECK_EQUAL( storage1_1.index().size(), 1u);
   fc::temp_directory td;
   auto out = fc::persistence_util::open_cfile_for_write(td.path(), "temp.dat");
   fc::persistence_util::write_persistence_header(out, 0x12345678, 5);
   storage1_1.write(out);
   out.flush();
   out.close();

   auto content = fc::persistence_util::open_cfile_for_read(td.path(), "temp.dat");
   auto version = fc::persistence_util::read_persistence_header(content, 0x12345678, 5, 5);
   BOOST_CHECK_EQUAL( version, 5u );
   auto ds = content.create_datastream();
   tracked_storage1 storage1_2;
   BOOST_CHECK(storage1_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage1_2.index().size(), 1u);
   const auto& primary_idx2 = storage1_2.index().get<by_key>();
   auto itr2 = primary_idx2.cbegin();
   BOOST_CHECK_EQUAL( itr2->key, 0u);
   BOOST_CHECK_EQUAL( itr2->s, 6u);
   BOOST_CHECK_EQUAL( storage1_2.memory_size(), 6u);

   const auto tellp = content.tellp();
   content.seek_end(0);
   BOOST_CHECK_EQUAL( content.tellp(), tellp );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(write_read_file_storage_test) {
   using tracked_storage1 = fc::tracked_storage<test_size_container>;
   tracked_storage1 storage1_1;
   storage1_1.insert(test_size{ 0, 6 });
   storage1_1.insert(test_size{ 3, 7 });
   storage1_1.insert(test_size{ 5, 3 });
   storage1_1.insert(test_size{ 9, 4 });
   storage1_1.insert(test_size{ 15, 6 });
   storage1_1.insert(test_size{ 16, 4 });
   storage1_1.insert(test_size{ 19, 3 });
   storage1_1.insert(test_size{ 25, 7 });
   BOOST_CHECK_EQUAL( storage1_1.memory_size(), 40u);
   BOOST_CHECK_EQUAL( storage1_1.index().size(), 8u);

   fc::temp_directory td;
   auto out = fc::persistence_util::open_cfile_for_write(td.path(), "temp.dat");
   fc::persistence_util::write_persistence_header(out, 0x12345678, 5);
   storage1_1.write(out);

   using tracked_storage2 = fc::tracked_storage<test_size2_container>;
   tracked_storage2 storage2_1;
   const auto now = fc::time_point::now();
   storage2_1.insert(test_size2{ 3, now, 7 });
   BOOST_CHECK_EQUAL( storage2_1.memory_size(), 7u);
   BOOST_CHECK_EQUAL( storage2_1.index().size(), 1u);

   storage2_1.write(out);

   out.flush();
   out.close();

   auto content = fc::persistence_util::open_cfile_for_read(td.path(), "temp.dat");
   auto version = fc::persistence_util::read_persistence_header(content, 0x12345678, 5, 5);
   BOOST_CHECK_EQUAL( version, 5u );
   auto ds = content.create_datastream();
   tracked_storage1 storage1_2;
   BOOST_CHECK(storage1_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage1_2.index().size(), 8u);
   const auto& primary_idx1_2 = storage1_2.index().get<by_key>();
   auto itr2 = primary_idx1_2.cbegin();
   BOOST_CHECK_EQUAL( itr2->key, 0u);
   BOOST_CHECK_EQUAL( itr2->s, 6u);
   BOOST_CHECK_EQUAL( (++itr2)->key, 3u);
   BOOST_CHECK_EQUAL( itr2->s, 7u);
   BOOST_CHECK_EQUAL( (++itr2)->key, 5u);
   BOOST_CHECK_EQUAL( itr2->s, 3u);
   BOOST_CHECK_EQUAL( (++itr2)->key, 9u);
   BOOST_CHECK_EQUAL( itr2->s, 4u);
   BOOST_CHECK_EQUAL( (++itr2)->key, 15u);
   BOOST_CHECK_EQUAL( itr2->s, 6u);
   BOOST_CHECK_EQUAL( (++itr2)->key, 16u);
   BOOST_CHECK_EQUAL( itr2->s, 4u);
   BOOST_CHECK_EQUAL( (++itr2)->key, 19u);
   BOOST_CHECK_EQUAL( itr2->s, 3u);
   BOOST_CHECK_EQUAL( (++itr2)->key, 25u);
   BOOST_CHECK_EQUAL( itr2->s, 7u);
   BOOST_CHECK_EQUAL( storage1_2.memory_size(), 40u);

   tracked_storage2 storage2_2;
   BOOST_CHECK(storage2_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage2_2.index().size(), 1u);
   const auto& primary_idx2_2 = storage2_2.index().get<by_key>();
   auto itr3 = primary_idx2_2.cbegin();
   BOOST_CHECK_EQUAL( itr3->key, 3u);
   BOOST_CHECK( itr3->time == now);
   BOOST_CHECK_EQUAL( itr3->s, 7u);

   const auto tellp = content.tellp();
   content.seek_end(0);
   BOOST_CHECK_EQUAL( content.tellp(), tellp );
}

BOOST_AUTO_TEST_SUITE_END()
