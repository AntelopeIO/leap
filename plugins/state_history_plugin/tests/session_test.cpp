#include <boost/test/unit_test.hpp>

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
#include <fc/network/listener.hpp>

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

std::unordered_map<uint32_t, eosio::chain::block_id_type> block_ids;
fc::sha256 block_id_for(const uint32_t bnum, const std::string& nonce = {}) {
   if (auto it = block_ids.find(bnum); it != block_ids.end())
      return it->second;
   fc::sha256 m = fc::sha256::hash(fc::sha256::hash(std::to_string(bnum)+nonce));
   m._hash[0]   = fc::endian_reverse_u32(bnum);
   block_ids[bnum] = m;
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
   std::optional<eosio::state_history_log> trace_log;
   std::optional<eosio::state_history_log> state_log;
   std::atomic<bool>                       stopping = false;
   eosio::session_manager                  session_mgr{ship_ioc};

   constexpr static uint32_t default_frame_size = 1024;

   std::optional<eosio::state_history_log>& get_trace_log() { return trace_log; }
   std::optional<eosio::state_history_log>& get_chain_state_log() { return state_log; }
   fc::sha256                get_chain_id() const { return {}; }

   boost::asio::io_context& get_ship_executor() { return ship_ioc; }

   void setup_state_history_log(eosio::state_history_log_config conf = {}) {
      trace_log.emplace("ship_trace", log_dir.path(), conf);
      state_log.emplace("ship_state", log_dir.path(), conf);
   }

   fc::logger logger = fc::logger::get(DEFAULT_LOGGER);

   fc::logger& get_logger() { return logger; }

   void get_block(uint32_t block_num, const eosio::chain::block_state_ptr& block_state,
                  std::optional<eosio::chain::bytes>& result) const {
      result.emplace().resize(16);
   }

   fc::time_point get_head_block_timestamp() const {
      return fc::time_point{};
   }

   std::optional<eosio::chain::block_id_type> get_block_id(uint32_t block_num) {
      std::optional<eosio::chain::block_id_type> id;
      if( trace_log ) {
         id = trace_log->get_block_id( block_num );
         if( id )
            return id;
      }
      if( state_log ) {
         id = state_log->get_block_id( block_num );
         if( id )
            return id;
      }
      return block_id_for(block_num);
   }

   eosio::state_history::block_position get_block_head() { return block_head; }
   eosio::state_history::block_position get_last_irreversible() { return block_head; }

   uint32_t get_first_available_block_num() const { return 0; }

   void add_session(std::shared_ptr<eosio::session_base> s) {
      session_mgr.insert(std::move(s));
   }

};

using session_type = eosio::session<mock_state_history_plugin, tcp::socket>;

struct test_server : mock_state_history_plugin {
   std::vector<std::thread> threads;
   tcp::endpoint            local_address{net::ip::make_address("127.0.0.1"), 0};

   void run() {

      main_ioc_work.emplace( boost::asio::make_work_guard( main_ioc ) );
      ship_ioc_work.emplace( boost::asio::make_work_guard( ship_ioc ) );

      threads.emplace_back([this]{ main_ioc.run(); });
      threads.emplace_back([this]{ ship_ioc.run(); });

      auto create_session = [this](tcp::socket&& peer_socket) {
         auto s = std::make_shared<session_type>(*this, std::move(peer_socket), session_mgr);
         s->start();
         add_session(s);
      };

      // Create and launch a listening port
      auto server = std::make_shared<fc::listener<tcp, decltype(create_session)>>(
                        ship_ioc, logger, boost::posix_time::milliseconds(100), "", local_address, "", create_session);
      server->do_accept();
      local_address = server->acceptor().local_endpoint();
   }

   ~test_server() {
      stopping = true;
      ship_ioc_work.reset();
      main_ioc_work.reset();
      ship_ioc.stop();

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

      // mock uses DEFAULT_LOGGER
      fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);

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

   void verify_status(const eosio::state_history::get_status_result_v0& status) {

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

      std::unique_lock gt(server.trace_log->_mx);
      server.trace_log->write_entry(header, block_id_for(index - 1), [&](auto& f) {
         f.write((const char*)&type, sizeof(type));
         if (type == 1) {
            f.write((const char*)&decompressed_byte_count, sizeof(decompressed_byte_count));
         }
         f.write(compressed.data(), compressed.size());
      });
      gt.unlock();
      std::unique_lock gs(server.state_log->_mx);
      server.state_log->write_entry(header, block_id_for(index - 1), [&](auto& f) {
         f.write((const char*)&type, sizeof(type));
         if (type == 1) {
            f.write((const char*)&decompressed_byte_count, sizeof(decompressed_byte_count));
         }
         f.write(compressed.data(), compressed.size());
      });
      gs.unlock();

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

BOOST_AUTO_TEST_CASE(store_with_existing) {
   uint64_t data_size = 512;
   fc::temp_directory       log_dir;
   eosio::state_history_log log("ship", log_dir.path(), {});

   eosio::state_history_log_header header;
   header.block_id     = block_id_for(1);
   header.payload_size = 0;
   auto data           = generate_data(data_size);

   log.pack_and_write_entry(header, block_id_for(0),
                            [&](auto&& buf) { bio::write(buf, (const char*)data.data(), data.size() * sizeof(data[0])); });

   header.block_id     = block_id_for(2);
   log.pack_and_write_entry(header, block_id_for(1),
                            [&](auto&& buf) { bio::write(buf, (const char*)data.data(), data.size() * sizeof(data[0])); });

   // Do not allow starting from scratch for existing
   header.block_id     = block_id_for(1);
   BOOST_CHECK_EXCEPTION(
         log.pack_and_write_entry(header, block_id_for(0), [&](auto&& buf) { bio::write(buf, (const char*)data.data(), data.size() * sizeof(data[0])); }),
         eosio::chain::plugin_exception,
         []( const auto& e ) {
            return e.to_detail_string().find( "Existing ship log" ) != std::string::npos;
         }
   );
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
      verify_status(eosio::state_history::get_status_result_v0{
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

BOOST_FIXTURE_TEST_CASE(test_split_log, state_history_test_fixture) {
   try {
      // setup block head for the server
      constexpr uint32_t head = 1023;
      eosio::state_history::partition_config conf;
      conf.stride = 25;
      server.setup_state_history_log(conf);
      uint32_t head_block_num = head;
      server.block_head       = {head_block_num, block_id_for(head_block_num)};

      // generate the log data used for traces and deltas
      uint32_t n = mock_state_history_plugin::default_frame_size;
      add_to_log(1, n * sizeof(uint32_t), generate_data(n)); // original data format
      add_to_log(2, 0, generate_data(n)); // format to accommodate the compressed size greater than 4GB
      add_to_log(3, 1, generate_data(n)); // format to encode decompressed size to avoid decompress entire data upfront.
      for (size_t i = 4; i <= head; ++i) {
         add_to_log(i, 1, generate_data(n));
      }

      send_request(eosio::state_history::get_blocks_request_v0{.start_block_num        = 1,
                                                               .end_block_num          = UINT32_MAX,
                                                               .max_messages_in_flight = UINT32_MAX,
                                                               .have_positions         = {},
                                                               .irreversible_only      = false,
                                                               .fetch_block            = true,
                                                               .fetch_traces           = true,
                                                               .fetch_deltas           = true});

      eosio::state_history::state_result result;
      // we should get 1023 consecutive block result
      eosio::chain::block_id_type prev_id;
      for (uint32_t i = 0; i < head; ++i) {
         receive_result(result);
         BOOST_REQUIRE(std::holds_alternative<eosio::state_history::get_blocks_result_v0>(result));
         auto r = std::get<eosio::state_history::get_blocks_result_v0>(result);
         BOOST_REQUIRE_EQUAL(r.head.block_num, server.block_head.block_num);
         if (i > 0) {
            BOOST_TEST(prev_id.str() == r.prev_block->block_id.str());
         }
         prev_id = r.this_block->block_id;
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
      verify_status(eosio::state_history::get_status_result_v0{
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

BOOST_FIXTURE_TEST_CASE(test_session_fork, state_history_test_fixture) {
   try {
      // setup block head for the server
      server.setup_state_history_log();
      uint32_t head_block_num = 4;
      server.block_head       = {head_block_num, block_id_for(head_block_num)};

      // generate the log data used for traces and deltas
      uint32_t n = mock_state_history_plugin::default_frame_size;
      add_to_log(1, n * sizeof(uint32_t), generate_data(n)); // original data format
      add_to_log(2, 0, generate_data(n)); // format to accommodate the compressed size greater than 4GB
      add_to_log(3, 1, generate_data(n)); // format to encode decompressed size to avoid decompress entire data upfront.
      add_to_log(4, 1, generate_data(n)); // format to encode decompressed size to avoid decompress entire data upfront.

      // send a get_status_request and verify the result is what we expected
      verify_status(eosio::state_history::get_status_result_v0{
            .head                    = {head_block_num, block_id_for(head_block_num)},
            .last_irreversible       = {head_block_num, block_id_for(head_block_num)},
            .trace_begin_block       = 1,
            .trace_end_block         = head_block_num + 1,
            .chain_state_begin_block = 1,
            .chain_state_end_block   = head_block_num + 1});

      // send a get_blocks_request to server
      send_request(eosio::state_history::get_blocks_request_v0{
            .start_block_num        = 1,
            .end_block_num          = UINT32_MAX,
            .max_messages_in_flight = UINT32_MAX,
            .have_positions         = {},
            .irreversible_only      = false,
            .fetch_block            = true,
            .fetch_traces           = true,
            .fetch_deltas           = true});

      std::vector<eosio::state_history::block_position> have_positions;
      eosio::state_history::state_result result;
      // we should get 4 consecutive block result
      for (uint32_t i = 0; i < 4; ++i) {
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
         BOOST_REQUIRE(r.this_block.has_value());
         BOOST_REQUIRE_EQUAL(r.this_block->block_num, i+1);
         have_positions.push_back(*r.this_block);

         BOOST_REQUIRE(std::equal(traces.begin(), traces.end(), (const char*)data.data()));
         BOOST_REQUIRE(std::equal(deltas.begin(), deltas.end(), (const char*)data.data()));
      }

      // generate a fork that includes blocks 3,4 and verify new data retrieved
      block_ids.extract(3); block_id_for(3, "fork");
      block_ids.extract(4); block_id_for(4, "fork");
      server.block_head = {head_block_num, block_id_for(head_block_num)};
      add_to_log(3, 0, generate_data(n));
      add_to_log(4, 1, generate_data(n));

      // send a get_status_request and verify the result is what we expected
      verify_status(eosio::state_history::get_status_result_v0{
            .head                    = {head_block_num, block_id_for(head_block_num)},
            .last_irreversible       = {head_block_num, block_id_for(head_block_num)},
            .trace_begin_block       = 1,
            .trace_end_block         = head_block_num + 1,
            .chain_state_begin_block = 1,
            .chain_state_end_block   = head_block_num + 1});

      // send a get_blocks_request to server starting at 5, will send 3,4 because of fork
      send_request(eosio::state_history::get_blocks_request_v0{
            .start_block_num        = 5,
            .end_block_num          = UINT32_MAX,
            .max_messages_in_flight = UINT32_MAX,
            .have_positions         = std::move(have_positions),
            .irreversible_only      = false,
            .fetch_block            = true,
            .fetch_traces           = true,
            .fetch_deltas           = true});

      eosio::state_history::state_result fork_result;
      // we should now get data for fork 3,4
      for (uint32_t i = 2; i < 4; ++i) {
         receive_result(fork_result);
         BOOST_REQUIRE(std::holds_alternative<eosio::state_history::get_blocks_result_v0>(fork_result));
         auto r = std::get<eosio::state_history::get_blocks_result_v0>(fork_result);
         BOOST_REQUIRE_EQUAL(r.head.block_num, server.block_head.block_num);
         BOOST_REQUIRE(r.this_block.has_value());
         BOOST_REQUIRE_EQUAL(r.this_block->block_num, i+1);
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
