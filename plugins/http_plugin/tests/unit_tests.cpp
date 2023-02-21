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
            {  std::string("/hello"),
                  [&](string, string body, url_response_callback cb) {
                  cb(200, fc::time_point::maximum(), fc::variant("world!"));
               }
            },
            {  std::string("/echo"),
                  [&](string, string body, url_response_callback cb) {
                  cb(200, fc::time_point::maximum(), fc::variant(body));
               }
            },
               
         });
   }

private:
};

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
struct BasicProtocol
{
   bool send_request(const char* r, const char* body) {
      http::request<http::string_body> req{http::verb::post, r, 11};
      req.set(http::field::host, host);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      if (body) {
         req.body() = body;
         req.prepare_payload();
      }
      return http::write(stream, req) != 0;
   };

   auto get_response() -> string {
      beast::flat_buffer buffer;
      http::response<http::dynamic_body> res;
      http::read(stream, buffer, res);
      return beast::buffers_to_string(res.body().data());
   };

   const char* host;
   beast::tcp_stream& stream;
};

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
struct Expect100ContinueProtocol
{
   bool send_request(const char* r, const char* body) {
      http::request<http::string_body> req{http::verb::post, r, 11};
      req.set(http::field::host, host);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      if (body) {
         req.set(http::field::expect, "100-continue");
         req.body() = body;
         req.prepare_payload();
      
         http::request_serializer<http::string_body> sr{req};
         beast::error_code ec;
         http::write_header(stream, sr, ec);
         BOOST_CHECK_MESSAGE(!ec, "write_header failed");
         if (ec)
            return false;
      
         {
            http::response<http::string_body> res;
            beast::flat_buffer buffer;
            http::read(stream, buffer, res, ec);
            BOOST_CHECK_MESSAGE(!ec, "continue_ read  failed");
         
            if (ec)
               return false;
         
            if (res.result() != http::status::continue_)
            {
               // The server indicated that it will not
               // accept the request, so skip sending the body.
               BOOST_CHECK_MESSAGE(false, "server rejected 100-continue request");
               return false;
            }
         }

         // Server is OK with the request, send the body
         http::write(stream, sr, ec);
         return !ec;
      } 
      return http::write(stream, req) != 0;
   };

   auto get_response() -> string {
      beast::flat_buffer buffer;
      http::response<http::dynamic_body> res;
      http::read(stream, buffer, res);
      return beast::buffers_to_string(res.body().data());
   };

   const char* host;
   beast::tcp_stream& stream;
};

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
template<class Protocol>
void run_test(Protocol &p)
{
   // try a simple request
   if (p.send_request("/hello", nullptr))
      BOOST_CHECK(p.get_response() == string("\"world!\""));
   else
      BOOST_CHECK(false);

   // try a echo
   if (p.send_request("/echo", "hello"))
      BOOST_CHECK(p.get_response() == string("\"hello\""));
   else
      BOOST_CHECK(false);
   //std::cout << p.get_response() << '\n';
}   

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

   try
   {
      net::io_context ioc;

      // These objects perform our I/O
      tcp::resolver resolver(ioc);
      beast::tcp_stream stream(ioc);

      // Look up IP and connect to it
      auto const results = resolver.resolve(host, port);
      stream.connect(results);

      {
         BasicProtocol p { host, stream };
         run_test(p);
      }

      {
         Expect100ContinueProtocol p { host, stream };
         run_test(p);
      }


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
