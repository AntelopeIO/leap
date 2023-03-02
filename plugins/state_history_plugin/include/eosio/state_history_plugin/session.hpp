#pragma once
#include <eosio/chain/block_state.hpp>
#include <eosio/state_history/compression.hpp>
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/serialization.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/buffer.hpp>
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
   virtual ~send_queue_entry_base() = default;
   virtual void send(std::shared_ptr<Session> s) = 0;
};

template <typename Session>
struct basic_send_queue_entry : send_queue_entry_base<Session> {
   std::vector<char> data;
   template <typename... Args>
   explicit basic_send_queue_entry(Args&&... args)
       : data(std::forward<Args>(args)...) {}
   void send(std::shared_ptr<Session> s) override {
      s->socket_stream->async_write(boost::asio::buffer(data),
                                   [s{std::move(s)}](boost::system::error_code ec, size_t) {
                                      s->callback(ec, "async_write", [s] { s->pop_entry(); });
                                   });
   }
};

template <typename Session>
class blocks_result_send_queue_entry : public send_queue_entry_base<Session> {
   std::shared_ptr<Session>                                        session;
   eosio::state_history::get_blocks_result_v0                      r;
   std::vector<char>                                               data;
   std::optional<locked_decompress_stream>                         stream;

   template <typename Next>
   void async_send(bool fin, const std::vector<char>& d, Next&& next) {
      session->socket_stream->async_write_some(
          fin, boost::asio::buffer(d),
          [this, s=session, next = std::forward<Next>(next)](boost::system::error_code ec, size_t) mutable {
             if( ec ) {
                stream.reset();
                s->sending_block_session.reset();
             }
             s->callback(ec, "async_write", [next = std::move(next)]() mutable { next(); });
          });
   }

   template <typename Next>
   void async_send(bool fin, std::unique_ptr<bio::filtering_istreambuf>& strm, Next&& next) {
      data.resize(session->default_frame_size);
      auto size = bio::read(*strm, data.data(), session->default_frame_size);
      data.resize(size);
      bool eof = (strm->sgetc() == EOF);

      session->socket_stream->async_write_some( fin && eof, boost::asio::buffer(data),
          [this, s=session, fin, eof, next = std::forward<Next>(next)](boost::system::error_code ec, size_t) mutable {
             if( ec ) {
                stream.reset();
                s->sending_block_session.reset();
             }
             s->callback(ec, "async_write", [this, fin, eof, next = std::move(next)]() mutable {
                if (eof) {
                   next();
                } else {
                   async_send_buf(fin, std::move(next));
                }
             });
          });
   }

   template <typename Next>
   void async_send_buf(bool fin, Next&& next) {
      std::visit([this, fin,
                  next = std::forward<Next>(next)](auto& d) mutable { this->async_send(fin, d, std::move(next)); },
                 stream->buf);
   }

   template <typename Next>
   void send_log(uint64_t entry_size, bool is_deltas, Next&& next) {
      if (entry_size) {
         data.resize(16); // should be at least for 1 byte (optional) + 10 bytes (variable sized uint64_t)
         fc::datastream<char*> ds(data.data(), data.size());
         fc::raw::pack(ds, true); // optional true
         history_pack_varuint64(ds, entry_size);
         data.resize(ds.tellp());
      } else {
         data = {'\0'}; // optional false
      }

      async_send(is_deltas && entry_size == 0, data,
                [this, is_deltas, entry_size, next = std::forward<Next>(next)]() mutable {
                   if (entry_size) {
                      async_send_buf(is_deltas, [next = std::move(next)]() { next(); });
                   } else
                      next();
                });
   }

   void send_deltas() {
      stream.reset();
      send_log(session->get_delta_log_entry(r, stream), true, [s=session]() {
         s->sending_block_session.reset();
         s->pop_entry();
      });
   }

   void send_traces() {
      stream.reset();
      send_log(session->get_trace_log_entry(r, stream), false, [this]() { this->send_deltas(); });
   }

public:

   explicit blocks_result_send_queue_entry(get_blocks_result_v0&& r)
       : r(std::move(r)) {}

   void send(std::shared_ptr<Session> s) override {
      s->sending_block_session = s;
      session = std::move(s);
      // pack the state_result{get_blocks_result} excluding the fields `traces` and `deltas`
      fc::datastream<size_t> ss;
      fc::raw::pack(ss, fc::unsigned_int(1)); // pack the variant index of state_result{r}
      fc::raw::pack(ss, static_cast<const get_blocks_result_base&>(r));
      data.resize(ss.tellp());
      fc::datastream<char*> ds(data.data(), data.size());
      fc::raw::pack(ds, fc::unsigned_int(1)); // pack the variant index of state_result{r}
      fc::raw::pack(ds, static_cast<const get_blocks_result_base&>(r));

      async_send(false, data, [this]() { send_traces(); });
   }
};

struct session_base {
   virtual void send()                                                        = 0;
   virtual void send_update(const eosio::chain::block_state_ptr& block_state) = 0;
   virtual ~session_base()                                                    = default;
   std::optional<get_blocks_request_v0> current_request;

   // static across all sessions. blocks_result_send_queue_entry locks state_history_log mutex
   // and holds until done streaming the block. Only one of these can be in progress at a time.
   inline static std::shared_ptr<session_base> sending_block_session{}; // ship thread only
   // other session waiting on this session to complete block sending before they can continue
   std::set<std::weak_ptr<session_base>, std::owner_less<>> others_waiting; // ship thread only
};

template <typename Plugin, typename SocketType>
struct session : session_base, std::enable_shared_from_this<session<Plugin, SocketType>> {

   using entry_ptr = std::unique_ptr<send_queue_entry_base<session>>;

   Plugin                 plugin;
   std::optional<ws::stream<SocketType>> socket_stream; // ship thread only after creation
   bool                   sending  = false; // ship thread only
   std::vector<entry_ptr> send_queue; // ship thread only

   bool                   need_to_send_update = false; // main thread only
   uint32_t               to_send_block_num = 0; // main thread only
   std::optional<std::vector<block_position>::const_iterator> position_it; // main thread only

   const int32_t          default_frame_size;

   session(Plugin plugin, SocketType socket)
       : plugin(std::move(plugin))
       , socket_stream(std::move(socket))
       , default_frame_size(plugin->default_frame_size) {}

   void start() {
      fc_ilog(plugin->logger(), "incoming connection");
      socket_stream->auto_fragment(false);
      socket_stream->binary(true);
      if constexpr (std::is_same_v<SocketType, tcp::socket>) {
         socket_stream->next_layer().set_option(boost::asio::ip::tcp::no_delay(true));
      }
      socket_stream->next_layer().set_option(boost::asio::socket_base::send_buffer_size(1024 * 1024));
      socket_stream->next_layer().set_option(boost::asio::socket_base::receive_buffer_size(1024 * 1024));

      socket_stream->async_accept([self = this->shared_from_this()](boost::system::error_code ec) {
         self->callback(ec, "async_accept", [self] {
            self->socket_stream->binary(false);
            self->socket_stream->async_write(
                  boost::asio::buffer(state_history_plugin_abi, strlen(state_history_plugin_abi)),
                  [self](boost::system::error_code ec, size_t) {
                     self->callback(ec, "async_write", [self] {
                        self->socket_stream->binary(true);
                        self->start_read();
                     });
                  });
         });
      });
   }

   void start_read() {
      auto in_buffer = std::make_shared<boost::beast::flat_buffer>();
      socket_stream->async_read(
          *in_buffer, [self = this->shared_from_this(), in_buffer](boost::system::error_code ec, size_t) {
             self->callback(ec, "async_read", [self, in_buffer] {
                auto d = boost::asio::buffer_cast<char const*>(boost::beast::buffers_front(in_buffer->data()));
                auto s = boost::asio::buffer_size(in_buffer->data());
                fc::datastream<const char*> ds(d, s);
                state_request               req;
                fc::raw::unpack(ds, req);
                self->plugin->post_task_main_thread_medium([self, req = std::move(req)]() mutable { std::visit(*self, req); });
                self->start_read();
             });
          });
   }

   void send() override {
      if (sending)
         return;
      if (send_queue.empty()) {
         plugin->post_task_main_thread_medium([self = this->shared_from_this()]() {
            self->send_update();
         });
         return;
      }
      if (sending_block_session) {
         sending_block_session->others_waiting.insert( this->weak_from_this() );
         return;
      }
      sending = true;

      send_queue[0]->send(this->shared_from_this());
   }

   template <typename T>
   void send(T obj) {
      boost::asio::post(this->plugin->get_ship_executor(), [self = this->shared_from_this(), obj = std::move(obj)]() mutable {
         self->send_queue.push_back(
             std::make_unique<basic_send_queue_entry<session>>(fc::raw::pack(state_result{std::move(obj)})));
         self->send();
      });
   }

   void send(get_blocks_result_v0&& obj) {
      boost::asio::post(this->plugin->get_ship_executor(), [self = this->shared_from_this(), obj = std::move(obj)]() mutable {
         self->send_queue.push_back(std::make_unique<blocks_result_send_queue_entry<session>>(std::move(obj)));
         self->send();
      });
   }

   void pop_entry() {
      send_queue.erase(send_queue.begin());
      sending = false;
      send();
      send_others_waiting();
   }

   void send_others_waiting() {
      for( auto& w : others_waiting ) {
         auto s = w.lock();
         if( s ) {
            s->send();
         }
      }
      others_waiting.clear();
   }

   uint64_t get_trace_log_entry(const eosio::state_history::get_blocks_result_v0& result,
                                std::optional<locked_decompress_stream>& buf) {
      if (result.traces.has_value()) {
         auto& optional_log = plugin->get_trace_log();
         if( optional_log ) {
            buf.emplace( optional_log->create_locked_decompress_stream() );
            return optional_log->get_unpacked_entry( result.this_block->block_num, *buf );
         }
      }
      return 0;
   }

   uint64_t get_delta_log_entry(const eosio::state_history::get_blocks_result_v0& result,
                                std::optional<locked_decompress_stream>& buf) {
      if (result.deltas.has_value()) {
         auto& optional_log = plugin->get_chain_state_log();
         if( optional_log ) {
            buf.emplace( optional_log->create_locked_decompress_stream() );
            return optional_log->get_unpacked_entry( result.this_block->block_num, *buf );
         }
      }
      return 0;
   }

   void operator()(get_status_request_v0&) {
      fc_dlog(plugin->logger(), "got get_status_request_v0");

      get_status_result_v0 result;
      result.head              = plugin->get_block_head();
      result.last_irreversible = plugin->get_last_irreversible();
      result.chain_id          = plugin->get_chain_id();
      auto&& trace_log         = plugin->get_trace_log();
      if (trace_log) {
         auto r = trace_log->block_range();
         result.trace_begin_block = r.first;
         result.trace_end_block   = r.second;
      }
      auto&& chain_state_log = plugin->get_chain_state_log();
      if (chain_state_log) {
         auto r = chain_state_log->block_range();
         result.chain_state_begin_block = r.first;
         result.chain_state_end_block   = r.second;
      }
      fc_dlog(plugin->logger(), "pushing get_status_result_v0 to send queue");
      send(std::move(result));
   }

   // called from main thread
   void operator()(get_blocks_request_v0& req) {
      fc_dlog(plugin->logger(), "received get_blocks_request_v0 = ${req}", ("req", req));
      to_send_block_num = req.start_block_num;
      for (auto& cp : req.have_positions) {
         if (req.start_block_num <= cp.block_num)
            continue;
         auto id = plugin->get_block_id(cp.block_num);
         if (!id || *id != cp.block_id)
            req.start_block_num = std::min(req.start_block_num, cp.block_num);

         if (!id) {
            to_send_block_num = std::min(to_send_block_num, cp.block_num);
            fc_dlog(plugin->logger(), "block ${block_num} is not available", ("block_num", cp.block_num));
         } else if (*id != cp.block_id) {
            to_send_block_num = std::min(to_send_block_num, cp.block_num);
            fc_dlog(plugin->logger(),
                    "the id for block ${block_num} in block request have_positions does not match the existing",
                    ("block_num", cp.block_num));
         }
      }
      fc_dlog(plugin->logger(), "  get_blocks_request_v0 start_block_num set to ${num}", ("num", to_send_block_num));

      if( !req.have_positions.empty() ) {
         position_it = req.have_positions.begin();
      }

      current_request = std::move(req);
      send_update(true);
   }

   // called from main thread
   void operator()(get_blocks_ack_request_v0& req) {
      fc_dlog(plugin->logger(), "received get_blocks_ack_request_v0 = ${req}", ("req", req));
      if (!current_request) {
         fc_dlog(plugin->logger(), " no current get_blocks_request_v0, discarding the get_blocks_ack_request_v0");
         return;
      }
      current_request->max_messages_in_flight += req.num_messages;
      send_update();
   }

   // called from main thread
   void send_update(get_blocks_result_v0 result, const chain::block_state_ptr& block_state) {
      need_to_send_update = true;
      if (!current_request || !current_request->max_messages_in_flight)
         return;

      result.last_irreversible = plugin->get_last_irreversible();
      uint32_t current =
            current_request->irreversible_only ? result.last_irreversible.block_num : result.head.block_num;

      if (to_send_block_num > current || to_send_block_num >= current_request->end_block_num) {
         fc_dlog( plugin->logger(), "Not sending, to_send_block_num: ${s}, current: ${c} current_request.end_block_num: ${b}",
                  ("s", to_send_block_num)("c", current)("b", current_request->end_block_num) );
         return;
      }

      auto block_id = plugin->get_block_id(to_send_block_num);

      if (block_id && position_it && (*position_it)->block_num == to_send_block_num) {
         // This branch happens when the head block of nodeos is behind the head block of connecting client.
         // In addition, the client told us the corresponding block id for block_num we are going to send.
         // We can send the block when the block_id is different.
         auto& itr = *position_it;
         auto block_id_seen_by_client = itr->block_id;
         ++itr;
         if (itr == current_request->have_positions.end())
            position_it.reset();

         if(block_id_seen_by_client == *block_id) {
            ++to_send_block_num;
            return;
         }
      }

      if (block_id) {
         result.this_block  = block_position{to_send_block_num, *block_id};
         auto prev_block_id = plugin->get_block_id(to_send_block_num - 1);
         if (prev_block_id)
            result.prev_block = block_position{to_send_block_num - 1, *prev_block_id};
         if (current_request->fetch_block)
            plugin->get_block(to_send_block_num, block_state, result.block);
         if (current_request->fetch_traces && plugin->get_trace_log())
            result.traces.emplace();
         if (current_request->fetch_deltas && plugin->get_chain_state_log())
            result.deltas.emplace();
      }
      ++to_send_block_num;

      // during syncing if block is older than 5 min, log every 1000th block
      bool fresh_block = fc::time_point::now() - plugin->get_head_block_timestamp() < fc::minutes(5);
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
      need_to_send_update = to_send_block_num <= current &&
                            to_send_block_num < current_request->end_block_num;
   }

   // called from main thread
   void send_update(const chain::block_state_ptr& block_state) override {
      if (!current_request || !current_request->max_messages_in_flight)
         return;

      get_blocks_result_v0 result;
      result.head = {block_state->block_num, block_state->id};
      send_update(std::move(result), block_state);
   }

   // called from main thread
   void send_update(bool changed = false) {
      if (changed || need_to_send_update) {
         get_blocks_result_v0 result;
         result.head = plugin->get_block_head();
         send_update(std::move(result), {});
      }
   }

   template <typename F>
   void callback(boost::system::error_code ec, const char* what, F f) {
      if (plugin->stopping)
         return;

      if( !ec ) {
         try {
            f();
            return;
         } catch( const fc::exception& e ) {
            fc_elog( plugin->logger(), "${e}", ("e", e.to_detail_string()) );
         } catch( const std::exception& e ) {
            fc_elog( plugin->logger(), "${e}", ("e", e.what()) );
         } catch( ... ) {
            fc_elog( plugin->logger(), "unknown exception" );
         }
      } else {
         if (ec == boost::asio::error::eof || ec == boost::beast::websocket::error::closed) {
            fc_dlog(plugin->logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         } else {
            fc_elog(plugin->logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         }
      }

      // on exception allow session to be destroyed

      send_others_waiting();
      plugin->session_set.erase( this->shared_from_this() );
   }
};
} // namespace eosio
