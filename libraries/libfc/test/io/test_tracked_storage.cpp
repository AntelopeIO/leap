#define BOOST_TEST_MODULE tracked_storage
#include <boost/test/included/unit_test.hpp>
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
   BOOST_CHECK_EQUAL( storage.memory_size(), 5);
   BOOST_CHECK_EQUAL( storage.index().size(), 1);
   BOOST_CHECK(storage.insert(test_size{ 1, 4 }).second);
   BOOST_CHECK_EQUAL( storage.memory_size(), 9);
   BOOST_CHECK_EQUAL( storage.index().size(), 2);
   BOOST_CHECK(storage.insert(test_size{ 2, 15 }).second);
   BOOST_CHECK_EQUAL( storage.memory_size(), 24);
   BOOST_CHECK_EQUAL( storage.index().size(), 3);
   auto to_mod = storage.find(1);
   storage.modify(to_mod, [](test_size& ts) { ts.s = 14; });
   BOOST_CHECK_EQUAL( storage.memory_size(), 34);
   BOOST_CHECK_EQUAL( storage.index().size(), 3);
   storage.modify(to_mod, [](test_size& ts) { ts.s = 0; });
   BOOST_CHECK_EQUAL( storage.memory_size(), 20);
   BOOST_CHECK(!storage.insert(test_size{ 1, 100 }).second);
   BOOST_CHECK_EQUAL( storage.memory_size(), 20);
   BOOST_CHECK_EQUAL( storage.index().size(), 3);
   storage.erase(2);
   BOOST_CHECK_EQUAL( storage.memory_size(), 5);
   BOOST_CHECK_NO_THROW(storage.erase(2));
   BOOST_CHECK_EQUAL( storage.memory_size(), 5);
   storage.erase( storage.index().find(0) );
   BOOST_CHECK_EQUAL( storage.memory_size(), 0);
}

BOOST_AUTO_TEST_CASE(simple_write_read_file_storage_test) {
   using tracked_storage1 = fc::tracked_storage<test_size_container>;
   tracked_storage1 storage1_1;
   BOOST_CHECK_EQUAL( storage1_1.memory_size(), 0);
   BOOST_CHECK_EQUAL( storage1_1.index().size(), 0);

   fc::temp_directory td;
   auto out = fc::persistence_util::open_cfile_for_write(td.path(), "temp.dat");
   fc::persistence_util::write_persistence_header(out, 0x12345678, 5);
   storage1_1.write(out);
   out.flush();
   out.close();

   auto content = fc::persistence_util::open_cfile_for_read(td.path(), "temp.dat");
   auto version = fc::persistence_util::read_persistence_header(content, 0x12345678, 5, 5);
   BOOST_CHECK_EQUAL( version, 5 );
   auto ds = content.create_datastream();
   tracked_storage1 storage1_2;
   BOOST_CHECK(storage1_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage1_2.index().size(), 0);
   BOOST_CHECK_EQUAL( storage1_2.memory_size(), 0);

   const auto tellp = content.tellp();
   content.seek_end(0);
   BOOST_CHECK_EQUAL( content.tellp(), tellp );
}

BOOST_AUTO_TEST_CASE(single_write_read_file_storage_test) { try {
   using tracked_storage1 = fc::tracked_storage<test_size_container>;
   tracked_storage1 storage1_1;
   storage1_1.insert(test_size{ 0, 6 });
   BOOST_CHECK_EQUAL( storage1_1.memory_size(), 6);
   BOOST_CHECK_EQUAL( storage1_1.index().size(), 1);
   fc::temp_directory td;
   auto out = fc::persistence_util::open_cfile_for_write(td.path(), "temp.dat");
   fc::persistence_util::write_persistence_header(out, 0x12345678, 5);
   storage1_1.write(out);
   out.flush();
   out.close();

   auto content = fc::persistence_util::open_cfile_for_read(td.path(), "temp.dat");
   auto version = fc::persistence_util::read_persistence_header(content, 0x12345678, 5, 5);
   BOOST_CHECK_EQUAL( version, 5 );
   auto ds = content.create_datastream();
   tracked_storage1 storage1_2;
   BOOST_CHECK(storage1_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage1_2.index().size(), 1);
   const auto& primary_idx2 = storage1_2.index().get<by_key>();
   auto itr2 = primary_idx2.cbegin();
   BOOST_CHECK_EQUAL( itr2->key, 0);
   BOOST_CHECK_EQUAL( itr2->s, 6);
   BOOST_CHECK_EQUAL( storage1_2.memory_size(), 6);

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
   BOOST_CHECK_EQUAL( storage1_1.memory_size(), 40);
   BOOST_CHECK_EQUAL( storage1_1.index().size(), 8);

   fc::temp_directory td;
   auto out = fc::persistence_util::open_cfile_for_write(td.path(), "temp.dat");
   fc::persistence_util::write_persistence_header(out, 0x12345678, 5);
   storage1_1.write(out);

   using tracked_storage2 = fc::tracked_storage<test_size2_container>;
   tracked_storage2 storage2_1;
   const auto now = fc::time_point::now();
   storage2_1.insert(test_size2{ 3, now, 7 });
   BOOST_CHECK_EQUAL( storage2_1.memory_size(), 7);
   BOOST_CHECK_EQUAL( storage2_1.index().size(), 1);

   storage2_1.write(out);

   out.flush();
   out.close();

   auto content = fc::persistence_util::open_cfile_for_read(td.path(), "temp.dat");
   auto version = fc::persistence_util::read_persistence_header(content, 0x12345678, 5, 5);
   BOOST_CHECK_EQUAL( version, 5 );
   auto ds = content.create_datastream();
   tracked_storage1 storage1_2;
   BOOST_CHECK(storage1_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage1_2.index().size(), 8);
   const auto& primary_idx1_2 = storage1_2.index().get<by_key>();
   auto itr2 = primary_idx1_2.cbegin();
   BOOST_CHECK_EQUAL( itr2->key, 0);
   BOOST_CHECK_EQUAL( itr2->s, 6);
   BOOST_CHECK_EQUAL( (++itr2)->key, 3);
   BOOST_CHECK_EQUAL( itr2->s, 7);
   BOOST_CHECK_EQUAL( (++itr2)->key, 5);
   BOOST_CHECK_EQUAL( itr2->s, 3);
   BOOST_CHECK_EQUAL( (++itr2)->key, 9);
   BOOST_CHECK_EQUAL( itr2->s, 4);
   BOOST_CHECK_EQUAL( (++itr2)->key, 15);
   BOOST_CHECK_EQUAL( itr2->s, 6);
   BOOST_CHECK_EQUAL( (++itr2)->key, 16);
   BOOST_CHECK_EQUAL( itr2->s, 4);
   BOOST_CHECK_EQUAL( (++itr2)->key, 19);
   BOOST_CHECK_EQUAL( itr2->s, 3);
   BOOST_CHECK_EQUAL( (++itr2)->key, 25);
   BOOST_CHECK_EQUAL( itr2->s, 7);
   BOOST_CHECK_EQUAL( storage1_2.memory_size(), 40);

   tracked_storage2 storage2_2;
   BOOST_CHECK(storage2_2.read(ds, 500));
   BOOST_CHECK_EQUAL( storage2_2.index().size(), 1);
   const auto& primary_idx2_2 = storage2_2.index().get<by_key>();
   auto itr3 = primary_idx2_2.cbegin();
   BOOST_CHECK_EQUAL( itr3->key, 3);
   BOOST_CHECK( itr3->time == now);
   BOOST_CHECK_EQUAL( itr3->s, 7);

   const auto tellp = content.tellp();
   content.seek_end(0);
   BOOST_CHECK_EQUAL( content.tellp(), tellp );
}

BOOST_AUTO_TEST_SUITE_END()
