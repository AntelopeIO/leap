#include "eosio/chain/block_header.hpp"
#include <eosio/chain/config.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/resource_monitor_plugin/resource_monitor_plugin.hpp>
#include <eosio/state_history/compression.hpp>
#include <eosio/state_history/create_deltas.hpp>
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/trace_converter.hpp>
#include <eosio/state_history_plugin/session.hpp>
#include <eosio/state_history_plugin/state_history_plugin.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/host_name.hpp>

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>

#include <boost/signals2/connection.hpp>
#include <mutex>

#include <fc/network/listener.hpp>

namespace ws = boost::beast::websocket;

namespace eosio {
using namespace chain;
using namespace state_history;
using boost::signals2::scoped_connection;
namespace bio = boost::iostreams;

static auto _state_history_plugin = application::register_plugin<state_history_plugin>();

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
   constexpr static uint64_t default_frame_size = 1024 * 1024;

private:
   chain_plugin*                    chain_plug = nullptr;
   std::optional<state_history_log> trace_log;
   std::optional<state_history_log> chain_state_log;
   bool                             trace_debug_mode = false;
   std::optional<scoped_connection> applied_transaction_connection;
   std::optional<scoped_connection> block_start_connection;
   std::optional<scoped_connection> accepted_block_connection;
   string                           endpoint_address;
   string                           unix_path;
   state_history::trace_converter   trace_converter;
   session_manager                  session_mgr;

   mutable std::mutex mtx;
   block_id_type      head_id;
   block_id_type      lib_id;
   time_point         head_timestamp;

   named_thread_pool<struct ship> thread_pool;

   bool  plugin_started = false;

public:
   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();
   session_manager& get_session_manager() { return session_mgr; }

   static fc::logger& get_logger() { return _log; }

   std::optional<state_history_log>& get_trace_log() { return trace_log; }
   std::optional<state_history_log>& get_chain_state_log(){ return chain_state_log; }

   boost::asio::io_context& get_ship_executor() { return thread_pool.get_executor(); }

   // thread-safe
   signed_block_ptr get_block(uint32_t block_num, const block_state_ptr& block_state) const {
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

   // thread safe
   fc::sha256 get_chain_id() const {
      return chain_plug->chain().get_chain_id();
   }

   // thread-safe
   void get_block(uint32_t block_num, const block_state_ptr& block_state, std::optional<bytes>& result) const {
      auto p = get_block(block_num, block_state);
      if (p)
         result = fc::raw::pack(*p);
   }

   // thread-safe
   std::optional<chain::block_id_type> get_block_id(uint32_t block_num) {
      if (trace_log)
         return trace_log->get_block_id(block_num);
      if (chain_state_log)
         return chain_state_log->get_block_id(block_num);
      try {
         return chain_plug->chain().get_block_id_for_num(block_num);
      } catch (...) {
      }
      return {};
   }

   // thread-safe
   block_position get_block_head() const {
      std::lock_guard g(mtx);
      return { block_header::num_from_id(head_id), head_id };
   }

   // thread-safe
   block_position get_last_irreversible() const {
      std::lock_guard g(mtx);
      return { block_header::num_from_id(lib_id), lib_id };
   }

   // thread-safe
   time_point get_head_block_timestamp() const {
      std::lock_guard g(mtx);
      return head_timestamp;
   }

   void listen();

   // called from main thread
   void on_applied_transaction(const transaction_trace_ptr& p, const packed_transaction_ptr& t) {
      if (trace_log)
         trace_converter.add_transaction(p, t);
   }

   // called from main thread
   void on_accepted_block(const block_state_ptr& block_state) {
      {
         const auto& chain = chain_plug->chain();
         std::lock_guard g(mtx);
         head_id = chain.head_block_id();
         lib_id = chain.last_irreversible_block_id();
         head_timestamp = chain.head_block_time();
      }

      try {
         store_traces(block_state);
         store_chain_state(block_state);
      } catch (const fc::exception& e) {
         fc_elog(_log, "fc::exception: ${details}", ("details", e.to_detail_string()));
         // Both app().quit() and exception throwing are required. Without app().quit(),
         // the exception would be caught and drop before reaching main(). The exception is
         // to ensure the block won't be committed.
         appbase::app().quit();
         EOS_THROW(
             chain::state_history_write_exception, // controller_emit_signal_exception, so it flow through emit()
             "State history encountered an Error which it cannot recover from.  Please resolve the error and relaunch "
             "the process");
      }

      // avoid accumulating all these posts during replay before ship threads started
      // that can lead to a large memory consumption and failures
      // this is safe as there are no clients connected until after replay is complete
      // this method is called from the main thread and "plugin_started" is set on the main thread as well when plugin is started 
      if (plugin_started) {
         boost::asio::post(get_ship_executor(), [self = this->shared_from_this(), block_state]() {
            self->get_session_manager().send_update(block_state);
         });
      }

   }

   // called from main thread
   void on_block_start(uint32_t block_num) {
      clear_caches();
   }

   // called from main thread
   void clear_caches() {
      trace_converter.cached_traces.clear();
      trace_converter.onblock_trace.reset();
   }

   // called from main thread
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

   // called from main thread
   void store_chain_state(const block_state_ptr& block_state) {
      if (!chain_state_log)
         return;
      bool fresh = chain_state_log->empty();
      if (fresh)
         fc_ilog(_log, "Placing initial state in block ${n}", ("n", block_state->block_num));

      state_history_log_header header{
          .magic = ship_magic(ship_current_version, 0), .block_id = block_state->id, .payload_size = 0};
      chain_state_log->pack_and_write_entry(header, block_state->header.previous, [this, fresh](auto&& buf) {
         pack_deltas(buf, chain_plug->chain().db(), fresh);
      });
   } // store_chain_state

   ~state_history_plugin_impl() {
   }

}; // state_history_plugin_impl

template <typename Protocol>
struct ship_listener : fc::listener<ship_listener<Protocol>, Protocol> {
   using socket_type = typename Protocol::socket;

   static constexpr uint32_t accept_timeout_ms = 200;

   state_history_plugin_impl& state_;

   ship_listener(boost::asio::io_context& executor, logger& logger, const std::string& local_address,
                 const typename Protocol::endpoint& endpoint, state_history_plugin_impl& state)
       : fc::listener<ship_listener<Protocol>, Protocol>(
             executor, logger, boost::posix_time::milliseconds(accept_timeout_ms), local_address, endpoint)
       , state_(state) {}

   void create_session(socket_type&& socket) {
      // Create a session object and run it
      catch_and_log([&] {
         auto s = std::make_shared<session<state_history_plugin_impl, socket_type>>(
            state_, std::move(socket), state_.get_session_manager());
         state_.get_session_manager().insert(s);
         s->start();
      });
   }
};

void state_history_plugin_impl::listen() {
   try {
      if (!endpoint_address.empty()) {
         ship_listener<boost::asio::ip::tcp>::create(thread_pool.get_executor(), _log, endpoint_address, *this);
      }
      if (!unix_path.empty()) {
         ship_listener<boost::asio::local::stream_protocol>::create(thread_pool.get_executor(), _log, unix_path, *this);
      }
   } catch (std::exception&) {
      FC_THROW_EXCEPTION(plugin_exception, "unable to open listen socket");
   }
}

state_history_plugin::state_history_plugin()
    : my(std::make_shared<state_history_plugin_impl>()) {}

state_history_plugin::~state_history_plugin() = default;

void state_history_plugin::set_program_options(options_description& cli, options_description& cfg) {
   auto options = cfg.add_options();
   options("state-history-dir", bpo::value<std::filesystem::path>()->default_value("state-history"),
           "the location of the state-history directory (absolute path or relative to application data dir)");
   options("state-history-retained-dir", bpo::value<std::filesystem::path>(),
           "the location of the state history retained directory (absolute path or relative to state-history dir).");
   options("state-history-archive-dir", bpo::value<std::filesystem::path>(),
           "the location of the state history archive directory (absolute path or relative to state-history dir).\n"
           "If the value is empty string, blocks files beyond the retained limit will be deleted.\n"
           "All files in the archive directory are completely under user's control, i.e. they won't be accessed by nodeos anymore.");
   options("state-history-stride", bpo::value<uint32_t>(),
         "split the state history log files when the block number is the multiple of the stride\n"
         "When the stride is reached, the current history log and index will be renamed '*-history-<start num>-<end num>.log/index'\n"
         "and a new current history log and index will be created with the most recent blocks. All files following\n"
         "this format will be used to construct an extended history log.");
   options("max-retained-history-files", bpo::value<uint32_t>(),
          "the maximum number of history file groups to retain so that the blocks in those files can be queried.\n"
          "When the number is reached, the oldest history file would be moved to archive dir or deleted if the archive dir is empty.\n"
          "The retained history log files should not be manipulated by users." );
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

void state_history_plugin_impl::plugin_initialize(const variables_map& options) {
   try {
      EOS_ASSERT(options.at("disable-replay-opts").as<bool>(), plugin_exception,
                 "state_history_plugin requires --disable-replay-opts");

      chain_plug = app().find_plugin<chain_plugin>();
      EOS_ASSERT(chain_plug, chain::missing_chain_plugin_exception, "");
      auto& chain = chain_plug->chain();
      applied_transaction_connection.emplace(chain.applied_transaction.connect(
          [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> t) {
             on_applied_transaction(std::get<0>(t), std::get<1>(t));
          }));
      accepted_block_connection.emplace(
          chain.accepted_block.connect([&](const block_state_ptr& p) { on_accepted_block(p); }));
      block_start_connection.emplace(
          chain.block_start.connect([&](uint32_t block_num) { on_block_start(block_num); }));

      auto                    dir_option = options.at("state-history-dir").as<std::filesystem::path>();
      std::filesystem::path state_history_dir;
      if (dir_option.is_relative())
         state_history_dir = app().data_dir() / dir_option;
      else
         state_history_dir = dir_option;
      if (auto resmon_plugin = app().find_plugin<resource_monitor_plugin>())
         resmon_plugin->monitor_directory(state_history_dir);

      endpoint_address = options.at("state-history-endpoint").as<string>();

      if (options.count("state-history-unix-socket-path")) {
         std::filesystem::path sock_path = options.at("state-history-unix-socket-path").as<string>();
         if (sock_path.is_relative())
            sock_path = app().data_dir() / sock_path;
         unix_path = sock_path.generic_string();
      }

      if (options.at("delete-state-history").as<bool>()) {
         fc_ilog(_log, "Deleting state history");
         std::filesystem::remove_all(state_history_dir);
      }
      std::filesystem::create_directories(state_history_dir);

      if (options.at("trace-history-debug-mode").as<bool>()) {
         trace_debug_mode = true;
      }

      bool has_state_history_partition_options =
          options.count("state-history-retained-dir") || options.count("state-history-archive-dir") ||
          options.count("state-history-stride") || options.count("max-retained-history-files");

      state_history_log_config ship_log_conf;
      if (options.count("state-history-log-retain-blocks")) {
         auto& ship_log_prune_conf = ship_log_conf.emplace<state_history::prune_config>();
         ship_log_prune_conf.prune_blocks = options.at("state-history-log-retain-blocks").as<uint32_t>();
         //the arbitrary limit of 1000 here is mainly so that there is enough buffer for newly applied forks to be delivered to clients
         // before getting pruned out. ideally pruning would have been smart enough to know not to prune reversible blocks
         EOS_ASSERT(ship_log_prune_conf.prune_blocks >= 1000, plugin_exception, "state-history-log-retain-blocks must be 1000 blocks or greater");
         EOS_ASSERT(!has_state_history_partition_options, plugin_exception, "state-history-log-retain-blocks cannot be used together with state-history-retained-dir,"
                  " state-history-archive-dir, state-history-stride or max-retained-history-files");
      } else if (has_state_history_partition_options){
         auto& config  = ship_log_conf.emplace<state_history::partition_config>();
         if (options.count("state-history-retained-dir"))
            config.retained_dir       = options.at("state-history-retained-dir").as<std::filesystem::path>();
         if (options.count("state-history-archive-dir"))
            config.archive_dir        = options.at("state-history-archive-dir").as<std::filesystem::path>();
         if (options.count("state-history-stride"))
            config.stride             = options.at("state-history-stride").as<uint32_t>();
         if (options.count("max-retained-history-files"))
            config.max_retained_files = options.at("max-retained-history-files").as<uint32_t>();
      }

      if (options.at("trace-history").as<bool>())
         trace_log.emplace("trace_history", state_history_dir , ship_log_conf);
      if (options.at("chain-state-history").as<bool>())
         chain_state_log.emplace("chain_state_history", state_history_dir, ship_log_conf);
   }
   FC_LOG_AND_RETHROW()
} // state_history_plugin::plugin_initialize

void state_history_plugin::plugin_initialize(const variables_map& options) {
   handle_sighup(); // setup logging
   my->plugin_initialize(options);
}

void state_history_plugin_impl::plugin_startup() {
   auto bsp = chain_plug->chain().head_block_state();
   if (bsp && chain_state_log && chain_state_log->empty()) {
      fc_ilog(_log, "Storing initial state on startup, this can take a considerable amount of time");
      store_chain_state(bsp);
      fc_ilog(_log, "Done storing initial state on startup");
   }
   listen();
   // use of executor assumes only one thread
   thread_pool.start(1, [](const fc::exception& e) {
      fc_elog(_log, "Exception in SHiP thread pool, exiting: ${e}", ("e", e.to_detail_string()));
      app().quit();
   });
   plugin_started = true;
}

void state_history_plugin::plugin_startup() {
   my->plugin_startup();
}

void state_history_plugin_impl::plugin_shutdown() {
   applied_transaction_connection.reset();
   accepted_block_connection.reset();
   block_start_connection.reset();
   thread_pool.stop();
}

void state_history_plugin::plugin_shutdown() {
   my->plugin_shutdown();
}

void state_history_plugin::handle_sighup() {
   fc::logger::update(logger_name, _log);
}

const state_history_log* state_history_plugin::trace_log() const {
   const auto& log = my->get_trace_log();
   return log ? std::addressof(*log) : nullptr;
}

const state_history_log* state_history_plugin::chain_state_log() const {
   const auto& log = my->get_chain_state_log();
   return log ? std::addressof(*log) : nullptr;
}

} // namespace eosio
