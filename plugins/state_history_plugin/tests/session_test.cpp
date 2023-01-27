
#define BOOST_TEST_MODULE example
#include <boost/test/included/unit_test.hpp>

#include <algorithm>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <eosio/state_history_plugin/session.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <eosio/state_history/log.hpp>
#include <fc/bitutil.hpp>

namespace beast     = boost::beast;     // from <boost/beast.hpp>
namespace http      = beast::http;      // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net       = boost::asio;      // from <boost/asio.hpp>
namespace bio       = boost::iostreams;
using tcp           = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

namespace eosio::state_history {

template <typename ST>
inline void unpack_varuint64(fc::datastream<ST>& ds, uint64_t& val) {
   val           = 0;
   int     shift = 0;
   uint8_t b     = 0;
   do {
      fc::raw::unpack(ds, b);
      val |= uint64_t(b & 0x7f) << shift;
      shift += 7;
   } while (b & 0x80);
}

template <typename ST>
void unpack_big_bytes(fc::datastream<ST>& ds, eosio::chain::bytes& v) {
   uint64_t sz;
   unpack_varuint64(ds, sz);
   v.resize(sz);
   if (sz)
      ds.read(v.data(), v.size());
}

template <typename ST>
void unpack_big_bytes(fc::datastream<ST>& ds, std::optional<eosio::chain::bytes>& v) {
   bool has_value;
   fc::raw::unpack(ds, has_value);
   if (has_value) {
      unpack_big_bytes(ds, v.emplace());
   } else {
      v.reset();
   }
}

template <typename ST>
fc::datastream<ST>& operator>>(fc::datastream<ST>& ds, eosio::state_history::get_blocks_result_v0& obj) {
   fc::raw::unpack(ds, obj.head);
   fc::raw::unpack(ds, obj.last_irreversible);
   fc::raw::unpack(ds, obj.this_block);
   fc::raw::unpack(ds, obj.prev_block);
   unpack_big_bytes(ds, obj.block);
   unpack_big_bytes(ds, obj.traces);
   unpack_big_bytes(ds, obj.deltas);
   return ds;
}
} // namespace eosio::state_history

//------------------------------------------------------------------------------

fc::sha256 block_id_for(const uint32_t bnum) {
   fc::sha256 m = fc::sha256::hash(fc::sha256::hash(std::to_string(bnum)));
   m._hash[0]   = fc::endian_reverse_u32(bnum);
   return m;
}

// Report a failure
void fail(beast::error_code ec, char const* what) { std::cerr << what << ": " << ec.message() << "\n"; }

struct mock_state_history_plugin {
   net::io_context                         main_ioc;
   net::io_context                         ship_ioc;
   using ioc_work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
   std::optional<ioc_work_t>               main_ioc_work;
   std::optional<ioc_work_t>               ship_ioc_work;

   eosio::state_history::block_position    block_head;
   fc::temp_directory                      log_dir;
   std::optional<eosio::state_history_log> log;
   std::atomic<bool>                       stopping = false;
   std::set<std::shared_ptr<eosio::session_base>> session_set;

   constexpr static uint32_t default_frame_size = 1024;

   std::optional<eosio::state_history_log>& get_trace_log() { return log; }
   std::optional<eosio::state_history_log>& get_chain_state_log() { return log; }
   fc::sha256                get_chain_id() const { return {}; }

   boost::asio::io_context& get_ship_executor() { return ship_ioc; }

   void setup_state_history_log(eosio::state_history_log_config conf = {}) {
      log.emplace("ship", log_dir.path(), conf);
   }

   fc::logger logger() { return fc::logger::get(DEFAULT_LOGGER); }

   void get_block(uint32_t block_num, const eosio::chain::block_state_ptr& block_state,
                  std::optional<eosio::bytes>& result) const {
      result.emplace().resize(16);
   }

   fc::time_point get_head_block_timestamp() const {
      return fc::time_point{};
   }

   std::optional<eosio::chain::block_id_type> get_block_id(uint32_t block_num) { return block_id_for(block_num); }

   eosio::state_history::block_position get_block_head() { return block_head; }
   eosio::state_history::block_position get_last_irreversible() { return block_head; }

   void add_session(std::shared_ptr<eosio::session_base> s) {
      session_set.insert(s);
   }

   template <typename Task>
   void post_task_main_thread_medium(Task&& task) {
      boost::asio::post(main_ioc, std::forward<Task>(task));
   }

};

using session_type = eosio::session<mock_state_history_plugin*, tcp::socket>;

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener> {
   mock_state_history_plugin* server_;
   tcp::acceptor              acceptor_;

 public:
   listener(mock_state_history_plugin* server, tcp::endpoint& endpoint)
       : server_(server)
       , acceptor_(server->ship_ioc) {
      beast::error_code ec;

      // Open the acceptor
      acceptor_.open(endpoint.protocol(), ec);
      if (ec) {
         fail(ec, "open");
         return;
      }

      // Allow address reuse
      acceptor_.set_option(net::socket_base::reuse_address(true), ec);
      if (ec) {
         fail(ec, "set_option");
         return;
      }

      // Bind to the server address
      acceptor_.bind(endpoint, ec);
      if (ec) {
         fail(ec, "bind");
         return;
      }

      endpoint = acceptor_.local_endpoint(ec);
      if (ec) {
         fail(ec, "local_endpoint");
         return;
      }

      // Start listening for connections
      acceptor_.listen(net::socket_base::max_listen_connections, ec);
      if (ec) {
         fail(ec, "listen");
         return;
      }
   }

   // Start accepting incoming connections
   void run() { do_accept(); }

 private:
   void do_accept() {
      // The new connection gets its own strand
      acceptor_.async_accept(boost::asio::make_strand(server_->ship_ioc),
                             [self = shared_from_this()](beast::error_code ec, boost::asio::ip::tcp::socket&& socket) {
                                if( self->server_->stopping ) return;
                                if (ec) {
                                   fail(ec, "async_accept");
                                } else {
                                   self->on_accept( ec, std::move( socket ) );
                                }
                             });
   }

   void on_accept(beast::error_code ec, tcp::socket&& socket) {
      if (ec) {
         fail(ec, "accept");
      } else {
         // Create the session and run it
         auto s = std::make_shared<session_type>(server_, std::move(socket));
         s->start();
         server_->add_session(s);
      }
   }
};

struct test_server : mock_state_history_plugin {
   std::vector<std::thread> threads;
   tcp::endpoint            local_address{net::ip::make_address("127.0.0.1"), 0};

   void run() {

      main_ioc_work.emplace( boost::asio::make_work_guard( main_ioc ) );
      ship_ioc_work.emplace( boost::asio::make_work_guard( ship_ioc ) );

      threads.emplace_back([this]{ main_ioc.run(); });
      threads.emplace_back([this]{ ship_ioc.run(); });

      // Create and launch a listening port
      std::make_shared<listener>(this, local_address)->run();
   }

   ~test_server() {
      stopping = true;
      ship_ioc_work.reset();
      main_ioc_work.reset();

      for (auto& thr : threads) {
         thr.join();
      }
      threads.clear();
   }
};

std::vector<char> zlib_compress(const char* data, size_t size) {
   std::vector<char>      out;
   bio::filtering_ostream comp;
   comp.push(bio::zlib_compressor(bio::zlib::default_compression));
   comp.push(bio::back_inserter(out));
   bio::write(comp, data, size);
   bio::close(comp);
   return out;
}

std::vector<int32_t> generate_data(uint32_t sz) {
   std::vector<int32_t> data(sz);
   std::mt19937         rng;
   std::generate_n(data.begin(), data.size(), std::ref(rng));
   return data;
}

struct state_history_test_fixture {
   test_server                    server;
   net::io_context                ioc;
   websocket::stream<tcp::socket> ws;

   std::vector<std::vector<int32_t>> written_data;

   state_history_test_fixture()
       : ws(ioc) {

      // start the server with 2 threads
      server.run();
      connect_to(server.local_address);

      // receives the ABI
      beast::flat_buffer buffer;
      ws.read(buffer);
      std::string text((const char*)buffer.data().data(), buffer.data().size());
      BOOST_REQUIRE_EQUAL(text, state_history_plugin_abi);
      ws.binary(true);
   }

   void connect_to(tcp::endpoint addr) {
      ws.next_layer().connect(addr);
      // Update the host_ string. This will provide the value of the
      // Host HTTP header during the WebSocket handshake.
      // See https://tools.ietf.org/html/rfc7230#section-5.4
      std::string host = "http:://localhost:" + std::to_string(addr.port());

      // Set a decorator to change the User-Agent of the handshake
      ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
         req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
      }));

      // Perform the websocket handshake
      ws.handshake(host, "/");
   }

   void send_status_request() { send_request(eosio::state_history::get_status_request_v0{}); }

   void send_request(const eosio::state_history::state_request& request) {
      auto request_bin = fc::raw::pack(request);
      ws.write(net::buffer(request_bin));
   }

   void receive_result(eosio::state_history::state_result& result) {
      beast::flat_buffer buffer;
      ws.read(buffer);

      auto                        d = boost::asio::buffer_cast<char const*>(boost::beast::buffers_front(buffer.data()));
      auto                        s = boost::asio::buffer_size(buffer.data());
      fc::datastream<const char*> ds(d, s);
      fc::raw::unpack(ds, result);
   }

   void verify_statue(const eosio::state_history::get_status_result_v0& status) {

      send_status_request();
      eosio::state_history::state_result result;
      receive_result(result);

      BOOST_REQUIRE(std::holds_alternative<eosio::state_history::get_status_result_v0>(result));

      auto received_status = std::get<eosio::state_history::get_status_result_v0>(result);

      // we don't have `equal` declared in eosio::state_history::get_status_result_v0, just compare its serialized form
      BOOST_CHECK(fc::raw::pack(status) == fc::raw::pack(received_status));
   }

   void add_to_log(uint32_t index, uint32_t type, std::vector<int32_t>&& decompressed_data) {
      uint64_t decompressed_byte_count = decompressed_data.size() * sizeof(int32_t);

      auto compressed = zlib_compress((const char*)decompressed_data.data(), decompressed_byte_count);

      eosio::state_history_log_header header;
      header.block_id     = block_id_for(index);
      header.payload_size = compressed.size() + sizeof(type);
      if (type == 1) {
         header.payload_size += sizeof(uint64_t);
      }

      server.log->write_entry(header, block_id_for(index - 1), [&](auto& f) {
         f.write((const char*)&type, sizeof(type));
         if (type == 1) {
            f.write((const char*)&decompressed_byte_count, sizeof(decompressed_byte_count));
         }
         f.write(compressed.data(), compressed.size());
      });

      if (written_data.size() < index)
         written_data.resize(index);
      written_data[index - 1].swap(decompressed_data);
   }

   ~state_history_test_fixture() { ws.close(websocket::close_code::normal); }
};

void store_read_test_case(uint64_t data_size, eosio::state_history_log_config config) {
   fc::temp_directory       log_dir;
   eosio::state_history_log log("ship", log_dir.path(), config);


   eosio::state_history_log_header header;
   header.block_id     = block_id_for(1);
   header.payload_size = 0;
   auto data           = generate_data(data_size);

   log.pack_and_write_entry(header, block_id_for(0),
      [&](auto&& buf) { bio::write(buf, (const char*)data.data(), data.size() * sizeof(data[0])); });

   // make sure the current file position is at the end of file
   auto pos = log.get_log_file().tellp();
   log.get_log_file().seek_end(0);
   BOOST_REQUIRE_EQUAL(log.get_log_file().tellp(), pos);


   eosio::locked_decompress_stream buf = log.create_locked_decompress_stream();
   log.get_unpacked_entry(1, buf);

   std::vector<char> decompressed;
   auto& strm = std::get<std::unique_ptr<bio::filtering_istreambuf>>(buf.buf);
   BOOST_CHECK(!!strm);
   BOOST_CHECK(buf.lock.owns_lock());
   bio::copy(*strm, bio::back_inserter(decompressed));

   BOOST_CHECK_EQUAL(data.size() * sizeof(data[0]), decompressed.size());
   BOOST_CHECK(std::equal(decompressed.begin(), decompressed.end(), (const char*)data.data()));
}

BOOST_AUTO_TEST_CASE(store_read_entry_no_prune) {
   store_read_test_case(1024, {});
}

BOOST_AUTO_TEST_CASE(store_read_big_entry_no_prune) {
   // test the case where the uncompressed data size exceeds 4GB
   store_read_test_case( (1ULL<< 32) + (1ULL << 20), {});
}

BOOST_AUTO_TEST_CASE(store_read_entry_prune_enabled) {
   store_read_test_case(1024, eosio::state_history::prune_config{.prune_blocks = 100});
}

BOOST_FIXTURE_TEST_CASE(test_session_no_prune, state_history_test_fixture) {
   try {
      // setup block head for the server
      server.setup_state_history_log();
      uint32_t head_block_num = 3;
      server.block_head       = {head_block_num, block_id_for(head_block_num)};

      // generate the log data used for traces and deltas
      uint32_t n = mock_state_history_plugin::default_frame_size;
      add_to_log(1, n * sizeof(uint32_t), generate_data(n)); // original data format
      add_to_log(2, 0, generate_data(n)); // format to accommodate the compressed size greater than 4GB
      add_to_log(3, 1, generate_data(n)); // format to encode decompressed size to avoid decompress entire data upfront.

      // send a get_status_request and verify the result is what we expected
      verify_statue(eosio::state_history::get_status_result_v0{
          .head                    = {head_block_num, block_id_for(head_block_num)},
          .last_irreversible       = {head_block_num, block_id_for(head_block_num)},
          .trace_begin_block       = 1,
          .trace_end_block         = head_block_num + 1,
          .chain_state_begin_block = 1,
          .chain_state_end_block   = head_block_num + 1});

      // send a get_blocks_request to server
      send_request(eosio::state_history::get_blocks_request_v0{.start_block_num        = 1,
                                                               .end_block_num          = UINT32_MAX,
                                                               .max_messages_in_flight = UINT32_MAX,
                                                               .have_positions         = {},
                                                               .irreversible_only      = false,
                                                               .fetch_block            = true,
                                                               .fetch_traces           = true,
                                                               .fetch_deltas           = true});

      eosio::state_history::state_result result;
      // we should get 3 consecutive block result
      for (int i = 0; i < 3; ++i) {
         receive_result(result);
         BOOST_REQUIRE(std::holds_alternative<eosio::state_history::get_blocks_result_v0>(result));
         auto r = std::get<eosio::state_history::get_blocks_result_v0>(result);
         BOOST_REQUIRE_EQUAL(r.head.block_num, server.block_head.block_num);
         BOOST_REQUIRE(r.traces.has_value());
         BOOST_REQUIRE(r.deltas.has_value());
         auto  traces    = r.traces.value();
         auto  deltas    = r.deltas.value();
         auto& data      = written_data[i];
         auto  data_size = data.size() * sizeof(int32_t);
         BOOST_REQUIRE_EQUAL(traces.size(), data_size);
         BOOST_REQUIRE_EQUAL(deltas.size(), data_size);

         BOOST_REQUIRE(std::equal(traces.begin(), traces.end(), (const char*)data.data()));
         BOOST_REQUIRE(std::equal(deltas.begin(), deltas.end(), (const char*)data.data()));
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_FIXTURE_TEST_CASE(test_session_with_prune, state_history_test_fixture) {
   try {
      // setup block head for the server
      server.setup_state_history_log(
          eosio::state_history::prune_config{.prune_blocks = 2, .prune_threshold = 4 * 1024});

      uint32_t head_block_num = 3;
      server.block_head       = {head_block_num, block_id_for(head_block_num)};

      // generate the log data used for traces and deltas
      uint32_t n = mock_state_history_plugin::default_frame_size;
      add_to_log(1, n * sizeof(uint32_t), generate_data(n)); // original data format
      add_to_log(2, 0, generate_data(n)); // format to accommodate the compressed size greater than 4GB
      add_to_log(3, 1, generate_data(n)); // format to encode decompressed size to avoid decompress entire data upfront.

      // send a get_status_request and verify the result is what we expected
      verify_statue(eosio::state_history::get_status_result_v0{
          .head                    = {head_block_num, block_id_for(head_block_num)},
          .last_irreversible       = {head_block_num, block_id_for(head_block_num)},
          .trace_begin_block       = 2,
          .trace_end_block         = head_block_num + 1,
          .chain_state_begin_block = 2,
          .chain_state_end_block   = head_block_num + 1});

      // send a get_blocks_request to server
      send_request(eosio::state_history::get_blocks_request_v0{.start_block_num        = 1,
                                                               .end_block_num          = UINT32_MAX,
                                                               .max_messages_in_flight = UINT32_MAX,
                                                               .have_positions         = {},
                                                               .irreversible_only      = false,
                                                               .fetch_block            = true,
                                                               .fetch_traces           = true,
                                                               .fetch_deltas           = true});

      eosio::state_history::state_result result;
      // we should get 3 consecutive block result

      receive_result(result);
      BOOST_REQUIRE(std::holds_alternative<eosio::state_history::get_blocks_result_v0>(result));
      auto r = std::get<eosio::state_history::get_blocks_result_v0>(result);
      BOOST_REQUIRE_EQUAL(r.head.block_num, server.block_head.block_num);
      BOOST_REQUIRE(!r.traces.has_value());
      BOOST_REQUIRE(!r.deltas.has_value());

      for (int i = 1; i < 3; ++i) {
         receive_result(result);
         BOOST_REQUIRE(std::holds_alternative<eosio::state_history::get_blocks_result_v0>(result));
         auto r = std::get<eosio::state_history::get_blocks_result_v0>(result);
         BOOST_REQUIRE_EQUAL(r.head.block_num, server.block_head.block_num);
         BOOST_REQUIRE(r.traces.has_value());
         BOOST_REQUIRE(r.deltas.has_value());
         auto  traces    = r.traces.value();
         auto  deltas    = r.deltas.value();
         auto& data      = written_data[i];
         auto  data_size = data.size() * sizeof(int32_t);
         BOOST_REQUIRE_EQUAL(traces.size(), data_size);
         BOOST_REQUIRE_EQUAL(deltas.size(), data_size);

         BOOST_REQUIRE(std::equal(traces.begin(), traces.end(), (const char*)data.data()));
         BOOST_REQUIRE(std::equal(deltas.begin(), deltas.end(), (const char*)data.data()));
      }
   }
   FC_LOG_AND_RETHROW()
}
