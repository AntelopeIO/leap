#pragma once
#include <boost/asio/error.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/state_history/compression.hpp>
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/types.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>


extern const char* const state_history_plugin_abi;

namespace eosio {
using namespace state_history;
using tcp     = boost::asio::ip::tcp;
using unixs   = boost::asio::local::stream_protocol;
namespace ws  = boost::beast::websocket;
namespace bio = boost::iostreams;

class session_manager;

struct send_queue_entry_base {
   virtual ~send_queue_entry_base() = default;
   virtual void send_entry() = 0;
};

struct session_base {
   virtual void send_update(bool changed)                                     = 0;
   virtual void send_update(const eosio::chain::block_state_ptr& block_state) = 0;
   virtual ~session_base()                                                    = default;
   std::optional<get_blocks_request_v0> current_request;
};

class send_update_send_queue_entry : public send_queue_entry_base {
   std::shared_ptr<session_base> session;
   const chain::block_state_ptr block_state;
public:
   send_update_send_queue_entry(std::shared_ptr<session_base> s, chain::block_state_ptr block_state)
         : session(std::move(s))
         , block_state(std::move(block_state)){}

   void send_entry() override {
      if( block_state ) {
         session->send_update(block_state);
      } else {
         session->send_update(false);
      }
   }
};

// accessed from ship thread
class session_manager {
private:
   using entry_ptr = std::unique_ptr<send_queue_entry_base>;

   std::set<std::shared_ptr<session_base>> session_set;
   bool sending  = false;
   std::vector<entry_ptr> send_queue;

public:
   void insert(std::shared_ptr<session_base> s) {
      session_set.insert(std::move(s));
   }

   void remove(const std::shared_ptr<session_base>& s, bool active_entry) {
      session_set.erase( s );
      if (active_entry)
         pop_entry();
   }

   void add_send_queue(entry_ptr p) {
      send_queue.emplace_back(std::move(p));
      send();
   }

   void send() {
      if (sending)
         return;
      if (send_queue.empty()) {
         send_update();
         return;
      }
      sending = true;

      send_queue[0]->send_entry();
   }

   void pop_entry(bool call_send = true) {
      send_queue.erase(send_queue.begin());
      sending = false;
      if( call_send)
         send();
   }

   void send_update() {
      for( auto& s : session_set ) {
         add_send_queue(std::make_unique<send_update_send_queue_entry>(s, nullptr));
      }
   }

   void send_update(const chain::block_state_ptr& block_state) {
      for( auto& s : session_set ) {
         add_send_queue(std::make_unique<send_update_send_queue_entry>(s, block_state));
      }
   }

};

template <typename Session>
class status_result_send_queue_entry : public send_queue_entry_base {
   std::shared_ptr<Session> session;
   std::vector<char> data;

public:

   explicit status_result_send_queue_entry(std::shared_ptr<Session> s)
   : session(std::move(s)) {};

   void send_entry() override {
      fc_dlog(session->plugin->logger(), "replying get_status_request_v0");
      get_status_result_v0 result;
      result.head              = session->plugin->get_block_head();
      result.last_irreversible = session->plugin->get_last_irreversible();
      result.chain_id          = session->plugin->get_chain_id();
      auto&& trace_log         = session->plugin->get_trace_log();
      if (trace_log) {
         auto r = trace_log->block_range();
         result.trace_begin_block = r.first;
         result.trace_end_block   = r.second;
      }
      auto&& chain_state_log = session->plugin->get_chain_state_log();
      if (chain_state_log) {
         auto r = chain_state_log->block_range();
         result.chain_state_begin_block = r.first;
         result.chain_state_end_block   = r.second;
      }
      fc_dlog(session->plugin->logger(), "pushing get_status_result_v0 to send queue");

      data = fc::raw::pack(state_result{std::move(result)});

      session->socket_stream->async_write(boost::asio::buffer(data),
                                   [s{session}](boost::system::error_code ec, size_t) {
                                      s->callback(ec, true, "async_write", [s] { s->session_mgr.pop_entry(); });
                                   });
   }
};

template <typename Session>
class blocks_ack_request_send_queue_entry : public send_queue_entry_base {
   std::shared_ptr<Session> session;
   eosio::state_history::get_blocks_ack_request_v0 req;

public:
   explicit blocks_ack_request_send_queue_entry(std::shared_ptr<Session> s, get_blocks_ack_request_v0&& r)
   : session(std::move(s))
   , req(std::move(r)) {}

   void send_entry() override {
      session->current_request->max_messages_in_flight += req.num_messages;
      session->send_update(false);
   }
};

template <typename Session>
class blocks_request_send_queue_entry : public send_queue_entry_base {
   std::shared_ptr<Session> session;
   eosio::state_history::get_blocks_request_v0 req;

public:
   blocks_request_send_queue_entry(std::shared_ptr<Session> s, get_blocks_request_v0&& r)
   : session(std::move(s))
   , req(std::move(r)) {}

   void send_entry() override {
      fc_dlog(session->plugin->logger(), "replying get_blocks_request_v0 = ${req}", ("req", req));
      session->to_send_block_num = req.start_block_num;
      for (auto& cp : req.have_positions) {
         if (req.start_block_num <= cp.block_num)
            continue;
         auto id = session->plugin->get_block_id(cp.block_num);
         if (!id || *id != cp.block_id)
            req.start_block_num = std::min(req.start_block_num, cp.block_num);

         if (!id) {
            session->to_send_block_num = std::min(session->to_send_block_num, cp.block_num);
            fc_dlog(session->plugin->logger(), "block ${block_num} is not available", ("block_num", cp.block_num));
         } else if (*id != cp.block_id) {
            session->to_send_block_num = std::min(session->to_send_block_num, cp.block_num);
            fc_dlog(session->plugin->logger(),
                    "the id for block ${block_num} in block request have_positions does not match the existing",
                    ("block_num", cp.block_num));
         }
      }
      fc_dlog(session->plugin->logger(), "  get_blocks_request_v0 start_block_num set to ${num}", ("num", session->to_send_block_num));

      if( !req.have_positions.empty() ) {
         session->position_it = req.have_positions.begin();
      }

      session->current_request = std::move(req);
      session->send_update(true);
   }
};

template <typename Session>
class blocks_result_send_queue_entry : public send_queue_entry_base, public std::enable_shared_from_this<blocks_result_send_queue_entry<Session>> {
   std::shared_ptr<Session>                                        session;
   eosio::state_history::get_blocks_result_v0                      r;
   std::vector<char>                                               data;
   std::optional<locked_decompress_stream>                         stream;

   template <typename Next>
   void async_send(bool fin, const std::vector<char>& d, Next&& next) {
      session->socket_stream->async_write_some(
          fin, boost::asio::buffer(d),
          [me=this->shared_from_this(), next = std::forward<Next>(next)](boost::system::error_code ec, size_t) mutable {
             if( ec ) {
                me->stream.reset();
             }
             me->session->callback(ec, true, "async_write", [me, next = std::move(next)]() mutable {
                next();
             });
          });
   }

   template <typename Next>
   void async_send(bool fin, std::unique_ptr<bio::filtering_istreambuf>& strm, Next&& next) {
      data.resize(session->default_frame_size);
      auto size = bio::read(*strm, data.data(), session->default_frame_size);
      data.resize(size);
      bool eof = (strm->sgetc() == EOF);

      session->socket_stream->async_write_some( fin && eof, boost::asio::buffer(data),
          [me=this->shared_from_this(), fin, eof, next = std::forward<Next>(next)](boost::system::error_code ec, size_t) mutable {
             if( ec ) {
                me->stream.reset();
             }
             me->session->callback(ec, true, "async_write", [me, fin, eof, next = std::move(next)]() mutable {
                if (eof) {
                   next();
                } else {
                   me->async_send_buf(fin, std::move(next));
                }
             });
          });
   }

   template <typename Next>
   void async_send_buf(bool fin, Next&& next) {
      std::visit([me=this->shared_from_this(), fin, next = std::forward<Next>(next)](auto& d) mutable {
            me->async_send(fin, d, std::move(next));
         }, stream->buf);
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
                [is_deltas, entry_size, next = std::forward<Next>(next), me=this->shared_from_this()]() mutable {
                   if (entry_size) {
                      me->async_send_buf(is_deltas, [me, next = std::move(next)]() {
                         next();
                      });
                   } else
                      next();
                });
   }

   void send_deltas() {
      stream.reset();
      send_log(session->get_delta_log_entry(r, stream), true, [me=this->shared_from_this()]() {
         me->stream.reset();
         me->session->session_mgr.pop_entry();
      });
   }

   void send_traces() {
      stream.reset();
      send_log(session->get_trace_log_entry(r, stream), false, [me=this->shared_from_this()]() {
         me->send_deltas();
      });
   }

public:
   blocks_result_send_queue_entry(std::shared_ptr<Session> s, get_blocks_result_v0&& r)
       : session(std::move(s)),
         r(std::move(r)) {}

   void send_entry() override {
      // pack the state_result{get_blocks_result} excluding the fields `traces` and `deltas`
      fc::datastream<size_t> ss;
      fc::raw::pack(ss, fc::unsigned_int(1)); // pack the variant index of state_result{r}
      fc::raw::pack(ss, static_cast<const get_blocks_result_base&>(r));
      data.resize(ss.tellp());
      fc::datastream<char*> ds(data.data(), data.size());
      fc::raw::pack(ds, fc::unsigned_int(1)); // pack the variant index of state_result{r}
      fc::raw::pack(ds, static_cast<const get_blocks_result_base&>(r));

      async_send(false, data, [me=this->shared_from_this()]() {
         me->send_traces();
      });
   }
};

template <typename Plugin, typename SocketType>
struct session : session_base, std::enable_shared_from_this<session<Plugin, SocketType>> {
private:
   Plugin                 plugin;
   session_manager&       session_mgr;
   std::optional<ws::stream<SocketType>> socket_stream; // ship thread only after creation

   bool                   need_to_send_update = false;
   uint32_t               to_send_block_num = 0;
   std::optional<std::vector<block_position>::const_iterator> position_it;

   const int32_t          default_frame_size;

   friend class blocks_result_send_queue_entry<session>;
   friend class status_result_send_queue_entry<session>;
   friend class blocks_ack_request_send_queue_entry<session>;
   friend class blocks_request_send_queue_entry<session>;

public:

   session(Plugin plugin, SocketType socket, session_manager& sm)
       : plugin(std::move(plugin))
       , session_mgr(sm)
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
         self->callback(ec, false, "async_accept", [self] {
            self->socket_stream->binary(false);
            self->socket_stream->async_write(
                  boost::asio::buffer(state_history_plugin_abi, strlen(state_history_plugin_abi)),
                  [self](boost::system::error_code ec, size_t) {
                     self->callback(ec, false, "async_write", [self] {
                        self->socket_stream->binary(true);
                        self->start_read();
                     });
                  });
         });
      });
   }

private:
   void start_read() {
      auto in_buffer = std::make_shared<boost::beast::flat_buffer>();
      socket_stream->async_read(
          *in_buffer, [self = this->shared_from_this(), in_buffer](boost::system::error_code ec, size_t) {
             self->callback(ec, false, "async_read", [self, in_buffer] {
                auto d = boost::asio::buffer_cast<char const*>(boost::beast::buffers_front(in_buffer->data()));
                auto s = boost::asio::buffer_size(in_buffer->data());
                fc::datastream<const char*> ds(d, s);
                state_request               req;
                fc::raw::unpack(ds, req);
                std::visit( [self]( auto& r ) {
                   self->process( r );
                }, req );
                self->start_read();
             });
          });
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

   // called from ship thread
   void process(get_status_request_v0&) {
      fc_dlog(plugin->logger(), "received get_status_request_v0");

      session_mgr.add_send_queue(std::make_unique<status_result_send_queue_entry<session>>(this->shared_from_this()));
   }

   // called from ship thread
   void process(get_blocks_request_v0& req) {
      fc_dlog(plugin->logger(), "received get_blocks_request_v0 = ${req}", ("req", req));

      session_mgr.add_send_queue(std::make_unique<blocks_request_send_queue_entry<session>>(this->shared_from_this(), std::move(req)));
   }

   // called from ship thread
   void process(get_blocks_ack_request_v0& req) {
      fc_dlog(plugin->logger(), "received get_blocks_ack_request_v0 = ${req}", ("req", req));
      if (!current_request) {
         fc_dlog(plugin->logger(), " no current get_blocks_request_v0, discarding the get_blocks_ack_request_v0");
         return;
      }

      session_mgr.add_send_queue(std::make_unique<blocks_ack_request_send_queue_entry<session>>(this->shared_from_this(), std::move(req)));
   }

   // called from ship thread
   void send_update(get_blocks_result_v0 result, const chain::block_state_ptr& block_state) {
      need_to_send_update = true;
      if (!current_request || !current_request->max_messages_in_flight) {
         session_mgr.pop_entry(false);
         return;
      }

      result.last_irreversible = plugin->get_last_irreversible();
      uint32_t current =
            current_request->irreversible_only ? result.last_irreversible.block_num : result.head.block_num;

      if (to_send_block_num > current || to_send_block_num >= current_request->end_block_num) {
         fc_dlog( plugin->logger(), "Not sending, to_send_block_num: ${s}, current: ${c} current_request.end_block_num: ${b}",
                  ("s", to_send_block_num)("c", current)("b", current_request->end_block_num) );
         session_mgr.pop_entry(false);
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
            session_mgr.pop_entry(false);
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

      --current_request->max_messages_in_flight;
      need_to_send_update = to_send_block_num <= current &&
                            to_send_block_num < current_request->end_block_num;

      std::make_shared<blocks_result_send_queue_entry<session>>(this->shared_from_this(), std::move(result))->send_entry();
   }

   // called from ship thread
   void send_update(const chain::block_state_ptr& block_state) override {
      if (!current_request || !current_request->max_messages_in_flight) {
         session_mgr.pop_entry(false);
         return;
      }

      get_blocks_result_v0 result;
      result.head = {block_state->block_num, block_state->id};
      send_update(std::move(result), block_state);
   }

   // called from ship thread
   void send_update(bool changed) override {
      if (changed || need_to_send_update) {
         get_blocks_result_v0 result;
         result.head = plugin->get_block_head();
         send_update(std::move(result), {});
      } else {
         session_mgr.pop_entry(false);
      }
   }

   template <typename F>
   void callback(const boost::system::error_code& ec, bool active_entry, const char* what, F f) {
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
         if (ec == boost::asio::error::operation_aborted ||
             ec == boost::asio::error::connection_reset ||
             ec == boost::asio::error::eof ||
             ec == boost::beast::websocket::error::closed) {
            fc_dlog(plugin->logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         } else {
            fc_elog(plugin->logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         }
      }

      // on exception allow session to be destroyed

      session_mgr.remove( this->shared_from_this(), active_entry );
   }
};

} // namespace eosio
