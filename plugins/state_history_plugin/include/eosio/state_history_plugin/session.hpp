#pragma once
#include <eosio/chain/block_state.hpp>
#include <eosio/state_history/compression.hpp>
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/serialization.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/websocket.hpp>


extern const char* const state_history_plugin_abi;

namespace eosio {
using namespace state_history;
using tcp     = boost::asio::ip::tcp;
using unixs   = boost::asio::local::stream_protocol;
namespace ws  = boost::beast::websocket;
namespace bio = boost::iostreams;

template <typename Session>
struct send_queue_entry_base {
   virtual ~send_queue_entry_base() {}
   virtual void send(Session* s) = 0;
};

template <typename Session>
struct basic_send_queue_entry : send_queue_entry_base<Session> {
   std::vector<char> data;
   template <typename... Args>
   basic_send_queue_entry(Args&&... args)
       : data(std::forward<Args>(args)...) {}
   void send(Session* s) override {
      s->socket_stream.async_write(boost::asio::buffer(data),
                                   [s = s->shared_from_this()](boost::system::error_code ec, size_t) {
                                      s->callback(ec, "async_write", [s] { s->pop_entry(); });
                                   });
   }
};

template <typename Session>
class blocks_result_send_queue_entry : public send_queue_entry_base<Session> {
   eosio::state_history::get_blocks_result_v0                      r;
   std::vector<char>                                               data;
   std::variant<std::vector<char>, maybe_locked_decompress_stream> buf;

   template <typename Next>
   void async_send(Session* s, bool fin, const std::vector<char>& d, Next&& next) {
      s->socket_stream.async_write_some(
          fin, boost::asio::buffer(d),
          [s = s->shared_from_this(), next = std::forward<Next>(next)](boost::system::error_code ec, size_t) mutable {
             s->callback(ec, "async_write", [s, next = std::move(next)]() mutable { next(s.get()); });
          });
   }

   template <typename Next>
   void async_send(Session* s, bool fin, maybe_locked_decompress_stream& locked_strm, Next&& next) {
      data.resize(s->default_frame_size);
      auto size = bio::read(locked_strm.buf, data.data(), s->default_frame_size);
      data.resize(size);
      bool eof = (locked_strm.buf.sgetc() == EOF);

      s->socket_stream.async_write_some(
          fin && eof, boost::asio::buffer(data),
          [this, s = s->shared_from_this(), fin, &locked_strm, eof,
           next = std::forward<Next>(next)](boost::system::error_code ec, size_t) mutable {
             if (ec) locked_strm.lock.reset();
             s->callback(ec, "async_write", [this, s, fin, &locked_strm, eof, next = std::move(next)]() mutable {
                if (eof) {
                   locked_strm.lock.reset();
                   next(s.get());
                } else {
                   async_send(s.get(), fin, locked_strm, std::move(next));
                }
             });
          });
   }

   template <typename Next>
   void async_send_buf(Session* s, bool fin, Next&& next) {
      std::visit([this, s, fin,
                  next = std::forward<Next>(next)](auto& d) mutable { this->async_send(s, fin, d, std::move(next)); },
                 buf);
   }

   template <typename Next>
   void send_log(Session* s, uint64_t entry_size, bool is_deltas, Next&& next) {
      if (entry_size) {
         data.resize(16); // should be at least for 1 byte (optional) + 10 bytes (variable sized uint64_t)
         fc::datastream<char*> ds(data.data(), data.size());
         fc::raw::pack(ds, true); // optional true
         history_pack_varuint64(ds, entry_size);
         data.resize(ds.tellp());
      } else {
         data = {'\0'}; // optional false
      }

      async_send(s, is_deltas && entry_size == 0, data,
                [this, is_deltas, entry_size, next = std::forward<Next>(next)](Session* s) mutable {
                   if (entry_size) {
                      async_send_buf(s, is_deltas, [next = std::move(next)](Session* s) { next(s); });
                   } else
                      next(s);
                });
   }

   void send_traces(Session* s) {
      send_log(s, s->get_trace_log_entry(r, buf), false, [this](Session* s) { this->send_deltas(s); });
   }

   void send_deltas(Session* s) {
      send_log(s, s->get_delta_log_entry(r, buf), true, [](Session* s) { s->pop_entry(); });
   }

public:

   explicit blocks_result_send_queue_entry(get_blocks_result_v0&& r)
       : r(std::move(r)) {}

   void send(Session* s) override {
      // pack the state_result{get_blocks_result} excluding the fields `traces` and `deltas`
      fc::datastream<size_t> ss;
      fc::raw::pack(ss, fc::unsigned_int(1)); // pack the variant index of state_result{r}
      fc::raw::pack(ss, static_cast<const get_blocks_result_base&>(r));
      data.resize(ss.tellp());
      fc::datastream<char*> ds(data.data(), data.size());
      fc::raw::pack(ds, fc::unsigned_int(1)); // pack the variant index of state_result{r}
      fc::raw::pack(ds, static_cast<const get_blocks_result_base&>(r));

      async_send(s, false, data, [this](Session* s) { send_traces(s); });
   }
};

struct session_base {
   virtual void send_update(const eosio::chain::block_state_ptr& block_state) = 0;
   virtual void close()                                                       = 0;
   virtual ~session_base()                                                    = default;
   std::optional<get_blocks_request_v0> current_request;
};

template <typename Plugin, typename SocketType>
struct session : session_base, std::enable_shared_from_this<session<Plugin, SocketType>> {

   using entry_ptr = std::unique_ptr<send_queue_entry_base<session>>;

   Plugin                 plugin;
   ws::stream<SocketType> socket_stream;
   bool                   sending  = false;
   bool                   sent_abi = false;
   std::vector<entry_ptr> send_queue;
   bool                   need_to_send_update = false;
   const int32_t          default_frame_size;

   session(Plugin plugin, SocketType socket)
       : plugin(std::move(plugin))
       , socket_stream(std::move(socket))
       , default_frame_size(plugin->default_frame_size) {}

   void start() {
      fc_ilog(plugin->logger(), "incoming connection");
      socket_stream.auto_fragment(false);
      socket_stream.binary(true);
      if constexpr (std::is_same_v<SocketType, tcp::socket>) {
         socket_stream.next_layer().set_option(boost::asio::ip::tcp::no_delay(true));
      }
      socket_stream.next_layer().set_option(boost::asio::socket_base::send_buffer_size(1024 * 1024));
      socket_stream.next_layer().set_option(boost::asio::socket_base::receive_buffer_size(1024 * 1024));

      socket_stream.async_accept([self = this->shared_from_this()](boost::system::error_code ec) {
         self->callback(ec, "async_accept", [self] {
            self->start_read();
            self->send_abi();
         });
      });
   }

   void start_read() {
      auto in_buffer = std::make_shared<boost::beast::flat_buffer>();
      socket_stream.async_read(
          *in_buffer, [self = this->shared_from_this(), in_buffer](boost::system::error_code ec, size_t) {
             self->callback(ec, "async_read", [self, in_buffer] {
                auto d = boost::asio::buffer_cast<char const*>(boost::beast::buffers_front(in_buffer->data()));
                auto s = boost::asio::buffer_size(in_buffer->data());
                fc::datastream<const char*> ds(d, s);
                state_request               req;
                fc::raw::unpack(ds, req);
                self->plugin->post_task([self, req = std::move(req)]() mutable { std::visit(*self, req); });
                self->start_read();
             });
          });
   }

   void send() {
      if (sending)
         return;
      if (send_queue.empty())
         return send_update();
      sending = true;
      socket_stream.binary(sent_abi);
      sent_abi = true;
      send_queue[0]->send(this);
   }

   void send_abi() {
      boost::asio::post(this->plugin->work_strand, [self = this->shared_from_this()]() {
         std::string_view str(state_history_plugin_abi);
         self->send_queue.push_back(std::make_unique<basic_send_queue_entry<session>>(str.begin(), str.end()));
         self->send();
      });
   }

   template <typename T>
   void send(T obj) {
      boost::asio::post(this->plugin->work_strand, [self = this->shared_from_this(), obj = std::move(obj)]() mutable {
         self->send_queue.push_back(
             std::make_unique<basic_send_queue_entry<session>>(fc::raw::pack(state_result{std::move(obj)})));
         self->send();
      });
   }

   void send(get_blocks_result_v0&& obj) {
      boost::asio::post(this->plugin->work_strand, [self = this->shared_from_this(), obj = std::move(obj)]() mutable {
         self->send_queue.push_back(std::make_unique<blocks_result_send_queue_entry<session>>(std::move(obj)));
         self->send();
      });
   }

   void pop_entry() {
      send_queue.erase(send_queue.begin());
      sending = false;
      send();
   }

   uint64_t get_trace_log_entry(const eosio::state_history::get_blocks_result_v0&              result,
                                std::variant<std::vector<char>, maybe_locked_decompress_stream>& buf) {
      if (result.traces.has_value())
         return plugin->get_trace_log()->get_unpacked_entry(result.this_block->block_num, buf);
      return 0;
   }

   uint64_t get_delta_log_entry(const eosio::state_history::get_blocks_result_v0&              result,
                                std::variant<std::vector<char>, maybe_locked_decompress_stream>& buf) {
      if (result.deltas.has_value())
         return plugin->get_chain_state_log()->get_unpacked_entry(result.this_block->block_num, buf);
      return 0;
   }

   using result_type = void;
   void operator()(get_status_request_v0&) {
      fc_ilog(plugin->logger(), "got get_status_request_v0");

      get_status_result_v0 result;
      result.head              = plugin->get_block_head();
      result.last_irreversible = plugin->get_last_irreversible();
      result.chain_id          = plugin->get_chain_id();
      auto&& trace_log         = plugin->get_trace_log();
      if (trace_log) {
         result.trace_begin_block = trace_log->begin_block();
         result.trace_end_block   = trace_log->end_block();
      }
      auto&& chain_state_log = plugin->get_chain_state_log();
      if (chain_state_log) {
         result.chain_state_begin_block = chain_state_log->begin_block();
         result.chain_state_end_block   = chain_state_log->end_block();
      }
      fc_ilog(plugin->logger(), "pushing get_status_result_v0 to send queue");
      send(std::move(result));
   }

   void operator()(get_blocks_request_v0& req) {
      fc_ilog(plugin->logger(), "received get_blocks_request_v0 = ${req}", ("req", req));
      for (auto& cp : req.have_positions) {
         if (req.start_block_num <= cp.block_num)
            continue;
         auto id = plugin->get_block_id(cp.block_num);
         if (!id || *id != cp.block_id)
            req.start_block_num = std::min(req.start_block_num, cp.block_num);

         if (!id) {
            fc_dlog(plugin->logger(), "block ${block_num} is not available", ("block_num", cp.block_num));
         } else if (*id != cp.block_id) {
            fc_dlog(plugin->logger(),
                    "the id for block ${block_num} in block request have_positions does not match the existing",
                    ("block_num", cp.block_num));
         }
      }
      req.have_positions.clear();
      fc_dlog(plugin->logger(), "  get_blocks_request_v0 start_block_num set to ${num}", ("num", req.start_block_num));
      current_request = req;
      send_update(true);
   }

   void operator()(get_blocks_ack_request_v0& req) {
      fc_ilog(plugin->logger(), "received get_blocks_ack_request_v0 = ${req}", ("req", req));
      if (!current_request) {
         fc_dlog(plugin->logger(), " no current get_blocks_request_v0, discarding the get_blocks_ack_request_v0");
         return;
      }
      current_request->max_messages_in_flight += req.num_messages;
      send_update();
   }

   void send_update(get_blocks_result_v0 result, const chain::block_state_ptr& block_state) {
      need_to_send_update = true;
      if (!send_queue.empty() || !current_request || !current_request->max_messages_in_flight)
         return;

      result.last_irreversible = plugin->get_last_irreversible();
      uint32_t current =
          current_request->irreversible_only ? result.last_irreversible.block_num : result.head.block_num;

      if (current_request->start_block_num <= current &&
          current_request->start_block_num < current_request->end_block_num) {
         auto block_id = plugin->get_block_id(current_request->start_block_num);

         if (block_id) {
            result.this_block  = block_position{current_request->start_block_num, *block_id};
            auto prev_block_id = plugin->get_block_id(current_request->start_block_num - 1);
            if (prev_block_id)
               result.prev_block = block_position{current_request->start_block_num - 1, *prev_block_id};
            if (current_request->fetch_block)
               plugin->get_block(current_request->start_block_num, block_state, result.block);
            if (current_request->fetch_traces && plugin->get_trace_log())
               result.traces.emplace();
            if (current_request->fetch_deltas && plugin->get_chain_state_log())
               result.deltas.emplace();
         }
         ++current_request->start_block_num;
      }

      auto& block_num = current_request->start_block_num;
      auto  timestamp = plugin->get_block_timestamp(block_num, block_state);

      // during syncing if block is older than 5 min, log every 1000th block
      bool fresh_block = timestamp && fc::time_point::now() - *timestamp < fc::minutes(5);
      if (fresh_block || (result.this_block && result.this_block->block_num % 1000 == 0)) {
         fc_ilog(plugin->logger(),
                 "pushing result "
                 "{\"head\":{\"block_num\":${head}},\"last_irreversible\":{\"block_num\":${last_irr}},\"this_block\":{"
                 "\"block_num\":${this_block}}} to send queue",
                 ("head", result.head.block_num)("last_irr", result.last_irreversible.block_num)(
                     "this_block", result.this_block ? result.this_block->block_num : fc::variant()));
      }

      send(std::move(result));
      --current_request->max_messages_in_flight;
      need_to_send_update = current_request->start_block_num <= current &&
                            current_request->start_block_num < current_request->end_block_num;
   }

   void send_update(const chain::block_state_ptr& block_state) {
      need_to_send_update = true;
      if (!send_queue.empty() || !current_request || !current_request->max_messages_in_flight)
         return;
      get_blocks_result_v0 result;
      result.head = {block_state->block_num, block_state->id};
      send_update(std::move(result), block_state);
   }

   void send_update(bool changed = false) {
      if (changed)
         need_to_send_update = true;
      if (!send_queue.empty() || !need_to_send_update || !current_request || !current_request->max_messages_in_flight)
         return;
      get_blocks_result_v0 result;
      result.head = plugin->get_block_head();
      send_update(std::move(result), {});
   }

   template <typename F>
   void catch_and_close(F f) {
      try {
         f();
      } catch (const fc::exception& e) {
         fc_elog(plugin->logger(), "${e}", ("e", e.to_detail_string()));
         close();
      } catch (const std::exception& e) {
         fc_elog(plugin->logger(), "${e}", ("e", e.what()));
         close();
      } catch (...) {
         fc_elog(plugin->logger(), "unknown exception");
         close();
      }
   }

   template <typename F>
   void callback(boost::system::error_code ec, const char* what, F f) {
      plugin->post_task([=]() {
         if (plugin->stopping)
            return;
         if (ec) {
            return on_fail(ec, what);
         }
         catch_and_close(f);
      });
   }

   void on_fail(boost::system::error_code ec, const char* what) {
      try {
         if (ec == boost::asio::error::eof || ec == boost::beast::websocket::error::closed) {
            fc_dlog(plugin->logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         } else {
            fc_elog(plugin->logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         }
         close();
      } catch (...) {
         fc_elog(plugin->logger(), "uncaught exception on close");
      }
   }

   void close() {
      boost::system::error_code ec;
      socket_stream.next_layer().close(ec);
      if (ec) {
         fc_elog(plugin->logger(), "close: ${m}", ("m", ec.message()));
      }
      plugin->remove_session(this->shared_from_this());
   }
};
} // namespace eosio
