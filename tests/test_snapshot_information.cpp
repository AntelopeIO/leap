#include <boost/test/unit_test.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/testing/tester.hpp>
#include "snapshot_suites.hpp"
#include <eosio/chain/snapshot_scheduler.hpp>
#include <eosio/chain/pending_snapshot.hpp>
#include <test_contracts.hpp>
#include <snapshots.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace boost::system;

namespace {
    snapshot_scheduler::snapshot_information test_snap_info;
}

BOOST_AUTO_TEST_SUITE(producer_snapshot_tests)

using next_t = pending_snapshot<snapshot_scheduler::snapshot_information>::next_t;

BOOST_AUTO_TEST_CASE_TEMPLATE(test_snapshot_information, SNAPSHOT_SUITE, snapshot_suites) {
   tester chain;
   const std::filesystem::path parent_path = chain.get_config().blocks_dir.parent_path();

   chain.create_account("snapshot"_n);
   chain.produce_blocks(1);
   chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
   chain.produce_blocks(1);

   auto block = chain.produce_block();
   BOOST_REQUIRE_EQUAL(block->block_num(), 6u); // ensure that test setup stays consistent with original snapshot setup
   // undo the auto-pending from tester
   chain.control->abort_block();

   auto block2 = chain.produce_block();
   BOOST_REQUIRE_EQUAL(block2->block_num(), 7u); // ensure that test setup stays consistent with original snapshot setup
   // undo the auto-pending from tester
   chain.control->abort_block();

   // write snapshot
   auto write_snapshot = [&]( const std::filesystem::path& p ) -> void {
      if ( !std::filesystem::exists( p.parent_path() ) )
         std::filesystem::create_directory( p.parent_path() );

      // create the snapshot
      auto snap_out = std::ofstream(p.generic_string(), (std::ios::out | std::ios::binary));
      auto writer = std::make_shared<ostream_snapshot_writer>(snap_out);
      (*chain.control).write_snapshot(writer);
      writer->finalize();
      snap_out.flush();
      snap_out.close();
   };

   auto final_path = pending_snapshot<snapshot_scheduler::snapshot_information>::get_final_path(block2->previous, "../snapshots/");
   auto pending_path = pending_snapshot<snapshot_scheduler::snapshot_information>::get_pending_path(block2->previous, "../snapshots/");

   write_snapshot( pending_path );
   next_t next;
   pending_snapshot pending{ block2->previous, next, pending_path.generic_string(), final_path.generic_string() };
   test_snap_info = pending.finalize(*chain.control);
   BOOST_REQUIRE_EQUAL(test_snap_info.head_block_num, 6u);
   BOOST_REQUIRE_EQUAL(test_snap_info.version, chain_snapshot_header::current_version);
}

BOOST_AUTO_TEST_SUITE_END()
