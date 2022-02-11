#include <eosio/testing/tester.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/test/unit_test.hpp>

#include <deep-mind.hpp>

using namespace fc;
using namespace eosio::testing;

extern void setup_test_logging();

struct deep_mind_log_fixture
{
   fc::logger deep_mind_logger;
   fc::temp_file log_output;
   deep_mind_log_fixture()
   {
      auto cfg = fc::logging_config::default_config();

      cfg.appenders.push_back(
         appender_config( "deep-mind", "dmlog",
            mutable_variant_object()
               ( "file", log_output.path().preferred_string().c_str())
         ) );

      fc::logger_config lc;
      lc.name = "deep-mind";
      lc.level = fc::log_level::all;
      lc.appenders.push_back("deep-mind");
      cfg.loggers.push_back( lc );

      fc::configure_logging(cfg);
      setup_test_logging();

      fc::logger::update("deep-mind", deep_mind_logger);
   }
   ~deep_mind_log_fixture()
   {
      fc::configure_logging(fc::logging_config::default_config());
      setup_test_logging();
   }
};

struct deep_mind_tester : deep_mind_log_fixture, validating_tester
{
   deep_mind_tester() : validating_tester({}, &deep_mind_logger) {}
};


BOOST_AUTO_TEST_SUITE(deep_mind_tests)

BOOST_FIXTURE_TEST_CASE(deep_mind, deep_mind_tester)
{
   produce_block();

   create_account( "alice"_n );

   push_action(config::system_account_name, "updateauth"_n, "alice"_n, fc::mutable_variant_object()
               ("account", "alice")
               ("permission", "test1")
               ("parent", "active")
               ("auth", authority{{"eosio"_n, "active"_n}}));

   produce_block();

   bool save_log = [](){
      auto argc = boost::unit_test::framework::master_test_suite().argc;
      auto argv = boost::unit_test::framework::master_test_suite().argv;
      return std::find(argv, argv + argc, std::string("--save-dmlog")) != (argv + argc);
   }();

   if(save_log)
   {
      auto output_path = fc::path(DEEP_MIND_LOGFILE);
      if(fc::exists(output_path))
      {
         fc::remove(output_path);
      }
      fc::copy(log_output.path(), output_path);
   }
   else
   {
      BOOST_TEST(std::system(("diff -u \"" DEEP_MIND_LOGFILE "\" " + log_output.path().preferred_string()).c_str()) == 0);
   }
}

BOOST_AUTO_TEST_SUITE_END()
