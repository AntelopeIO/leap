#pragma once
#include <eosio/state_history/compression.hpp>
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/types.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>


extern const char* const state_history_plugin_abi;

namespace eosio {

class session_manager;

struct send_queue_entry_base {
   virtual ~send_queue_entry_base() = default;
   virtual void send_entry()        = 0;
};

struct session_base {
   virtual void send_update(bool changed)                                     = 0;
   virtual void send_update(const chain::signed_block_ptr& block, const chain::block_id_type& id) = 0;
   virtual ~session_base()                                                    = default;

   std::optional<state_history::get_blocks_request> current_request;
   bool need_to_send_update = false;
};

class send_update_send_queue_entry : public send_queue_entry_base {
   std::shared_ptr<session_base> session;
   const chain::signed_block_ptr block;
   const chain::block_id_type id;
public:
   send_update_send_queue_entry(std::shared_ptr<session_base> s, chain::signed_block_ptr block, const chain::block_id_type& id)
         : session(std::move(s))
         , block(std::move(block))
         , id(id){}

   void send_entry() override {
      if( block) {
         session->send_update(block, id);
      } else {
         session->send_update(false);
      }
   }
};

/// Coordinate sending of queued entries. Only one session can read from the ship logs at a time so coordinate
/// their execution on the ship thread.
/// accessed from ship thread
class session_manager {
private:
   using entry_ptr = std::unique_ptr<send_queue_entry_base>;

   boost::asio::io_context& ship_io_context;
   std::set<std::shared_ptr<session_base>> session_set;
   bool sending  = false;
   std::deque<std::pair<std::shared_ptr<session_base>, entry_ptr>> send_queue;

public:
   explicit session_manager(boost::asio::io_context& ship_io_context)
   : ship_io_context(ship_io_context) {}

   void insert(std::shared_ptr<session_base> s) {
      session_set.insert(std::move(s));
   }

   void remove(const std::shared_ptr<session_base>& s, bool active_entry) {
      session_set.erase( s );
      if (active_entry)
         pop_entry();
   }

   bool is_active(const std::shared_ptr<session_base>& s) {
      return session_set.count(s);
   }

   void add_send_queue(std::shared_ptr<session_base> s, entry_ptr p) {
      send_queue.emplace_back(std::move(s), std::move(p));
      send();
   }

   void send() {
      if (sending)
         return;
      if (send_queue.empty()) {
         send_updates();
         return;
      }
      while (!send_queue.empty() && !is_active(send_queue[0].first)) {
         send_queue.erase(send_queue.begin());
      }

      if (!send_queue.empty()) {
         sending = true;
         send_queue[0].second->send_entry();
      } else {
         send();
      }
   }

   void pop_entry(bool call_send = true) {
      send_queue.erase(send_queue.begin());
      sending = false;
      if (call_send || !send_queue.empty()) {
         // avoid blowing the stack
         boost::asio::post(ship_io_context, [this]() {
            send();
         });
      }
   }

   void send_updates() {
      for( auto& s : session_set ) {
         if (s->need_to_send_update ) {
            add_send_queue(s, std::make_unique<send_update_send_queue_entry>(s, nullptr, chain::block_id_type{}));
         }
      }
   }

   void send_update(const chain::signed_block_ptr& block, const chain::block_id_type& id) {
      for( auto& s : session_set ) {
         add_send_queue(s, std::make_unique<send_update_send_queue_entry>(s, block, id));
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
      data = fc::raw::pack(state_history::state_result{session->get_status_result()});

      session->socket_stream->async_write(boost::asio::buffer(data),
                                   [s{session}](boost::system::error_code ec, size_t) {
                                      s->callback(ec, true, "async_write", [s] {
                                         s->session_mgr.pop_entry();
                                      });
                                   });
   }
};

template <typename Session>
class blocks_ack_request_send_queue_entry : public send_queue_entry_base {
   std::shared_ptr<Session> session;
   eosio::state_history::get_blocks_ack_request_v0 req;

public:
   explicit blocks_ack_request_send_queue_entry(std::shared_ptr<Session> s, state_history::get_blocks_ack_request_v0&& r)
   : session(std::move(s))
   , req(std::move(r)) {}

   void send_entry() override {
      assert(session->current_request);
      assert(std::holds_alternative<state_history::get_blocks_request_v0>(*session->current_request) ||
             std::holds_alternative<state_history::get_blocks_request_v1>(*session->current_request));

      std::visit([&](auto& request) { request.max_messages_in_flight += req.num_messages; },
                 *session->current_request);
      session->send_update(false);
   }
};

template <typename Session>
class blocks_request_send_queue_entry : public send_queue_entry_base {
   std::shared_ptr<Session> session;
   eosio::state_history::get_blocks_request req;

public:
   blocks_request_send_queue_entry(std::shared_ptr<Session> s, state_history::get_blocks_request&& r)
   : session(std::move(s))
   , req(std::move(r)) {}

   void send_entry() override {
      session->update_current_request(req);
      session->send_update(true);
   }
};

template <typename Session>
class blocks_result_send_queue_entry : public send_queue_entry_base, public std::enable_shared_from_this<blocks_result_send_queue_entry<Session>> {
   std::shared_ptr<Session>                                        session;
   state_history::get_blocks_result                                result;
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
   void send_log(uint64_t entry_size, bool fin, Next&& next) {
      if (entry_size) {
         data.resize(16); // should be at least for 1 byte (optional) + 10 bytes (variable sized uint64_t)
         fc::datastream<char*> ds(data.data(), data.size());
         fc::raw::pack(ds, true); // optional true
         history_pack_varuint64(ds, entry_size);
         data.resize(ds.tellp());
      } else {
         data = {'\0'}; // optional false
      }

      async_send(fin && entry_size == 0, data,
                [fin, entry_size, next = std::forward<Next>(next), me=this->shared_from_this()]() mutable {
                   if (entry_size) {
                      me->async_send_buf(fin, [me, next = std::move(next)]() {
                         next();
                      });
                   } else
                      next();
                });
   }

   // last to be sent if result is get_blocks_result_v1
   void send_finality_data() {
      assert(std::holds_alternative<state_history::get_blocks_result_v1>(result));
      stream.reset();
      send_log(session->get_finality_data_log_entry(std::get<state_history::get_blocks_result_v1>(result), stream), true, [me=this->shared_from_this()]() {
         me->stream.reset();
         me->session->session_mgr.pop_entry();
      });
   }

   // second to be sent if result is get_blocks_result_v1;
   // last to be sent if result is get_blocks_result_v0
   void send_deltas() {
      stream.reset();
      std::visit(chain::overloaded{
                    [&](state_history::get_blocks_result_v0& r) {
                        send_log(session->get_delta_log_entry(r, stream), true, [me=this->shared_from_this()]() {
                           me->stream.reset();
                           me->session->session_mgr.pop_entry();}); },
                    [&](state_history::get_blocks_result_v1& r) {
                        send_log(session->get_delta_log_entry(r, stream), false, [me=this->shared_from_this()]() {
                           me->send_finality_data(); }); }},
                 result);
   }

   // first to be sent
   void send_traces() {
      stream.reset();
      send_log(session->get_trace_log_entry(result, stream), false, [me=this->shared_from_this()]() {
         me->send_deltas();
      });
   }

   template<typename T>
   void pack_result_base(const T& result, uint32_t variant_index) {
      // pack the state_result{get_blocks_result} excluding the fields `traces` and `deltas`,
      // and `finality_data` if get_blocks_result_v1
      fc::datastream<size_t> ss;

      fc::raw::pack(ss, fc::unsigned_int(variant_index)); // pack the variant index of state_result{result}
      fc::raw::pack(ss, static_cast<const state_history::get_blocks_result_base&>(result));
      data.resize(ss.tellp());
      fc::datastream<char*> ds(data.data(), data.size());
      fc::raw::pack(ds, fc::unsigned_int(variant_index)); // pack the variant index of state_result{result}
      fc::raw::pack(ds, static_cast<const state_history::get_blocks_result_base&>(result));
   }

public:
   blocks_result_send_queue_entry(std::shared_ptr<Session> s, state_history::get_blocks_result&& r)
       : session(std::move(s)),
         result(std::move(r)) {}

   void send_entry() override {
      std::visit(
         chain::overloaded{
            [&](state_history::get_blocks_result_v0& r) {
               static_assert(std::is_same_v<state_history::get_blocks_result_v0, std::variant_alternative_t<1, state_history::state_result>>);
               pack_result_base(r, 1); // 1 for variant index of get_blocks_result_v0 in state_result
            },
            [&](state_history::get_blocks_result_v1& r) {
               static_assert(std::is_same_v<state_history::get_blocks_result_v1, std::variant_alternative_t<2, state_history::state_result>>);
               pack_result_base(r, 2); // 2 for variant index of get_blocks_result_v1 in state_result
            }
         },
         result
      );

      async_send(false, data, [me=this->shared_from_this()]() {
         me->send_traces();
      });
   }
};

template <typename Plugin, typename SocketType>
struct session : session_base, std::enable_shared_from_this<session<Plugin, SocketType>> {
private:
   Plugin&                plugin;
   session_manager&       session_mgr;
   std::optional<boost::beast::websocket::stream<SocketType>> socket_stream; // ship thread only after creation
   std::string            description;

   uint32_t               to_send_block_num = 0;
   std::optional<std::vector<state_history::block_position>::const_iterator> position_it;

   const int32_t          default_frame_size;

   friend class blocks_result_send_queue_entry<session>;
   friend class status_result_send_queue_entry<session>;
   friend class blocks_ack_request_send_queue_entry<session>;
   friend class blocks_request_send_queue_entry<session>;

public:

   session(Plugin& plugin, SocketType socket, session_manager& sm)
       : plugin(plugin)
       , session_mgr(sm)
       , socket_stream(std::move(socket))
       , default_frame_size(plugin.default_frame_size) {
      description = to_description_string();
   }

   void start() {
      fc_ilog(plugin.get_logger(), "incoming connection from ${a}", ("a", description));
      socket_stream->auto_fragment(false);
      socket_stream->binary(true);
      if constexpr (std::is_same_v<SocketType, boost::asio::ip::tcp::socket>) {
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
                state_history::state_request req;
                fc::raw::unpack(ds, req);
                std::visit( [self]( auto& r ) {
                   self->process( r );
                }, req );
                self->start_read();
             });
          });
   }

   // should only be called once per session
   std::string to_description_string() const {
      try {
         boost::system::error_code ec;
         auto re = socket_stream->next_layer().remote_endpoint(ec);
         return boost::lexical_cast<std::string>(re);
      } catch (...) {
         static uint32_t n = 0;
         return "unknown " + std::to_string(++n);
      }
   }

   uint64_t get_log_entry_impl(const eosio::state_history::get_blocks_result& result,
                               bool has_value,
                               std::optional<state_history_log>& optional_log,
                               std::optional<locked_decompress_stream>& buf) {
      if (has_value) {
         if( optional_log ) {
            buf.emplace( optional_log->create_locked_decompress_stream() );
            return std::visit([&](auto& r) { return optional_log->get_unpacked_entry( r.this_block->block_num, *buf ); }, result);
         }
      }
      return 0;
   }

   uint64_t get_trace_log_entry(const eosio::state_history::get_blocks_result& result,
                                std::optional<locked_decompress_stream>& buf) {
      return std::visit([&](auto& r) { return get_log_entry_impl(r, r.traces.has_value(), plugin.get_trace_log(), buf); }, result);
   }

   uint64_t get_delta_log_entry(const eosio::state_history::get_blocks_result& result,
                                std::optional<locked_decompress_stream>& buf) {
      return std::visit([&](auto& r) { return get_log_entry_impl(r, r.deltas.has_value(), plugin.get_chain_state_log(), buf); }, result);
   }

   uint64_t get_finality_data_log_entry(const eosio::state_history::get_blocks_result_v1& result,
                                        std::optional<locked_decompress_stream>& buf) {
      return get_log_entry_impl(result, result.finality_data.has_value(), plugin.get_finality_data_log(), buf);
   }

   void process(state_history::get_status_request_v0&) {
      fc_dlog(plugin.get_logger(), "received get_status_request_v0");

      auto self = this->shared_from_this();
      auto entry_ptr = std::make_unique<status_result_send_queue_entry<session>>(self);
      session_mgr.add_send_queue(std::move(self), std::move(entry_ptr));
   }

   void process(state_history::get_blocks_request_v0& req) {
      fc_dlog(plugin.get_logger(), "received get_blocks_request_v0 = ${req}", ("req", req));

      auto self = this->shared_from_this();
      auto entry_ptr = std::make_unique<blocks_request_send_queue_entry<session>>(self, std::move(req));
      session_mgr.add_send_queue(std::move(self), std::move(entry_ptr));
   }

   void process(state_history::get_blocks_request_v1& req) {
      fc_dlog(plugin.get_logger(), "received get_blocks_request_v1 = ${req}", ("req", req));

      auto self = this->shared_from_this();
      auto entry_ptr = std::make_unique<blocks_request_send_queue_entry<session>>(self, std::move(req));
      session_mgr.add_send_queue(std::move(self), std::move(entry_ptr));
   }

   void process(state_history::get_blocks_ack_request_v0& req) {
      fc_dlog(plugin.get_logger(), "received get_blocks_ack_request_v0 = ${req}", ("req", req));
      if (!current_request) {
         fc_dlog(plugin.get_logger(), " no current get_blocks_request_v0, discarding the get_blocks_ack_request_v0");
         return;
      }

      auto self = this->shared_from_this();
      auto entry_ptr = std::make_unique<blocks_ack_request_send_queue_entry<session>>(self, std::move(req));
      session_mgr.add_send_queue(std::move(self), std::move(entry_ptr));
   }

   state_history::get_status_result_v0 get_status_result() {
      fc_dlog(plugin.get_logger(), "replying get_status_request_v0");
      state_history::get_status_result_v0 result;
      result.head              = plugin.get_block_head();
      result.last_irreversible = plugin.get_last_irreversible();
      result.chain_id          = plugin.get_chain_id();
      auto&& trace_log         = plugin.get_trace_log();
      if (trace_log) {
         auto r = trace_log->block_range();
         result.trace_begin_block = r.first;
         result.trace_end_block   = r.second;
      }
      auto&& chain_state_log = plugin.get_chain_state_log();
      if (chain_state_log) {
         auto r = chain_state_log->block_range();
         result.chain_state_begin_block = r.first;
         result.chain_state_end_block   = r.second;
      }
      fc_dlog(plugin.get_logger(), "pushing get_status_result_v0 to send queue");

      return result;
   }

   void update_current_request_impl(state_history::get_blocks_request_v0& req) {
      fc_dlog(plugin.get_logger(), "replying get_blocks_request_v0 = ${req}", ("req", req));
      to_send_block_num = std::max(req.start_block_num, plugin.get_first_available_block_num());
      for (auto& cp : req.have_positions) {
         if (req.start_block_num <= cp.block_num)
            continue;
         auto id = plugin.get_block_id(cp.block_num);
         if (!id || *id != cp.block_id)
            req.start_block_num = std::min(req.start_block_num, cp.block_num);

         if (!id) {
            to_send_block_num = std::min(to_send_block_num, cp.block_num);
            fc_dlog(plugin.get_logger(), "block ${block_num} is not available", ("block_num", cp.block_num));
         } else if (*id != cp.block_id) {
            to_send_block_num = std::min(to_send_block_num, cp.block_num);
            fc_dlog(plugin.get_logger(), "the id for block ${block_num} in block request have_positions does not match the existing",
                    ("block_num", cp.block_num));
         }
      }
      fc_dlog(plugin.get_logger(), "  get_blocks_request_v0 start_block_num set to ${num}", ("num", to_send_block_num));

      if( !req.have_positions.empty() ) {
         position_it = req.have_positions.begin();
      }
   }

   void update_current_request(state_history::get_blocks_request& req) {
      assert(std::holds_alternative<state_history::get_blocks_request_v0>(req) ||
             std::holds_alternative<state_history::get_blocks_request_v1>(req));

      std::visit( [&](auto& request) { update_current_request_impl(request); }, req );
      current_request = std::move(req);
   }

   template<typename T> // get_blocks_result_v0 or get_blocks_result_v1
   void send_update(state_history::get_blocks_request_v0& request, bool fetch_finality_data, T&& result, const chain::signed_block_ptr& block, const chain::block_id_type& id) {
      need_to_send_update = true;

      result.last_irreversible = plugin.get_last_irreversible();
      uint32_t current =
            request.irreversible_only ? result.last_irreversible.block_num : result.head.block_num;

      fc_dlog( plugin.get_logger(), "irreversible_only: ${i}, last_irreversible: ${p}, head.block_num: ${h}", ("i", request.irreversible_only)("p", result.last_irreversible.block_num)("h", result.head.block_num));
      if (to_send_block_num > current || to_send_block_num >= request.end_block_num) {
         fc_dlog( plugin.get_logger(), "Not sending, to_send_block_num: ${s}, current: ${c} request.end_block_num: ${b}",
                  ("s", to_send_block_num)("c", current)("b", request.end_block_num) );
         session_mgr.pop_entry(false);
         return;
      }

      // not just an optimization, on accepted_block signal may not be able to find block_num in forkdb as it has not been validated
      // until after the accepted_block signal
      std::optional<chain::block_id_type> block_id =
          (block && block->block_num() == to_send_block_num) ? id : plugin.get_block_id(to_send_block_num);

      if (block_id && position_it && (*position_it)->block_num == to_send_block_num) {
         // This branch happens when the head block of nodeos is behind the head block of connecting client.
         // In addition, the client told us the corresponding block id for block_num we are going to send.
         // We can send the block when the block_id is different.
         auto& itr = *position_it;
         auto block_id_seen_by_client = itr->block_id;
         ++itr;
         if (itr == request.have_positions.end())
            position_it.reset();

         if(block_id_seen_by_client == *block_id) {
            ++to_send_block_num;
            session_mgr.pop_entry(false);
            return;
         }
      }

      if (block_id) {
         result.this_block  = state_history::block_position{to_send_block_num, *block_id};
         auto prev_block_id = plugin.get_block_id(to_send_block_num - 1);
         if (prev_block_id)
            result.prev_block = state_history::block_position{to_send_block_num - 1, *prev_block_id};
         if (request.fetch_block) {
            uint32_t block_num = block ? block->block_num() : 0; // block can be nullptr in testing
            plugin.get_block(to_send_block_num, block_num, block, result.block);
         }
         if (request.fetch_traces && plugin.get_trace_log())
            result.traces.emplace();
         if (request.fetch_deltas && plugin.get_chain_state_log())
            result.deltas.emplace();
         if constexpr (std::is_same_v<T, state_history::get_blocks_result_v1>) {
            if (fetch_finality_data && plugin.get_finality_data_log()) {
               result.finality_data.emplace();
            }
         }
      }
      ++to_send_block_num;

      // during syncing if block is older than 5 min, log every 1000th block
      bool fresh_block = fc::time_point::now() - plugin.get_head_block_timestamp() < fc::minutes(5);
      if (fresh_block || (result.this_block && result.this_block->block_num % 1000 == 0)) {
         fc_ilog(plugin.get_logger(),
                 "pushing result "
                 "{\"head\":{\"block_num\":${head}},\"last_irreversible\":{\"block_num\":${last_irr}},\"this_block\":{"
                 "\"block_num\":${this_block}, \"block_id\":${this_id}}} to send queue",
                 ("head", result.head.block_num)("last_irr", result.last_irreversible.block_num)
                 ("this_block", result.this_block ? result.this_block->block_num : fc::variant())
                 ("this_id", result.this_block ? fc::variant{result.this_block->block_id} : fc::variant{}));
      }

      --request.max_messages_in_flight;
      need_to_send_update = to_send_block_num <= current &&
                            to_send_block_num < request.end_block_num;

      std::make_shared<blocks_result_send_queue_entry<session>>(this->shared_from_this(), std::move(result))->send_entry();
   }

   bool no_request_or_not_max_messages_in_flight() {
      if (!current_request)
         return true;

      uint32_t max_messages_in_flight = std::visit(
         [&](auto& request) -> uint32_t { return request.max_messages_in_flight; },
         *current_request);

      return !max_messages_in_flight;
   }

   void send_update(const state_history::block_position& head, const chain::signed_block_ptr& block, const chain::block_id_type& id) {
      if (no_request_or_not_max_messages_in_flight()) {
         session_mgr.pop_entry(false);
         return;
      }

      assert(current_request);
      assert(std::holds_alternative<state_history::get_blocks_request_v0>(*current_request) ||
             std::holds_alternative<state_history::get_blocks_request_v1>(*current_request));

      std::visit(eosio::chain::overloaded{
                    [&](eosio::state_history::get_blocks_request_v0& request) {
                       state_history::get_blocks_result_v0 result;
                       result.head = head;
                       send_update(request, false, std::move(result), block, id); },
                    [&](eosio::state_history::get_blocks_request_v1& request) {
                       state_history::get_blocks_result_v1 result;
                       result.head = head;
                       send_update(request, request.fetch_finality_data, std::move(result), block, id); } },
                 *current_request);
   }

   void send_update(const chain::signed_block_ptr& block, const chain::block_id_type& id) override {
      if (no_request_or_not_max_messages_in_flight()) {
         session_mgr.pop_entry(false);
         return;
      }

      auto block_num = block->block_num();
      to_send_block_num = std::min(block_num, to_send_block_num);
      send_update(state_history::block_position{block_num, id}, block, id);
   }

   void send_update(bool changed) override {
      if (changed || need_to_send_update) {
         send_update(plugin.get_block_head(), nullptr, chain::block_id_type{});
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
            fc_elog( plugin.get_logger(), "${e}", ("e", e.to_detail_string()) );
         } catch( const std::exception& e ) {
            fc_elog( plugin.get_logger(), "${e}", ("e", e.what()) );
         } catch( ... ) {
            fc_elog( plugin.get_logger(), "unknown exception" );
         }
      } else {
         if (ec == boost::asio::error::operation_aborted ||
             ec == boost::asio::error::connection_reset ||
             ec == boost::asio::error::eof ||
             ec == boost::beast::websocket::error::closed) {
            fc_dlog(plugin.get_logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         } else {
            fc_elog(plugin.get_logger(), "${w}: ${m}", ("w", what)("m", ec.message()));
         }
      }

      // on exception allow session to be destroyed

      fc_ilog(plugin.get_logger(), "Closing connection from ${a}", ("a", description));
      session_mgr.remove( this->shared_from_this(), active_entry );
   }
};

} // namespace eosio
