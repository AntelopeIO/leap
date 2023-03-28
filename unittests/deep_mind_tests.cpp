#include <eosio/testing/tester.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/io/cfile.hpp>
#include <eosio/chain/deep_mind.hpp>

#include <boost/test/unit_test.hpp>

#include <deep-mind.hpp>


using namespace eosio::testing;

extern void setup_test_logging();

struct deep_mind_log_fixture
{
   deep_mind_handler deep_mind_logger;
   fc::temp_cfile tmp;

   deep_mind_log_fixture() : tmp("ab")
   {
      auto cfg = fc::logging_config::default_config();

      cfg.appenders.push_back(
         appender_config( "deep-mind", "dmlog",
            mutable_variant_object()
               ( "file", tmp.file().get_file_path().c_str())
         ) );

      fc::logger_config lc;
      lc.name = "deep-mind";
      lc.level = fc::log_level::all;
      lc.appenders.push_back("deep-mind");
      cfg.loggers.push_back( lc );

      fc::configure_logging(cfg);
      setup_test_logging();

      deep_mind_logger.update_config(deep_mind_handler::deep_mind_config{.zero_elapsed = true});
      deep_mind_logger.update_logger("deep-mind");
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

namespace {

void compare_files(const std::string& filename1, const std::string& filename2)
{
   std::ifstream file1(filename1.c_str());
   std::ifstream file2(filename2.c_str());
   std::string line1, line2;
   int i = 1;
   while(std::getline(file2, line2))
   {
      if(!std::getline(file1, line1))
      {
         BOOST_TEST(false, "Unexpected end of input of file1 at line " << i);
         return;
      }
      if(line1 != line2)
      {
         BOOST_TEST(false, "Mismatch at line " << i << "\n+ " << line1 << "\n- " << line2);
         return;
      }
      ++i;
   }
   if(std::getline(file1, line1))
   {
      BOOST_TEST(false, "Expected end of file of file1 at line " << i);
   }
}

} // namespace

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

   auto log_output_path = tmp.file().get_file_path();

   if(save_log)
   {
      std::filesystem::copy(log_output_path, DEEP_MIND_LOGFILE, std::filesystem::copy_options::update_existing);
   }
   else
   {
      compare_files(log_output_path.string(), DEEP_MIND_LOGFILE);
   }
}

BOOST_AUTO_TEST_SUITE_END()
