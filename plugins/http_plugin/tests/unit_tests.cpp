#include <eosio/chain/application.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/http_plugin/common.hpp>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <fc/scoped_exit.hpp>
#include <fc/crypto/rand.hpp>

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
               api_category::node,
               [&](string&&, string&& body, url_response_callback&& cb) {
                  cb(200, fc::variant("world!"));
               }
            },
            {  std::string("/echo"),
               api_category::node,
               [&](string&&, string&& body, url_response_callback&& cb) {
                  cb(200, fc::variant(body));
               }
            },
            {  std::string("/check_ones"), // returns "yes" if body only has only '1' chars, "no" otherwise
               api_category::node,
               [&](string&&, string&& body, url_response_callback&& cb) {
                  bool ok = std::all_of(body.begin(), body.end(), [](char c) { return c == '1'; });
                  cb(200, fc::variant(ok ? string("yes") : string("no")));
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

namespace eosio {
class chain_api_plugin : public appbase::plugin<chain_api_plugin> {
 public:
   APPBASE_PLUGIN_REQUIRES();
   virtual void set_program_options(options_description& cli, options_description& cfg) override {}
   void         plugin_initialize(const variables_map& options) {}
   void         plugin_startup() {}
   void         plugin_shutdown() {}
};

class net_api_plugin : public appbase::plugin<net_api_plugin> {
 public:
   APPBASE_PLUGIN_REQUIRES();
   virtual void set_program_options(options_description& cli, options_description& cfg) override {}
   void         plugin_initialize(const variables_map& options) {}
   void         plugin_startup() {}
   void         plugin_shutdown() {}
};

class producer_api_plugin : public appbase::plugin<producer_api_plugin> {
 public:
   APPBASE_PLUGIN_REQUIRES();
   virtual void set_program_options(options_description& cli, options_description& cfg) override {}
   void         plugin_initialize(const variables_map& options) {}
   void         plugin_startup() {}
   void         plugin_shutdown() {}
};

static auto _chain_api_plugin    = application::register_plugin<chain_api_plugin>();
static auto _net_api_plugin      = application::register_plugin<net_api_plugin>();
static auto _producer_api_plugin = application::register_plugin<producer_api_plugin>();
} // namespace eosio

struct http_plugin_test_fixture {
   appbase::scoped_app app;
   std::thread         app_thread;

   http_plugin* init(std::initializer_list<const char*> args) {
      if (app->initialize<http_plugin>(args.size(), const_cast<char**>(args.begin()))) {
         auto                       plugin  = app->find_plugin<http_plugin>();
         std::atomic<bool>& listening_or_failed = plugin->listening();
         app_thread = std::thread([&]() {
            try {
               app->startup();
               app->exec();
            } catch (...) {
               plugin = nullptr;
               listening_or_failed.store(true);
            }
         });

         while (!listening_or_failed.load()) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(10ms);
         }
         return plugin;
      }
      return nullptr;
   }

   ~http_plugin_test_fixture() {
      if (app_thread.joinable()) {
         app->quit();
         app_thread.join();
      }
   }
};

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(http_plugin_unit_tests, http_plugin_test_fixture) {

   const uint16_t default_port{8888};
   const char*    port = "8888";
   const char*    host = "127.0.0.1";

   http_plugin::set_defaults({.default_unix_socket_path = "", .default_http_port = default_port, .server_header = "/"});

   auto http_plugin =
       init({bu::framework::current_test_case().p_name->c_str(), "--plugin", "eosio::http_plugin",
             "--http-validate-host", "false", "--http-threads", "4", "--http-max-response-time-ms", "50"});

   BOOST_REQUIRE(http_plugin);
   BOOST_CHECK(http_plugin->get_state() == abstract_plugin::started);

   Db db;
   db.add_api(*http_plugin);

   size_t max_body_size = http_plugin->get_max_body_size();

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
      std::cerr << "Error: " << e.what() << "\n";
   }
}

class app_log {
   std::string result;
   int         fork_app_and_redirect_stderr(const char* redirect_filename, std::initializer_list<const char*> args) {
      int pid = fork();
      if (pid == 0) {
         (void) freopen(redirect_filename, "w", stderr);
         bool ret = 0;
         try {
            appbase::scoped_app app;
            ret = app->initialize<http_plugin>(args.size(), const_cast<char**>(args.begin()));
         } catch (...) {
         }
         fclose(stderr);
         exit(ret ? 0 : 1);
      } else {
         int chld_state;
         waitpid(pid, &chld_state, 0);
         BOOST_CHECK(WIFEXITED(chld_state));
         return WEXITSTATUS(chld_state);
      }
   }

 public:
   app_log(std::initializer_list<const char*> args) {
      fc::temp_directory dir;
      std::filesystem::path log = dir.path()/"test.stderr";
      BOOST_CHECK(fork_app_and_redirect_stderr(log.c_str(), args));
      std::ifstream file(log.c_str());
      result.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      std::filesystem::remove(log);
   }

   boost::test_tools::predicate_result contains(const char* str) const {
      if (result.find(str) == std::string::npos) {
         boost::test_tools::predicate_result res(false);
         res.message() << "\nlog result: " << result << "\n";
         return res;
      }
      return true;
   }
};

BOOST_AUTO_TEST_CASE(invalid_category_addresses) {

   const char* test_name = bu::framework::current_test_case().p_name->c_str();

   BOOST_TEST(app_log({test_name, "--plugin=eosio::http_plugin", "--http-server-address",
                                 "http-category-address", "--http-category-address", "chain_ro,localhost:8889"})
                  .contains("--plugin=eosio::chain_api_plugin is required"));

   BOOST_TEST(app_log({test_name, "--plugin=eosio::chain_api_plugin", "--http-category-address",
                                 "chain_ro,localhost:8889"})
                  .contains("http-server-address must be set as `http-category-address`"));

   BOOST_TEST(app_log({test_name, "--plugin=eosio::chain_api_plugin", "--http-server-address",
                                 "http-category-address", "--unix-socket-path", "/tmp/tmp.sock",
                                 "--http-category-address", "chain_ro,localhost:8889"})
                  .contains("`unix-socket-path` must be left unspecified"));

   BOOST_TEST(app_log({test_name, "--plugin=eosio::chain_api_plugin", "--http-server-address",
                                 "http-category-address", "--http-category-address", "node,localhost:8889"})
                  .contains("invalid category name"));

   BOOST_TEST(app_log({test_name, "--plugin=eosio::chain_api_plugin", "--http-server-address",
                                 "http-category-address", "--http-category-address", "chain_ro,127.0.0.1:8889",
                                 "--http-category-address", "chain_rw,localhost:8889"})
                  .contains("unable to listen to port 8889"));
}

struct http_response_for {
   net::io_context                    ioc;
   http::response<http::dynamic_body> response;
   http_response_for(const char* addr, const char* path) {
      auto [host, port] = fc::split_host_port(addr);
      // These objects perform our I/O
      tcp::resolver     resolver(ioc);
      beast::tcp_stream stream(ioc);

      // Look up IP and connect to it
      auto const results = resolver.resolve(host, port);
      stream.connect(results);
      initiate(stream, addr, path);
   }

   http_response_for(std::filesystem::path addr, const char* path) {
      using unix_stream = beast::basic_stream<boost::asio::local::stream_protocol, beast::tcp_stream::executor_type,
                                              beast::unlimited_rate_policy>;

      unix_stream stream(ioc);
      stream.connect(addr.c_str());
      initiate(stream, "", path);
   }

   template <typename Stream>
   void initiate(Stream&& stream, const char* addr, const char* path) {
      int                              http_version = 11;
      http::request<http::string_body> req{http::verb::post, path, http_version};
      if (addr)
         req.set(http::field::host, addr);
      req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      BOOST_CHECK(http::write(stream, req) != 0);

      beast::flat_buffer buffer;
      http::read(stream, buffer, response);
   }

   http::status status() const { return response.result(); }

   std::string body() const { return beast::buffers_to_string(response.body().data()); }
};

BOOST_FIXTURE_TEST_CASE(valid_category_addresses, http_plugin_test_fixture) {
   fc::temp_directory dir;
   auto               data_dir = dir.path() / "data";

   // clang-format off
   auto http_plugin = init({bu::framework::current_test_case().p_name->c_str(),
                      "--data-dir", data_dir.c_str(),
                      "--plugin=eosio::chain_api_plugin",
                      "--plugin=eosio::net_api_plugin",
                      "--plugin=eosio::producer_api_plugin",
                      "--http-server-address", "http-category-address",
                      "--http-category-address", "chain_ro,127.0.0.1:8890",
                      "--http-category-address", "chain_rw,:8889",
                      "--http-category-address", "net_ro,127.0.0.1:8890",
                      "--http-category-address", "net_rw,:8889",
                      "--http-category-address", "producer_ro,./producer_ro.sock",
                      "--http-category-address", "producer_rw,../producer_rw.sock"
                      });
   // clang-format on

   BOOST_REQUIRE(http_plugin);

   http_plugin->add_api({{std::string("/v1/node/hello"), api_category::node,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, fc::variant("world!"));
                          }},
                         {std::string("/v1/chain_ro/hello"), api_category::chain_ro,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, fc::variant("world!"));
                          }},
                         {std::string("/v1/chain_rw/hello"), api_category::chain_rw,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, fc::variant("world!"));
                          }},
                         {std::string("/v1/net_ro/hello"), api_category::net_ro,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, fc::variant("world!"));
                          }},
                         {std::string("/v1/net_rw/hello"), api_category::net_rw,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, fc::variant("world!"));
                          }},
                         {std::string("/v1/producer_ro/hello"), api_category::producer_ro,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, fc::variant("world!"));
                          }},
                         {std::string("/v1/producer_rw/hello"), api_category::producer_rw,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, fc::variant("world!"));
                          }}},
                        appbase::exec_queue::read_write);

   BOOST_CHECK(http_plugin->is_on_loopback(api_category::chain_ro));
   BOOST_CHECK(http_plugin->is_on_loopback(api_category::net_ro));
   BOOST_CHECK(http_plugin->is_on_loopback(api_category::producer_ro));
   BOOST_CHECK(http_plugin->is_on_loopback(api_category::producer_rw));
   BOOST_CHECK(!http_plugin->is_on_loopback(api_category::chain_rw));
   BOOST_CHECK(!http_plugin->is_on_loopback(api_category::net_rw));

   std::string world_string = "\"world!\"";

   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8890", "/v1/node/hello").body(), world_string);
   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8889", "/v1/node/hello").body(), world_string);

   bool ip_v6_enabled = [] {
      try {
         net::io_context ioc;
         tcp::socket     s(ioc, tcp::endpoint{net::ip::address::from_string("::1"), 9999});
         return true;
      } catch (...) {
         return false;
      }
   }();

   if (ip_v6_enabled) {
      BOOST_CHECK_EQUAL(http_response_for("[::1]:8889", "/v1/node/hello").body(), world_string);
   }

   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8890", "/v1/chain_ro/hello").body(), world_string);
   BOOST_CHECK_EQUAL(http_response_for("localhost:8890", "/v1/chain_ro/hello").status(), http::status::bad_request);
   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8890", "/v1/net_ro/hello").body(), world_string);
   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8890", "/v1/chain_rw/hello").status(), http::status::not_found);
   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8890", "/v1/net_rw/hello").status(), http::status::not_found);

   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8889", "/v1/chain_ro/hello").status(), http::status::not_found);
   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8889", "/v1/net_ro/hello").status(), http::status::not_found);
   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8889", "/v1/chain_rw/hello").body(), world_string);
   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8889", "/v1/net_rw/hello").body(), world_string);

   BOOST_CHECK_EQUAL(http_response_for(data_dir / "./producer_ro.sock", "/v1/producer_ro/hello").body(), world_string);
   BOOST_CHECK_EQUAL(http_response_for(data_dir / "../producer_rw.sock", "/v1/producer_rw/hello").body(), world_string);

   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8890", "/v1/node/get_supported_apis").body(),
                     R"({"apis":["/v1/chain_ro/hello","/v1/net_ro/hello","/v1/node/hello"]})");

   BOOST_CHECK_EQUAL(http_response_for("127.0.0.1:8889", "/v1/node/get_supported_apis").body(),
                     R"({"apis":["/v1/chain_rw/hello","/v1/net_rw/hello","/v1/node/hello"]})");
}


bool on_loopback(std::initializer_list<const char*> args){
   appbase::scoped_app app;
   BOOST_REQUIRE(app->initialize<http_plugin>(args.size(), const_cast<char**>(args.begin())));
   return app->get_plugin<http_plugin>().is_on_loopback(api_category::chain_rw);
}

BOOST_AUTO_TEST_CASE(test_on_loopback) {
   BOOST_CHECK(on_loopback({"test", "--plugin=eosio::http_plugin", "--http-server-address", "", "--unix-socket-path=a"}));
   BOOST_CHECK(on_loopback({"test", "--plugin=eosio::http_plugin", "--http-server-address", "127.0.0.1:8888"}));
   BOOST_CHECK(on_loopback({"test", "--plugin=eosio::http_plugin", "--http-server-address", "localhost:8888"}));
   BOOST_CHECK(!on_loopback({"test", "--plugin=eosio::http_plugin", "--http-server-address", ":8888"}));
   BOOST_CHECK(!on_loopback({"test", "--plugin=eosio::http_plugin", "--http-server-address", "example.com:8888"}));
}

BOOST_FIXTURE_TEST_CASE(bytes_in_flight, http_plugin_test_fixture) {
   http_plugin* http_plugin = init({"--plugin=eosio::http_plugin",
                                    "--http-server-address=127.0.0.1:8891",
                                    "--http-max-bytes-in-flight-mb=64"});
   BOOST_REQUIRE(http_plugin);

   http_plugin->add_api({{std::string("/4megabyte"), api_category::node,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             fc::blob b;
                             b.data.resize(4*1024*1024);
                             fc::rand_bytes(b.data.data(), b.data.size());
                             cb(200, b);
                          }}}, appbase::exec_queue::read_write);

   boost::asio::io_context ctx;
   boost::asio::ip::tcp::resolver resolver(ctx);

   std::list<boost::asio::ip::tcp::socket> connections;

   auto send_4mb_requests = [&](unsigned count) {
      for(unsigned i = 0; i < count; ++i) {
         boost::asio::ip::tcp::socket& s = connections.emplace_back(ctx, boost::asio::ip::tcp::v4());
         //we can't control http_plugin's send buffer, but at least we can control our receive buffer size to help increase
         // chance of server blocking
         s.set_option(boost::asio::socket_base::receive_buffer_size(8*1024));
         s.connect(resolver.resolve("127.0.0.1", "8891")->endpoint());
         boost::beast::http::request<boost::beast::http::empty_body> req(boost::beast::http::verb::get, "/4megabyte", 11);
         req.keep_alive(true);
         req.set(http::field::host, "127.0.0.1:8891");
         boost::beast::http::write(s, req);
      }
   };

   auto drain_http_replies = [&](unsigned max = std::numeric_limits<unsigned>::max()) {
      std::unordered_map<boost::beast::http::status, size_t> count_of_status_replies;
      while(connections.size() && max--) {
         boost::beast::http::response<boost::beast::http::string_body> resp;
         boost::beast::flat_buffer buffer;
         boost::beast::http::read(connections.front(), buffer, resp);

         count_of_status_replies[resp.result()]++;

         connections.erase(connections.begin());
      }
      return count_of_status_replies;
   };


   //send a single request to start with
   send_4mb_requests(1u);
   std::unordered_map<boost::beast::http::status, size_t> r = drain_http_replies();
   BOOST_REQUIRE_EQUAL(r[boost::beast::http::status::ok], 1u);

   //load up 32, this should exceed max
   send_4mb_requests(32u);
   r = drain_http_replies();
   BOOST_REQUIRE_GT(r[boost::beast::http::status::ok], 0u);
   BOOST_REQUIRE_GT(r[boost::beast::http::status::service_unavailable], 0u);
   BOOST_REQUIRE_EQUAL(r[boost::beast::http::status::service_unavailable] + r[boost::beast::http::status::ok], 32u);

   //send some more requests
   send_4mb_requests(10u);
   r = drain_http_replies();
   BOOST_REQUIRE_EQUAL(r[boost::beast::http::status::ok], 10u);

   //load up some more requests that exceed max
   send_4mb_requests(32u);
   //make sure got to the point http threads had responses queued
   std::this_thread::sleep_for(std::chrono::seconds(1));
   //now rip these connections out before the responses are completely sent
   connections.clear();
   //send some requests that should work still
   send_4mb_requests(8u);
   r = drain_http_replies();
   BOOST_REQUIRE_EQUAL(r[boost::beast::http::status::ok], 8u);
}

BOOST_FIXTURE_TEST_CASE(requests_in_flight, http_plugin_test_fixture) {
   http_plugin* http_plugin = init({"--plugin=eosio::http_plugin",
                                    "--http-server-address=127.0.0.1:8892",
                                    "--http-max-in-flight-requests=16"});
   BOOST_REQUIRE(http_plugin);

   http_plugin->add_api({{std::string("/doit"), api_category::node,
                          [&](string&&, string&& body, url_response_callback&& cb) {
                             cb(200, "hello");
                          }}}, appbase::exec_queue::read_write);

   boost::asio::io_context ctx;
   boost::asio::ip::tcp::resolver resolver(ctx);

   std::list<boost::asio::ip::tcp::socket> connections;

   auto send_requests = [&](unsigned count) {
      for(unsigned i = 0; i < count; ++i) {
         boost::asio::ip::tcp::socket& s = connections.emplace_back(ctx, boost::asio::ip::tcp::v4());
         boost::asio::connect(s, resolver.resolve("127.0.0.1", "8892"));
         boost::beast::http::request<boost::beast::http::empty_body> req(boost::beast::http::verb::get, "/doit", 11);
         req.keep_alive(true);
         req.set(http::field::host, "127.0.0.1:8892");
         boost::beast::http::write(s, req);
      }
   };

   auto scan_http_replies = [&]() {
      std::unordered_map<boost::beast::http::status, size_t> count_of_status_replies;
      for(boost::asio::ip::tcp::socket& c : connections) {
         boost::beast::http::response<boost::beast::http::string_body> resp;
         boost::beast::flat_buffer buffer;
         boost::beast::http::read(c, buffer, resp);

         count_of_status_replies[resp.result()]++;

         if(resp.result() == boost::beast::http::status::ok)
            BOOST_REQUIRE(resp.keep_alive());
      }
      return count_of_status_replies;
   };


   //8 requests to start with
   send_requests(8u);
   std::unordered_map<boost::beast::http::status, size_t> r = scan_http_replies();
   BOOST_REQUIRE_EQUAL(r[boost::beast::http::status::ok], 8u);
   connections.clear();

   //24 requests will exceed threshold
   send_requests(24u);
   r = scan_http_replies();
   BOOST_REQUIRE_GT(r[boost::beast::http::status::ok], 0u);
   BOOST_REQUIRE_GT(r[boost::beast::http::status::service_unavailable], 0u);
   BOOST_REQUIRE_EQUAL(r[boost::beast::http::status::service_unavailable] + r[boost::beast::http::status::ok], 24u);
   connections.clear();

   //requests should still work
   send_requests(8u);
   r = scan_http_replies();
   BOOST_REQUIRE_EQUAL(r[boost::beast::http::status::ok], 8u);
   connections.clear();
}

//A warning for future tests: destruction of http_plugin_test_fixture sometimes does not destroy http_plugin's listeners. Tests
// added in the future should avoid reusing ports of other tests in http_plugin_unit_tests.