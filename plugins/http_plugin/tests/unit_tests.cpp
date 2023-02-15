#include <appbase/application.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <iostream>
#include <string_view>
#include <thread>
#include <future>
#include <boost/exception/diagnostic_information.hpp>


#define BOOST_TEST_MODULE http_plugin unit tests
#include <boost/test/included/unit_test.hpp>

namespace bu = boost::unit_test;
namespace bpo = boost::program_options;

using std::string;
using std::vector;

using namespace appbase;
using namespace eosio;

// -------------------------------------------------------------------------
// this class handles some basic http requests.
// -------------------------------------------------------------------------
class Db
{
public:

private:
};

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(http_plugin_unit_tests)
{
      appbase::scoped_app app;

      http_plugin::set_defaults({
         .default_unix_socket_path = "",
         .default_http_port = 8888,
         .server_header = "/"
      });

      const char* argv[] = { bu::framework::current_test_case().p_name->c_str() };
      BOOST_CHECK(app->initialize<http_plugin>(sizeof(argv) / sizeof(char*), const_cast<char**>(argv)));

      std::promise<http_plugin&> plugin_promise;
      std::future<http_plugin&> plugin_fut = plugin_promise.get_future();
      std::thread app_thread( [&]() {
         app->startup();
         plugin_promise.set_value(app->get_plugin<http_plugin>());
         app->exec();
      } );

      auto plugin = plugin_fut.get();
      BOOST_CHECK(plugin.get_state() == abstract_plugin::started);

      app->quit();
      app_thread.join();
}
