#define BOOST_TEST_MODULE custom_appbase_tests
#include <boost/test/included/unit_test.hpp>
#include <thread>
#include <iostream>

#include <eosio/chain/application.hpp>

using namespace appbase;

BOOST_AUTO_TEST_SUITE(custom_appbase_tests)

std::thread start_app_thread(appbase::scoped_app& app) {
   const char* argv[] = { boost::unit_test::framework::current_test_case().p_name->c_str() };
   BOOST_CHECK(app->initialize(sizeof(argv) / sizeof(char*), const_cast<char**>(argv)));
   app->startup();
   std::thread app_thread( [&]() {
      app->exec();
   } );
   return app_thread;
}

// verify functions from both queues are executed when execution window is not explictly set
BOOST_AUTO_TEST_CASE( default_exec_window ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);
   
   // post functions
   std::map<int, int> rslts {};
   int seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_only,  [&]() { rslts[0]=seq_num; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_write, [&]() { rslts[1]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[2]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_only,  [&]() { rslts[3]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,  [&]() { rslts[4]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write, [&]() { rslts[5]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,  [&]() { rslts[6]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[7]=seq_num; ++seq_num; } );

   // Stop app. Use the lowest priority to make sure this function to execute the last
   app->executor().post( priority::lowest, exec_queue::read_only, [&]() {
      // read_only_queue should only contain the current lambda function,
      // and read_write_queue should have executed all its functions
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().size(), 1); // pop()s after execute
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().size(), 0 );
      app->quit();
      } );
   app_thread.join();

   // both queues are cleared after execution 
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().empty(), true);

   // exactly number of both queues' functions processed
   BOOST_REQUIRE_EQUAL( rslts.size(), 8 );

   // same priority of functions executed by the post order
   BOOST_CHECK_LT( rslts[0], rslts[1] );  // medium
   BOOST_CHECK_LT( rslts[2], rslts[3] );  // high
   BOOST_CHECK_LT( rslts[3], rslts[7] );  // high
   BOOST_CHECK_LT( rslts[4], rslts[5] );  // low

   // higher priority posted earlier executed earlier
   BOOST_CHECK_LT( rslts[3], rslts[4] );
   BOOST_CHECK_LT( rslts[6], rslts[7] );
}

// verify functions only from read_only queue are processed during read window
BOOST_AUTO_TEST_CASE( execute_from_read_queue ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);
   
   // set to run functions from read_only queue only
   app->executor().set_to_read_window(1, [](){return false;});

   // post functions
   std::map<int, int> rslts {};
   int seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_write, [&]() { rslts[0]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_only,  [&]() { rslts[1]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[2]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_only,  [&]() { rslts[3]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,  [&]() { rslts[4]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write, [&]() { rslts[5]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,  [&]() { rslts[6]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,  [&]() { rslts[7]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_only,  [&]() { rslts[8]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[9]=seq_num; ++seq_num; } );

   // stop application. Use lowest at the end to make sure this executes the last
   app->executor().post( priority::lowest, exec_queue::read_only, [&]() {
      // read_queue should be empty (read window pops before execute) and write_queue should have all its functions
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().size(), 0); // pop()s before execute
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().size(), 4 );
      app->quit();
      } );
   app_thread.join();

   // both queues are cleared after execution 
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().empty(), true);

   // exactly number of posts processed
   BOOST_REQUIRE_EQUAL( rslts.size(), 6 );

   // same priority (high) of functions in read_queue executed by the post order
   BOOST_CHECK_LT( rslts[1], rslts[3] );

   // higher priority posted earlier in read_queue executed earlier
   BOOST_CHECK_LT( rslts[3], rslts[4] );
}

// verify no functions are executed during  read window if read_only queue is empty
BOOST_AUTO_TEST_CASE( execute_from_empty_read_queue ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);
   
   // set to run functions from read_only queue only
   app->executor().set_to_read_window(1, [](){return false;});

   // post functions
   std::map<int, int> rslts {};
   int seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_write, [&]() { rslts[0]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[1]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[2]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[3]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write, [&]() { rslts[4]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write, [&]() { rslts[5]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_write, [&]() { rslts[6]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_write, [&]() { rslts[7]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[8]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[9]=seq_num; ++seq_num; } );

   // Stop application. Use lowest at the end to make sure this executes the last
   app->executor().post( priority::lowest, exec_queue::read_only, [&]() {
      // read_queue should be empty (read window pops before execute) and write_queue should have all its functions
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().size(), 0); // pop()s before execute
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().size(), 10 );
      app->quit();
      } );
   app_thread.join();

   // both queues are cleared after execution 
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().empty(), true);

   // no results
   BOOST_REQUIRE_EQUAL( rslts.size(), 0 );
}

// verify functions from both queues are processed in write window
BOOST_AUTO_TEST_CASE( execute_from_both_queues ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);

   // set to run functions from both queues
   app->executor().is_write_window();

   // post functions
   std::map<int, int> rslts {};
   int seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_only,  [&]() { rslts[0]=seq_num; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_write, [&]() { rslts[1]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write, [&]() { rslts[2]=seq_num; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,  [&]() { rslts[3]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,  [&]() { rslts[4]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write, [&]() { rslts[5]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,  [&]() { rslts[6]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write, [&]() { rslts[7]=seq_num; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,  [&]() { rslts[8]=seq_num; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,  [&]() { rslts[9]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write, [&]() { rslts[10]=seq_num; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_write, [&]() { rslts[11]=seq_num; ++seq_num; } );

   // stop application. Use lowest at the end to make sure this executes the last
   app->executor().post( priority::lowest, exec_queue::read_only, [&]() {
      // read_queue should have current function and write_queue's functions are all executed 
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().size(), 1); // pop()s after execute
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().size(), 0 );
      app->quit();
      } );

   app_thread.join();

   // queues are emptied after quit
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue().empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue().empty(), true);

   // exactly number of posts processed
   BOOST_REQUIRE_EQUAL( rslts.size(), 12 );

   // all low must be processed the in order of posting
   BOOST_CHECK_LT( rslts[4], rslts[5] );
   BOOST_CHECK_LT( rslts[5], rslts[7] );
   BOOST_CHECK_LT( rslts[7], rslts[10] );

   // all medium must be processed the in order of posting
   BOOST_CHECK_LT( rslts[0], rslts[1] );
   BOOST_CHECK_LT( rslts[1], rslts[11] );

   // all functions posted after high before highest must be processed after high
   BOOST_CHECK_LT( rslts[2], rslts[3] );
   BOOST_CHECK_LT( rslts[2], rslts[4] );
   BOOST_CHECK_LT( rslts[2], rslts[5] );

   // all functions posted after highest must be processed after it
   BOOST_CHECK_LT( rslts[6], rslts[7] );
   BOOST_CHECK_LT( rslts[6], rslts[8] );
   BOOST_CHECK_LT( rslts[6], rslts[9] );
   BOOST_CHECK_LT( rslts[6], rslts[10] );
   BOOST_CHECK_LT( rslts[6], rslts[11] );
   BOOST_CHECK_LT( rslts[6], rslts[11] );
}

BOOST_AUTO_TEST_SUITE_END()
