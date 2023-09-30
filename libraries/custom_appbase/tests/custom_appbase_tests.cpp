#define BOOST_TEST_MODULE custom_appbase_tests
#include <boost/test/included/unit_test.hpp>

#include <eosio/chain/application.hpp>

#include <fc/log/logger_config.hpp>

#include <thread>
#include <iostream>

using namespace appbase;

BOOST_AUTO_TEST_SUITE(custom_appbase_tests)

std::thread start_app_thread(appbase::scoped_app& app) {
   const char* argv[] = { boost::unit_test::framework::current_test_case().p_name->c_str() };
   BOOST_CHECK(app->initialize(sizeof(argv) / sizeof(char*), const_cast<char**>(argv)));
   app->startup();
   std::thread app_thread( [&]() {
      app->executor().init_main_thread_id();
      app->exec();
   } );
   return app_thread;
}

std::thread start_read_thread(appbase::scoped_app& app) {
   static int num = 0;
   std::thread read_thread( [&]() {
      std::string name ="read-" + std::to_string(num++);
      fc::set_thread_name(name);
      bool more = true;
      while (more) {
         more = app->executor().execute_highest_read(); // blocks until all read only threads are idle
      }
   });
   return read_thread;
}

// verify functions from both queues (read_only,read_write) are executed when execution window is not explicitly set
BOOST_AUTO_TEST_CASE( default_exec_window ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);
   
   // post functions
   std::map<int, int> rslts {};
   int seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_only,      [&]() { rslts[0]=seq_num; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_write,     [&]() { rslts[1]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write,     [&]() { rslts[2]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_only,      [&]() { rslts[3]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,      [&]() { rslts[4]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write,     [&]() { rslts[5]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,      [&]() { rslts[6]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write,     [&]() { rslts[7]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_exclusive, [&]() { rslts[8]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_exclusive, [&]() { rslts[9]=seq_num; ++seq_num; } );

   // Stop app. Use the lowest priority to make sure this function to execute the last
   app->executor().post( priority::lowest, exec_queue::read_only, [&]() {
      // read_only_queue should only contain the current lambda function,
      // and read_write_queue should have executed all its functions
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_size(), 1u); // pop()s after execute
      BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_size(), 2u );
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_size(), 0u );
      app->quit();
      } );
   app_thread.join();

   // all queues are cleared when exiting application::exec()
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_empty(), true);

   // exactly number of both queues' functions processed
   BOOST_REQUIRE_EQUAL( rslts.size(), 8u );

   // same priority of functions executed by the post order
   BOOST_CHECK_LT( rslts[0], rslts[1] );  // medium
   BOOST_CHECK_LT( rslts[2], rslts[3] );  // high
   BOOST_CHECK_LT( rslts[3], rslts[7] );  // high
   BOOST_CHECK_LT( rslts[4], rslts[5] );  // low

   // higher priority posted earlier executed earlier
   BOOST_CHECK_LT( rslts[3], rslts[4] );
   BOOST_CHECK_LT( rslts[6], rslts[7] );
}

// verify functions only from read_only queue are processed during read window on the main thread
BOOST_AUTO_TEST_CASE( execute_from_read_only_queue ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);
   
   // set to run functions from read_only queue only
   app->executor().init_read_threads(1);
   app->executor().set_to_read_window([](){return false;});

   // post functions
   std::map<int, int> rslts {};
   int seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_write,     [&]() { rslts[0]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_only,      [&]() { rslts[1]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write,     [&]() { rslts[2]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_only,      [&]() { rslts[3]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,      [&]() { rslts[4]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write,     [&]() { rslts[5]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,      [&]() { rslts[6]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_exclusive, [&]() { rslts[7]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_exclusive, [&]() { rslts[8]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write,     [&]() { rslts[9]=seq_num; ++seq_num; } );

   // stop application. Use lowest at the end to make sure this executes the last
   app->executor().post( priority::lowest, exec_queue::read_only, [&]() {
      // read_queue should be empty (read window pops before execute) and write_queue should have all its functions
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_size(), 0u); // pop()s before execute
      BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_size(), 2u);
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_size(), 4u );
      app->quit();
      } );
   app_thread.join();

   // all queues are cleared when exiting application::exec()
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_empty(), true);

   // exactly number of posts processed
   BOOST_REQUIRE_EQUAL( rslts.size(), 4u );

   // same priority (high) of functions in read queues executed by the post order
   BOOST_CHECK_LT( rslts[1], rslts[3] );

   // higher priority posted earlier in read queues executed earlier
   BOOST_CHECK_LT( rslts[3], rslts[4] );
}

// verify no functions are executed during read window if read_only & read_exclusive queue is empty
BOOST_AUTO_TEST_CASE( execute_from_empty_read_only_queue ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);
   
   // set to run functions from read_only & read_exclusive queues only
   app->executor().init_read_threads(1);
   app->executor().set_to_read_window([](){return false;});

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
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_size(), 0u); // pop()s before execute
      BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_size(), 0u);
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_size(), 10u );
      app->quit();
      } );
   app_thread.join();

   // all queues are cleared when exiting application::exec()
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_empty(), true);

   // no results
   BOOST_REQUIRE_EQUAL( rslts.size(), 0u );
}

// verify functions from both queues (read_only, read_write) are processed in write window, but not read_exclusive
BOOST_AUTO_TEST_CASE( execute_from_read_only_and_read_write_queues ) {
   appbase::scoped_app app;
   auto app_thread = start_app_thread(app);

   // set to run functions from both queues
   app->executor().is_write_window();

   // post functions
   std::map<int, int> rslts {};
   int seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_only,      [&]() { rslts[0]=seq_num; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_write,     [&]() { rslts[1]=seq_num; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_write,     [&]() { rslts[2]=seq_num; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,      [&]() { rslts[3]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,      [&]() { rslts[4]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write,     [&]() { rslts[5]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,      [&]() { rslts[6]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write,     [&]() { rslts[7]=seq_num; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,      [&]() { rslts[8]=seq_num; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,      [&]() { rslts[9]=seq_num; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write,     [&]() { rslts[10]=seq_num; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_write,     [&]() { rslts[11]=seq_num; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_exclusive, [&]() { rslts[12]=seq_num; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_exclusive, [&]() { rslts[13]=seq_num; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_exclusive, [&]() { rslts[14]=seq_num; ++seq_num; } );

   // stop application. Use lowest at the end to make sure this executes the last
   app->executor().post( priority::lowest, exec_queue::read_only, [&]() {
      // read_queue should have current function and write_queue's functions are all executed 
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_size(), 1u); // pop()s after execute
      BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_size(), 3u);
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_size(), 0u );
      app->quit();
      } );

   app_thread.join();

   // queues are emptied after exec
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_empty(), true);

   // exactly number of posts processed
   BOOST_REQUIRE_EQUAL( rslts.size(), 12u );

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

// verify tasks from both queues (read_only, read_exclusive) are processed in read window
BOOST_AUTO_TEST_CASE( execute_from_read_only_and_read_exclusive_queues ) {
   appbase::scoped_app app;

   app->executor().init_read_threads(3);
   // set to run functions from read_only & read_exclusive queues only
   app->executor().set_to_read_window([](){return false;});

   // post functions
   std::vector<std::atomic<int>> rslts(16);
   std::atomic<int> seq_num = 0;
   app->executor().post( priority::medium, exec_queue::read_only,      [&]() { rslts.at(0)=1; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_exclusive, [&]() { rslts.at(1)=2; ++seq_num; } );
   app->executor().post( priority::high,   exec_queue::read_exclusive, [&]() { rslts.at(2)=3; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,      [&]() { rslts.at(3)=4; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,      [&]() { rslts.at(4)=5; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write,     [&]() { rslts.at(5)=6; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_only,      [&]() { rslts.at(6)=7; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_write,     [&]() { rslts.at(7)=8; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_only,      [&]() { rslts.at(8)=9; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_exclusive, [&]() { rslts.at(9)=10; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_write,     [&]() { rslts.at(10)=11; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_exclusive, [&]() { rslts.at(11)=12; ++seq_num; } );
   app->executor().post( priority::highest,exec_queue::read_exclusive, [&]() { rslts.at(12)=13; ++seq_num; } );
   app->executor().post( priority::lowest, exec_queue::read_exclusive, [&]() { rslts.at(13)=14; ++seq_num; } );
   app->executor().post( priority::medium, exec_queue::read_exclusive, [&]() { rslts.at(14)=15; ++seq_num; } );
   app->executor().post( priority::low,    exec_queue::read_only,      [&]() { rslts.at(15)=16; ++seq_num; } );

   // Use lowest at the end to make sure this executes the last
   app->executor().post( priority::lowest, exec_queue::read_exclusive, [&]() {
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_size(), 0u); // pop()s before execute
      BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_size(), 0u);
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_size(), 3u );
      } );


   std::optional<boost::asio::io_service::work> work;
   work.emplace(app->get_io_service());
   while( true ) {
      app->get_io_service().poll();
      size_t s = app->executor().read_only_queue_size() + app->executor().read_exclusive_queue_size() + app->executor().read_write_queue_size();
      if (s == 17)
         break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }

   auto app_thread = start_app_thread(app);
   constexpr int num_expected = 13; // 16 - 3 read_write

   auto read_thread1 = start_read_thread(app);
   auto read_thread2 = start_read_thread(app);
   auto read_thread3 = start_read_thread(app);
   read_thread1.join();
   read_thread2.join();
   read_thread3.join();

   size_t num_sleeps = 0;
   while (seq_num < num_expected) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (++num_sleeps > 10000)
         break;
   };
   work.reset();
   app->quit();
   app_thread.join();

   // queues are emptied after exec
   BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_empty(), true);
   BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_empty(), true);

   // exactly number of posts processed
   BOOST_REQUIRE_EQUAL( std::count_if(rslts.cbegin(), rslts.cend(), [](const auto& v){return v > 0; }), num_expected );

   // all low must be processed the in order of posting
   BOOST_CHECK_LT( rslts[4], rslts[15] );

   // all medium must be processed the in order of posting
   BOOST_CHECK_LT( rslts[0], rslts[1] );
   BOOST_CHECK_LT( rslts[1], rslts[11] );
   BOOST_CHECK_LT( rslts[11], rslts[14] );

   // all functions posted after high before highest must be processed after high
   BOOST_CHECK_LT( rslts[2], rslts[3] );
   BOOST_CHECK_LT( rslts[2], rslts[4] );
   BOOST_CHECK_LT( rslts[2], rslts[9] );

   // all functions posted after highest must be processed after it
   BOOST_CHECK_LT( rslts[6], rslts[8] );
   BOOST_CHECK_LT( rslts[6], rslts[9] );
   BOOST_CHECK_LT( rslts[6], rslts[11] );
   BOOST_CHECK_LT( rslts[6], rslts[12] );
   BOOST_CHECK_LT( rslts[6], rslts[14] );
}

// verify tasks from both queues (read_only, read_exclusive) are processed in read window
BOOST_AUTO_TEST_CASE( execute_many_from_read_only_and_read_exclusive_queues ) {
   appbase::scoped_app app;

   auto app_thread = start_app_thread(app);

   // set to run functions from read_only & read_exclusive queues only
   app->executor().init_read_threads(3);
   app->executor().set_to_read_window([](){return false;});

   // post functions
   constexpr int num_expected = 600;
   std::vector<std::atomic<std::thread::id>> rslts(num_expected);
   std::atomic<int> seq_num = 0;
   for (size_t i = 0; i < 200; i+=5) {
      app->executor().post( priority::high,   exec_queue::read_exclusive, [&,i]() { rslts.at(i)   = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(10)); } );
      app->executor().post( priority::low,    exec_queue::read_only,      [&,i]() { rslts.at(i+1) = std::this_thread::get_id(); ++seq_num; } );
      app->executor().post( priority::low,    exec_queue::read_exclusive, [&,i]() { rslts.at(i+2) = std::this_thread::get_id(); ++seq_num; } );
      app->executor().post( priority::high,   exec_queue::read_only,      [&,i]() { rslts.at(i+3) = std::this_thread::get_id(); ++seq_num; } );
      app->executor().post( priority::medium, exec_queue::read_only,      [&,i]() { rslts.at(i+4) = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(i+1)); } );
   }
   auto read_thread1 = start_read_thread(app);
   std::thread::id read_thread1_id = read_thread1.get_id();
   for (size_t i = 200; i < 400; i+=5) {
      app->executor().post( priority::high,   exec_queue::read_exclusive, [&,i]() { rslts.at(i)   = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(i)); } );
      app->executor().post( priority::low,    exec_queue::read_only,      [&,i]() { rslts.at(i+1) = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(i)); } );
      app->executor().post( priority::low,    exec_queue::read_exclusive, [&,i]() { rslts.at(i+2) = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(i)); } );
      app->executor().post( priority::high,   exec_queue::read_only,      [&,i]() { rslts.at(i+3) = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(i)); } );
      app->executor().post( priority::medium, exec_queue::read_exclusive, [&,i]() { rslts.at(i+4) = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(i)); } );
   }
   auto read_thread2 = start_read_thread(app);
   std::thread::id read_thread2_id = read_thread2.get_id();
   for (size_t i = 400; i < num_expected; i+=5) {
      app->executor().post( priority::high,   exec_queue::read_only,      [&,i]() { rslts.at(i)   = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(10)); } );
      app->executor().post( priority::low,    exec_queue::read_only,      [&,i]() { rslts.at(i+1) = std::this_thread::get_id(); ++seq_num; std::this_thread::sleep_for(std::chrono::microseconds(10)); } );
      app->executor().post( priority::low,    exec_queue::read_only,      [&,i]() { rslts.at(i+2) = std::this_thread::get_id(); ++seq_num; } );
      app->executor().post( priority::high,   exec_queue::read_only,      [&,i]() { rslts.at(i+3) = std::this_thread::get_id(); ++seq_num; } );
      app->executor().post( priority::medium, exec_queue::read_exclusive, [&,i]() { rslts.at(i+4) = std::this_thread::get_id(); ++seq_num; } );
   }
   auto read_thread3 = start_read_thread(app);
   std::thread::id read_thread3_id = read_thread3.get_id();

   // Use lowest at the end to make sure this executes the last
   app->executor().post( priority::lowest, exec_queue::read_exclusive, [&]() {
      BOOST_REQUIRE_EQUAL( app->executor().read_only_queue_size(), 0u); // pop()s before execute
      BOOST_REQUIRE_EQUAL( app->executor().read_exclusive_queue_size(), 0u);
      BOOST_REQUIRE_EQUAL( app->executor().read_write_queue_size(), 0u );
      } );

   read_thread1.join();
   read_thread2.join();
   read_thread3.join();

   size_t num_sleeps = 0;
   while (seq_num < num_expected) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (++num_sleeps > 10000)
         break;
   };

   app->quit();
   app_thread.join();

   // exactly number of posts processed
   BOOST_REQUIRE_EQUAL( std::count_if(rslts.cbegin(), rslts.cend(), [](const auto& v){ return v != std::thread::id(); }), num_expected );

   const auto run_on_1 = std::count_if(rslts.cbegin(), rslts.cend(), [&](const auto& v){ return v == read_thread1_id; });
   const auto run_on_2 = std::count_if(rslts.cbegin(), rslts.cend(), [&](const auto& v){ return v == read_thread2_id; });
   const auto run_on_3 = std::count_if(rslts.cbegin(), rslts.cend(), [&](const auto& v){ return v == read_thread3_id; });
   const auto run_on_main = std::count_if(rslts.cbegin(), rslts.cend(), [&](const auto& v){ return v == app->executor().get_main_thread_id(); });

   BOOST_REQUIRE_EQUAL(run_on_1+run_on_2+run_on_3+run_on_main, num_expected);
   BOOST_CHECK(run_on_1 > 0);
   BOOST_CHECK(run_on_2 > 0);
   BOOST_CHECK(run_on_3 > 0);
   BOOST_CHECK(run_on_main > 0);
}

BOOST_AUTO_TEST_SUITE_END()
