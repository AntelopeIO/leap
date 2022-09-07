#include <wasm_spec_tests.hpp>

const string wasm_str_traps_0 = base_dir + "/traps.0.wasm";
std::vector<uint8_t> wasm_traps_0= read_wasm(wasm_str_traps_0.c_str());

BOOST_DATA_TEST_CASE(traps_0_check_throw, boost::unit_test::data::xrange(0,6), index) { try {
   TESTER tester;
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_traps_0);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   BOOST_CHECK_THROW(push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t()), wasm_execution_error);
   tester.produce_block();
} FC_LOG_AND_RETHROW() }

const string wasm_str_traps_1 = base_dir + "/traps.1.wasm";
std::vector<uint8_t> wasm_traps_1= read_wasm(wasm_str_traps_1.c_str());

BOOST_DATA_TEST_CASE(traps_1_check_throw, boost::unit_test::data::xrange(0,4), index) { try {
   TESTER tester;
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_traps_1);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   BOOST_CHECK_THROW(push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t()), wasm_execution_error);
   tester.produce_block();
} FC_LOG_AND_RETHROW() }

const string wasm_str_traps_2 = base_dir + "/traps.2.wasm";
std::vector<uint8_t> wasm_traps_2= read_wasm(wasm_str_traps_2.c_str());

BOOST_DATA_TEST_CASE(traps_2_check_throw, boost::unit_test::data::xrange(0,8), index) { try {
   TESTER tester;
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_traps_2);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   BOOST_CHECK_THROW(push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t()), wasm_execution_error);
   tester.produce_block();
} FC_LOG_AND_RETHROW() }

const string wasm_str_traps_3 = base_dir + "/traps.3.wasm";
std::vector<uint8_t> wasm_traps_3= read_wasm(wasm_str_traps_3.c_str());

BOOST_DATA_TEST_CASE(traps_3_check_throw, boost::unit_test::data::xrange(0,14), index) { try {
   TESTER tester;
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_traps_3);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   BOOST_CHECK_THROW(push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t()), wasm_execution_error);
   tester.produce_block();
} FC_LOG_AND_RETHROW() }
