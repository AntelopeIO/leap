#include <eosio/chain/application.hpp>
#include <eosio/http_plugin/http_plugin.hpp>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <boost/asio/basic_stream_socket.hpp>

#define BOOST_TEST_MODULE http_plugin unit tests
#include <boost/test/included/unit_test.hpp>

#include <iostream>
#include <thread>
#include <future>
#include <optional>

namespace bu = boost::unit_test;

using std::string;

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
   void add_api(http_plugin& p) {
      p.add_api({
            {  std::string("/hello"),
               [&](string&&, string&& body, url_response_callback&& cb) {
                  cb(200, fc::time_point::maximum(), fc::variant("world!"));
               }
            },
            {  std::string("/echo"),
               [&](string&&, string&& body, url_response_callback&& cb) {
                  cb(200, fc::time_point::maximum(), fc::variant(body));
               }
            },
            {  std::string("/check_ones"), // returns "yes" if body only has only '1' chars, "no" otherwise
               [&](string&&, string&& body, url_response_callback&& cb) {
                  bool ok = std::all_of(body.begin(), body.end(), [](char c) { return c == '1'; });
                  cb(200, fc::time_point::maximum(), fc::variant(ok ? string("yes") : string("no")));
               }
            },
         }, appbase::exec_queue::read_write);
   }

private:
};

// --------------------------------------------------------------------------
// Expect100ContinueProtocol sends requests using the `Expect "100-continue"`
// from HTTP 1.1
// --------------------------------------------------------------------------
template <class Results>
struct ProtocolCommon
{
   auto get_response() -> std::optional<string>  {
      try {
         beast::flat_buffer buffer;
         http::response<http::dynamic_body> res;
         http::read(stream, buffer, res);
         return { beast::buffers_to_string(res.body().data()) };
      }
      catch(std::exception const& e)
      {
         std::cerr << "Error: " << e.what() << std::endl;
         this->reconnect();
         return {};
      }
   }

   void reconnect()
   {
      try {
         stream.connect(results);
      } catch(...) {};
   }

   const char* host;
   beast::tcp_stream& stream;
   Results& results;
};

   
template <class Results>
struct BasicProtocol : public ProtocolCommon<Results>
{
   bool send_request(const char* r, const char* body, bool expect_fail) {
      try {
         http::request<http::string_body> req{http::verb::post, r, 11};
         req.set(http::field::host, this->host);
         req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
         if (body) {
            req.body() = body;
            req.prepare_payload();
         }
         return http::write(this->stream, req) != 0;
      }
      catch(std::exception const& e)
      {
         std::cerr << "Error: " << e.what() << std::endl;
         this->reconnect();
         return false;
      }
   }
};

template <class Results>
struct Expect100ContinueProtocol : public ProtocolCommon<Results>
{
   bool send_request(const char* r, const char* body, bool expect_fail) {
      try {
         http::request<http::string_body> req{http::verb::post, r, 11};
         req.set(http::field::host, this->host);
         req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
         if (body) {
            req.set(http::field::expect, "100-continue");
            req.body() = body;
            req.prepare_payload();
      
            http::request_serializer<http::string_body> sr{req};
            beast::error_code ec;
            http::write_header(this->stream, sr, ec);
            if (ec) {
               BOOST_CHECK_MESSAGE(expect_fail, "write_header failed");
               return false;
            }
            
            {
               http::response<http::string_body> res {};
               beast::flat_buffer buffer;
               http::read(this->stream, buffer, res, ec);
               // std::cerr << "Result: " << res.result() << '\n';
               if (ec) {
                  BOOST_CHECK_MESSAGE(expect_fail, "continue_ read  failed");
                  if (res.result() != http::status::continue_)
                  {
                     // The server indicated that it will not
                     // accept the request, so skip sending the body.
                     // actually we don't get here, the server closes the connection
                     BOOST_CHECK_MESSAGE(expect_fail, "server rejected 100-continue request");
                     this->reconnect();
                  }
                  return false;
               }
            }

            // Server is OK with the request, send the body
            http::write(this->stream, sr, ec);
            return !ec;
         } 
         return http::write(this->stream, req) != 0;
      }
      catch(std::exception const& e)
      {
         std::cerr << "Error: " << e.what() << std::endl;
         this->reconnect();
         return false;
      }
   }
};

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
template<class Protocol>
void check_request(Protocol& p, const char* r, const char* body,
                   std::optional<const char*> expected_response)
{
   if (p.send_request(r, body, !expected_response)) {
      auto resp = p.get_response();
      BOOST_CHECK(!resp || expected_response);
      if (expected_response) {
         // substr to remove enclosing '"' characters
         BOOST_CHECK(resp->substr(1, resp->size() - 2) == string(*expected_response));
      }
      if (resp)
         std::cout << *resp << '\n';
      else
         p.reconnect();
   } else
      BOOST_CHECK(!expected_response);
}

template<class Protocol>
void run_test(Protocol& p, size_t max_body_size)
{
   // try a echo
   check_request(p, "/echo", "hello", {"hello"});

   // try a simple request
   check_request(p, "/hello", nullptr, {"world!"});

   // check ones with small body
   check_request(p, "/check_ones", "111111111111111111111111", {"yes"});

   // check ones with long body exactly max_req_size - should work and return yes
   {
      string test_str;
      test_str.resize(max_body_size, '1');
      check_request(p, "/check_ones", test_str.c_str(), {"yes"});
   }

   // check ones with long body (should be rejected by http_plugin as over max_body_size
   {
      string test_str;
      test_str.resize(max_body_size + 1, '1');
      check_request(p, "/check_ones", test_str.c_str(), {}); // we don't expect a response
   }
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

   std::this_thread::sleep_for(std::chrono::milliseconds(100));
   
   Db db;
   db.add_api(http_plugin);

   size_t max_body_size = http_plugin.get_max_body_size();
   
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
         BasicProtocol<decltype(results)> p{ {host, stream, results} };
         run_test(p, max_body_size);
      }

      {
         Expect100ContinueProtocol<decltype(results)> p{ {host, stream, results} };
         run_test(p, max_body_size);
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
