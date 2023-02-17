#include <appbase/application.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <iostream>
#include <string_view>
#include <thread>
#include <future>
#include <boost/exception/diagnostic_information.hpp>

#include <boost/asio.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>

#include <boost/asio/basic_socket_acceptor.hpp>
#include <boost/asio/basic_socket_iostream.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/detail/config.hpp>

#define BOOST_TEST_MODULE http_plugin unit tests
#include <boost/test/included/unit_test.hpp>

namespace bu = boost::unit_test;
namespace bpo = boost::program_options;

using std::string;
using std::string_view;
using std::vector;

using namespace appbase;
using namespace eosio;

namespace beast = boost::beast;     // from <boost/beast.hpp>
namespace http  = beast::http;      // from <boost/beast/http.hpp>
namespace net   = boost::asio;      // from <boost/asio.hpp>
using tcp       = net::ip::tcp;     // from <boost/asio/ip/tcp.hpp>

// -------------------------------------------------------------------------
// this class handles some basic http requests.
// -------------------------------------------------------------------------
class Db
{
public:
   void add_api(http_plugin &p) {
      p.add_api({
            {  std::string("/Hello"),
                  [&](string, string body, url_response_callback cb) {
                  cb(200, fc::time_point::maximum(), fc::variant("World!"));
               }
            },
            {  std::string("/mirror"),
                  [&](string, string body, url_response_callback cb) {
                  cb(200, fc::time_point::maximum(), fc::variant());
               }
            },
               
         });
   }

private:
};

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(http_plugin_unit_tests)
{
   appbase::scoped_app app;

   
   const uint16_t default_port { 8888 };
   const char* port = "8888";
   const char* host = "127.0.0.1";
   
   http_plugin::set_defaults({
         .default_unix_socket_path = "",
         .default_http_port = default_port,
         .server_header = "/"
      });

   const char* argv[] = { bu::framework::current_test_case().p_name->c_str(),
                          "--http-validate-host", "false",
                          "--http-threads", "4", 
                          "--http-max-response-time-ms", "50" };
   
   BOOST_CHECK(app->initialize<http_plugin>(sizeof(argv) / sizeof(char*), const_cast<char**>(argv)));

   std::promise<http_plugin&> plugin_promise;
   std::future<http_plugin&> plugin_fut = plugin_promise.get_future();
   std::thread app_thread( [&]() {
      app->startup();
      plugin_promise.set_value(app->get_plugin<http_plugin>());
      app->exec();
   } );

   auto http_plugin = plugin_fut.get();
   BOOST_CHECK(http_plugin.get_state() == abstract_plugin::started);

   //do {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
   //} while (http_plugin.get_supported_apis().apis.empty());
   
   Db db;
   db.add_api(http_plugin);

   auto send_request = [&](const char* r, const char* body) {
      http::request<http::string_body> req{http::verb::post, r, 11};
      req.set(http::field::host, host);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      if (body)
         req.set(http::field::body, body);
      return req;
   };

   auto get_response = [&](beast::tcp_stream& stream) -> string {
      beast::flat_buffer buffer;
      http::response<http::dynamic_body> res;
      http::read(stream, buffer, res);
      return beast::buffers_to_string(res.body().data());
   };

   try
   {
      net::io_context ioc;

      // These objects perform our I/O
      tcp::resolver resolver(ioc);
      beast::tcp_stream stream(ioc);

      // Look up IP and connect to it
      auto const results = resolver.resolve(host, port);
      stream.connect(results);

      // try a simple request
      http::write(stream, send_request("/Hello", nullptr));
      BOOST_CHECK(get_response(stream) == string("\"World!\""));

      // Gracefully close the socket
      beast::error_code ec;
      stream.socket().shutdown(tcp::socket::shutdown_both, ec);
      if (ec && ec != beast::errc::not_connected) // not_connected happens sometimes
         throw beast::system_error{ec};
   }
   catch(std::exception const& e)
   {
      std::cerr << "Error: " << e.what() << std::endl;
   }

   app->quit();
   app_thread.join();
}
