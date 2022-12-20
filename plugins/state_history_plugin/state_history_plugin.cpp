#include <eosio/chain/config.hpp>
#include <eosio/resource_monitor_plugin/resource_monitor_plugin.hpp>
#include <eosio/state_history/compression.hpp>
#include <eosio/state_history/create_deltas.hpp>
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/trace_converter.hpp>
#include <eosio/state_history_plugin/state_history_plugin.hpp>
#include "session.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/host_name.hpp>

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>

#include <boost/signals2/connection.hpp>


namespace ws = boost::beast::websocket;


/* Prior to boost 1.70, if socket type is not boost::asio::ip::tcp::socket nor boost::asio::ssl::stream beast requires
   an overload of async_teardown. This has been improved in 1.70+ to support any basic_stream_socket<> out of the box
   which includes unix sockets. */
#if BOOST_VERSION < 107000
namespace boost::beast::websocket {
template <typename TeardownHandler>
void async_teardown(role_type, unixs::socket& sock, TeardownHandler&& handler) {
   boost::system::error_code ec;
   sock.close(ec);
   boost::asio::post(boost::asio::get_associated_executor(handler, sock.get_executor()),
                     [h = std::move(handler), ec]() mutable { h(ec); });
}
} // namespace boost::beast::websocket
#endif

// overload pattern for variant visitation
template <class... Ts>
struct overload : Ts... {
   using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

namespace eosio {
using namespace chain;
using namespace state_history;
using boost::signals2::scoped_connection;
namespace bio = boost::iostreams;

static appbase::abstract_plugin& _state_history_plugin = app().register_plugin<state_history_plugin>();

const std::string logger_name("state_history");
fc::logger        _log;

template <typename F>
auto catch_and_log(F f) {
   try {
      return f();
   } catch (const fc::exception& e) {
      fc_elog(_log, "${e}", ("e", e.to_detail_string()));
   } catch (const std::exception& e) {
      fc_elog(_log, "${e}", ("e", e.what()));
   } catch (...) {
      fc_elog(_log, "unknown exception");
   }
}

struct state_history_plugin_impl : std::enable_shared_from_this<state_history_plugin_impl> {
   chain_plugin*                    chain_plug = nullptr;
   std::optional<state_history_log> trace_log;
   std::optional<state_history_log> chain_state_log;
   bool                             trace_debug_mode = false;
   std::atomic<bool>                stopping         = false;
   std::optional<scoped_connection> applied_transaction_connection;
   std::optional<scoped_connection> block_start_connection;
   std::optional<scoped_connection> accepted_block_connection;
   string                           endpoint_address;
   uint16_t                         endpoint_port = 8080;
   string                           unix_path;
   state_history::trace_converter   trace_converter;

   constexpr static uint64_t default_frame_size =  1024 * 1024;


   using acceptor_type = std::variant<std::unique_ptr<tcp::acceptor>, std::unique_ptr<unixs::acceptor>>;
   std::set<acceptor_type> acceptor;

   std::thread                                                              thr;
   boost::asio::io_context                                                  ctx;
   boost::asio::io_context::strand                                          work_strand{ctx};
   boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard =
       boost::asio::make_work_guard(ctx);

   fc::logger& logger() { return _log; }

   std::optional<state_history_log>& get_trace_log() { return trace_log; }
   std::optional<state_history_log>& get_chain_state_log(){ return chain_state_log; }

   signed_block_ptr get_block(uint32_t block_num, const block_state_ptr& block_state) {
      chain::signed_block_ptr p;
      try {
         if (block_state && block_num == block_state->block_num) {
            p = block_state->block;
         } else {
            p = chain_plug->chain().fetch_block_by_number(block_num);
         }
      } catch (...) {
      }
      return p;
   }

   fc::sha256 get_chain_id() {
      return chain_plug->chain().get_chain_id();
   }

   void get_block(uint32_t block_num, const block_state_ptr& block_state, std::optional<bytes>& result) {
      auto p = get_block(block_num, block_state);
      if (p)
         result = fc::raw::pack(*p);
   }

   std::optional<chain::block_id_type> get_block_id(uint32_t block_num) {
      if (trace_log && block_num >= trace_log->begin_block() && block_num < trace_log->end_block())
         return trace_log->get_block_id(block_num);
      if (chain_state_log && block_num >= chain_state_log->begin_block() && block_num < chain_state_log->end_block())
         return chain_state_log->get_block_id(block_num);
      try {
         return chain_plug->chain().get_block_id_for_num(block_num);
      } catch (...) {
      }
      return {};
   }

   block_position get_block_head() {
      auto& chain              = chain_plug->chain();
      return {chain.head_block_num(), chain.head_block_id()};
   }

   block_position get_last_irreversible() {
      auto& chain              = chain_plug->chain();
      return {chain.last_irreversible_block_num(), chain.last_irreversible_block_id()};
   }

   std::optional<block_timestamp_type> get_block_timestamp(uint32_t block_num, const block_state_ptr& block_state) {
      auto p = get_block(block_num, block_state);
      if (p)
         return p->timestamp;
      return {};
   }

   template <typename Task>
   void post_task(Task&& task) {
      app().post(priority::medium, std::move(task));
   }

   class session_manager_t {
      std::mutex                                                mx;
      boost::container::flat_set<std::shared_ptr<session_base>> session_set;

    public:
      template <typename SocketType>
      void add(std::shared_ptr<state_history_plugin_impl> plugin, std::shared_ptr<SocketType> socket) {
         auto s = std::make_shared<session<std::shared_ptr<state_history_plugin_impl>, SocketType>>(plugin, std::move(*socket));
         s->start();
         std::lock_guard lock(mx);
         session_set.insert(std::move(s));
      }

      void remove(std::shared_ptr<session_base> s) {
         std::lock_guard lock(mx);
         session_set.erase(s);
      }

      template <typename F>
      void for_each(F&& f) {
         std::lock_guard lock(mx);
         for (auto& s : session_set) {
            f(s);
         }
      }
   } sessions;

   void remove_session(std::shared_ptr<session_base> s) { sessions.remove(s); }

   void listen() {
      boost::system::error_code ec;

      auto check_ec = [&](const char* what) {
         if (!ec)
            return;
         fc_elog(_log, "${w}: ${m}", ("w", what)("m", ec.message()));
         EOS_ASSERT(false, plugin_exception, "unable to open listen socket");
      };

      auto init_tcp_acceptor  = [&]() { acceptor.insert(std::make_unique<tcp::acceptor>(app().get_io_service())); };
      auto init_unix_acceptor = [&]() {
         // take a sniff and see if anything is already listening at the given socket path, or if the socket path exists
         //  but nothing is listening
         {
            boost::system::error_code test_ec;
            unixs::socket             test_socket(app().get_io_service());
            test_socket.connect(unix_path.c_str(), test_ec);

            // looks like a service is already running on that socket, don't touch it... fail out
            if (test_ec == boost::system::errc::success)
               ec = boost::system::errc::make_error_code(boost::system::errc::address_in_use);
            // socket exists but no one home, go ahead and remove it and continue on
            else if (test_ec == boost::system::errc::connection_refused)
               ::unlink(unix_path.c_str());
            else if (test_ec != boost::system::errc::no_such_file_or_directory)
               ec = test_ec;
         }
         check_ec("open");
         acceptor.insert(std::make_unique<unixs::acceptor>(this->ctx));
      };

      // create and configure acceptors, can be both
      if (endpoint_address.size()) init_tcp_acceptor();
      if (unix_path.size())        init_unix_acceptor();

      // start it
      std::for_each(acceptor.begin(), acceptor.end(), [&](const acceptor_type& acc) {
         std::visit(overloaded{[&](const std::unique_ptr<tcp::acceptor>& tcp_acc) {
                                auto address  = boost::asio::ip::make_address(endpoint_address);
                                auto endpoint = tcp::endpoint{address, endpoint_port};
                                tcp_acc->open(endpoint.protocol(), ec);
                                check_ec("open");
                                tcp_acc->set_option(boost::asio::socket_base::reuse_address(true));
                                tcp_acc->bind(endpoint, ec);
                                check_ec("bind");
                                tcp_acc->listen(boost::asio::socket_base::max_listen_connections, ec);
                                check_ec("listen");
                                do_accept(*tcp_acc);
                             },
                             [&](const std::unique_ptr<unixs::acceptor>& unx_acc) {
                                unx_acc->open(unixs::acceptor::protocol_type(), ec);
                                check_ec("open");
                                unx_acc->bind(unix_path.c_str(), ec);
                                check_ec("bind");
                                unx_acc->listen(boost::asio::socket_base::max_listen_connections, ec);
                                check_ec("listen");
                                do_accept(*unx_acc);
                             }},
                    acc);
      });
   }

   template <typename Acceptor>
   void do_accept(Acceptor& acceptor) {
      auto socket = std::make_shared<typename Acceptor::protocol_type::socket>(this->ctx);
      acceptor.async_accept(*socket, [self = shared_from_this(), this, socket, &acceptor](const boost::system::error_code& ec) {
         if (stopping)
            return;
         if (ec) {
            if (ec == boost::system::errc::too_many_files_open)
               catch_and_log([&] { do_accept(acceptor); });
            return;
         }
         catch_and_log([&] {
            sessions.add(self, socket);
         });
         catch_and_log([&] { do_accept(acceptor); });
      });
   }

   void on_applied_transaction(const transaction_trace_ptr& p, const packed_transaction_ptr& t) {
      if (trace_log)
         trace_converter.add_transaction(p, t);
   }

   void on_accepted_block(const block_state_ptr& block_state) {
      try {
         store_traces(block_state);
         store_chain_state(block_state);
      } catch (const fc::exception& e) {
         fc_elog(_log, "fc::exception: ${details}", ("details", e.to_detail_string()));
         // Both app().quit() and exception throwing are required. Without app().quit(),
         // the exception would be caught and drop before reaching main(). The exception is
         // to ensure the block won't be commited.
         appbase::app().quit();
         EOS_THROW(
             chain::state_history_write_exception,
             "State history encountered an Error which it cannot recover from.  Please resolve the error and relaunch "
             "the process");
      }

      sessions.for_each([&block_state](auto& p) {
         if (p) {
            if (p->current_request && block_state->block_num < p->current_request->start_block_num)
               p->current_request->start_block_num = block_state->block_num;
            p->send_update(block_state);
         }
      });
   }

   void on_block_start(uint32_t block_num) { clear_caches(); }

   void clear_caches() {
      trace_converter.cached_traces.clear();
      trace_converter.onblock_trace.reset();
   }

   void store_traces(const block_state_ptr& block_state) {
      if (!trace_log)
         return;

      state_history_log_header header{.magic        = ship_magic(ship_current_version, 0),
                                      .block_id     = block_state->id,
                                      .payload_size = 0};
      trace_log->pack_and_write_entry(header, block_state->block->previous, [this, &block_state](auto&& buf) {
         trace_converter.pack(buf, chain_plug->chain().db(), trace_debug_mode, block_state);
      });
   }

   void store_chain_state(const block_state_ptr& block_state) {
      if (!chain_state_log)
         return;
      bool fresh = chain_state_log->begin_block() == chain_state_log->end_block();
      if (fresh)
         fc_ilog(_log, "Placing initial state in block ${n}", ("n", block_state->block_num));

      state_history_log_header header{
          .magic = ship_magic(ship_current_version, 0), .block_id = block_state->id, .payload_size = 0};
      chain_state_log->pack_and_write_entry(header, block_state->header.previous, [this, fresh](auto&& buf) {
         pack_deltas(buf, chain_plug->chain().db(), fresh);
      });
   } // store_chain_state
};   // state_history_plugin_impl

state_history_plugin::state_history_plugin()
    : my(std::make_shared<state_history_plugin_impl>()) {}

state_history_plugin::~state_history_plugin() {}

void state_history_plugin::set_program_options(options_description& cli, options_description& cfg) {
   auto options = cfg.add_options();
   options("state-history-dir", bpo::value<bfs::path>()->default_value("state-history"),
           "the location of the state-history directory (absolute path or relative to application data dir)");
   cli.add_options()("delete-state-history", bpo::bool_switch()->default_value(false), "clear state history files");
   options("trace-history", bpo::bool_switch()->default_value(false), "enable trace history");
   options("chain-state-history", bpo::bool_switch()->default_value(false), "enable chain state history");
   options("state-history-endpoint", bpo::value<string>()->default_value("127.0.0.1:8080"),
           "the endpoint upon which to listen for incoming connections. Caution: only expose this port to "
           "your internal network.");
   options("state-history-unix-socket-path", bpo::value<string>(),
           "the path (relative to data-dir) to create a unix socket upon which to listen for incoming connections.");
   options("trace-history-debug-mode", bpo::bool_switch()->default_value(false), "enable debug mode for trace history");

   if(cfile::supports_hole_punching())
      options("state-history-log-retain-blocks", bpo::value<uint32_t>(), "if set, periodically prune the state history files to store only configured number of most recent blocks");
}

void state_history_plugin::plugin_initialize(const variables_map& options) {
   try {
      EOS_ASSERT(options.at("disable-replay-opts").as<bool>(), plugin_exception,
                 "state_history_plugin requires --disable-replay-opts");

      my->chain_plug = app().find_plugin<chain_plugin>();
      EOS_ASSERT(my->chain_plug, chain::missing_chain_plugin_exception, "");
      auto& chain = my->chain_plug->chain();
      my->applied_transaction_connection.emplace(chain.applied_transaction.connect(
          [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> t) {
             my->on_applied_transaction(std::get<0>(t), std::get<1>(t));
          }));
      my->accepted_block_connection.emplace(
          chain.accepted_block.connect([&](const block_state_ptr& p) { my->on_accepted_block(p); }));
      my->block_start_connection.emplace(
          chain.block_start.connect([&](uint32_t block_num) { my->on_block_start(block_num); }));

      auto                    dir_option = options.at("state-history-dir").as<bfs::path>();
      boost::filesystem::path state_history_dir;
      if (dir_option.is_relative())
         state_history_dir = app().data_dir() / dir_option;
      else
         state_history_dir = dir_option;
      if (auto resmon_plugin = app().find_plugin<resource_monitor_plugin>())
         resmon_plugin->monitor_directory(state_history_dir);

      auto ip_port = options.at("state-history-endpoint").as<string>();

      if (ip_port.size()) {
         auto port            = ip_port.substr(ip_port.find(':') + 1, ip_port.size());
         auto host            = ip_port.substr(0, ip_port.find(':'));
         my->endpoint_address = host;
         my->endpoint_port    = std::stoi(port);

         fc_dlog(_log, "PLUGIN_INITIALIZE ${ip_port} ${host} ${port}",
                 ("ip_port", ip_port)("host", host)("port", port));
      }

      if (options.count("state-history-unix-socket-path")) {
         boost::filesystem::path sock_path = options.at("state-history-unix-socket-path").as<string>();
         if (sock_path.is_relative())
            sock_path = app().data_dir() / sock_path;
         my->unix_path = sock_path.generic_string();
      }

      if (options.at("delete-state-history").as<bool>()) {
         fc_ilog(_log, "Deleting state history");
         boost::filesystem::remove_all(state_history_dir);
      }
      boost::filesystem::create_directories(state_history_dir);

      if (options.at("trace-history-debug-mode").as<bool>()) {
         my->trace_debug_mode = true;
      }

      std::optional<state_history_log_prune_config> ship_log_prune_conf;
      if (options.count("state-history-log-retain-blocks")) {
         ship_log_prune_conf.emplace();
         ship_log_prune_conf->prune_blocks = options.at("state-history-log-retain-blocks").as<uint32_t>();
         //the arbitrary limit of 1000 here is mainly so that there is enough buffer for newly applied forks to be delivered to clients
         // before getting pruned out. ideally pruning would have been smart enough to know not to prune reversible blocks
         EOS_ASSERT(ship_log_prune_conf->prune_blocks >= 1000, plugin_exception, "state-history-log-retain-blocks must be 1000 blocks or greater");
      }

      if (options.at("trace-history").as<bool>())
         my->trace_log.emplace("trace_history", (state_history_dir / "trace_history.log").string(),
                               (state_history_dir / "trace_history.index").string(), ship_log_prune_conf);
      if (options.at("chain-state-history").as<bool>())
         my->chain_state_log.emplace("chain_state_history", (state_history_dir / "chain_state_history.log").string(),
                                     (state_history_dir / "chain_state_history.index").string(), ship_log_prune_conf);
   }
   FC_LOG_AND_RETHROW()
} // state_history_plugin::plugin_initialize

void state_history_plugin::plugin_startup() {
   handle_sighup(); // setup logging

   try {
      my->thr = std::thread([ptr = my.get()] { ptr->ctx.run(); });
      auto bsp = my->chain_plug->chain().head_block_state();
      if( bsp && my->chain_state_log && my->chain_state_log->begin_block() == my->chain_state_log->end_block() ) {
         fc_ilog( _log, "Storing initial state on startup, this can take a considerable amount of time" );
         my->store_chain_state( bsp );
         fc_ilog( _log, "Done storing initial state on startup" );
      }
      my->listen();
   } catch (std::exception& ex) {
      appbase::app().quit();
   }
}

void state_history_plugin::plugin_shutdown() {
   my->applied_transaction_connection.reset();
   my->accepted_block_connection.reset();
   my->block_start_connection.reset();
   my->stopping = true;
   my->trace_log->stop();
   my->chain_state_log->stop();
   if (my->thr.joinable()) {
      my->work_guard.reset();
      my->ctx.stop();
      my->thr.join();
   }
}

void state_history_plugin::handle_sighup() { fc::logger::update(logger_name, _log); }

} // namespace eosio
