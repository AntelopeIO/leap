#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/producer_plugin/block_timing_util.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/snapshot_scheduler.hpp>
#include <eosio/chain/subjective_billing.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/unapplied_transaction_queue.hpp>
#include <eosio/resource_monitor_plugin/resource_monitor_plugin.hpp>

#include <fc/io/json.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/scoped_exit.hpp>
#include <fc/time.hpp>

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/signals2/connection.hpp>

#include <cstdint>
#include <iostream>
#include <algorithm>
#include <mutex>

namespace bmi = boost::multi_index;
using bmi::hashed_unique;
using bmi::indexed_by;
using bmi::member;
using bmi::ordered_non_unique;
using bmi::tag;

using boost::multi_index_container;

using boost::signals2::scoped_connection;
using std::string;
using std::vector;

#undef FC_LOG_AND_DROP
#define LOG_AND_DROP()                                                                         \
   catch (const guard_exception& e) {                                                          \
      chain_plugin::handle_guard_exception(e);                                                 \
   }                                                                                           \
   catch (const std::bad_alloc&) {                                                             \
      chain_apis::api_base::handle_bad_alloc();                                                \
   }                                                                                           \
   catch (boost::interprocess::bad_alloc&) {                                                   \
      chain_apis::api_base::handle_db_exhaustion();                                            \
   }                                                                                           \
   catch (fc::exception & er) {                                                                \
      wlog("${details}", ("details", er.to_detail_string()));                                  \
   }                                                                                           \
   catch (const std::exception& e) {                                                           \
      fc::exception fce(FC_LOG_MESSAGE(warn, "std::exception: ${what}: ", ("what", e.what())), \
                        fc::std_exception_code,                                                \
                        BOOST_CORE_TYPEID(e).name(),                                           \
                        e.what());                                                             \
      wlog("${details}", ("details", fce.to_detail_string()));                                 \
   }                                                                                           \
   catch (...) {                                                                               \
      fc::unhandled_exception e(FC_LOG_MESSAGE(warn, "unknown: ", ), std::current_exception());\
      wlog("${details}", ("details", e.to_detail_string()));                                   \
   }

const std::string logger_name("producer_plugin");
fc::logger        _log;

const std::string trx_successful_trace_logger_name("transaction_success_tracing");
fc::logger        _trx_successful_trace_log;

const std::string trx_failed_trace_logger_name("transaction_failure_tracing");
fc::logger        _trx_failed_trace_log;

const std::string trx_trace_success_logger_name("transaction_trace_success");
fc::logger        _trx_trace_success_log;

const std::string trx_trace_failure_logger_name("transaction_trace_failure");
fc::logger        _trx_trace_failure_log;

const std::string trx_logger_name("transaction");
fc::logger        _trx_log;

const std::string transient_trx_successful_trace_logger_name("transient_trx_success_tracing");
fc::logger        _transient_trx_successful_trace_log;

const std::string transient_trx_failed_trace_logger_name("transient_trx_failure_tracing");
fc::logger        _transient_trx_failed_trace_log;

namespace eosio {

static auto _producer_plugin = application::register_plugin<producer_plugin>();

using namespace eosio::chain;
using namespace eosio::chain::plugin_interface;

namespace {
bool exception_is_exhausted(const fc::exception& e) {
   auto code = e.code();
   return (code == block_cpu_usage_exceeded::code_value) ||
          (code == block_net_usage_exceeded::code_value) ||
          (code == deadline_exception::code_value) ||
          (code == ro_trx_vm_oc_compile_temporary_failure::code_value);
}
} // namespace

struct transaction_id_with_expiry {
   transaction_id_type trx_id;
   fc::time_point      expiry;
};

struct by_id;
struct by_expiry;

using transaction_id_with_expiry_index = multi_index_container<
   transaction_id_with_expiry,
   indexed_by<hashed_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER(transaction_id_with_expiry, transaction_id_type, trx_id)>,
              ordered_non_unique<tag<by_expiry>, BOOST_MULTI_INDEX_MEMBER(transaction_id_with_expiry, fc::time_point, expiry)>>>;

namespace {

// track multiple failures on unapplied transactions
class account_failures {
public:
   account_failures() = default;

   void set_max_failures_per_account(uint32_t max_failures, uint32_t size) {
      max_failures_per_account        = max_failures;
      reset_window_size_in_num_blocks = size;
   }

   void add(const account_name& n, const fc::exception& e) {
      auto& fa = failed_accounts[n];
      ++fa.num_failures;
      fa.add(n, e);
   }

   // return true if exceeds max_failures_per_account and should be dropped
   bool failure_limit(const account_name& n) {
      auto fitr = failed_accounts.find(n);
      if (fitr != failed_accounts.end() && fitr->second.num_failures >= max_failures_per_account) {
         ++fitr->second.num_failures;
         return true;
      }
      return false;
   }

   void report_and_clear(uint32_t block_num, const chain::subjective_billing& sub_bill) {
      if (last_reset_block_num != block_num && (block_num % reset_window_size_in_num_blocks == 0)) {
         report(block_num, sub_bill);
         failed_accounts.clear();
         last_reset_block_num = block_num;
      }
   }

   fc::time_point next_reset_timepoint(uint32_t current_block_num, fc::time_point current_block_time) const {
      auto num_blocks_to_reset = reset_window_size_in_num_blocks - (current_block_num % reset_window_size_in_num_blocks);
      return current_block_time + fc::milliseconds(num_blocks_to_reset * eosio::chain::config::block_interval_ms);
   }

private:
   void report(uint32_t block_num, const chain::subjective_billing& sub_bill) const {
      if (_log.is_enabled(fc::log_level::debug)) {
         auto now = fc::time_point::now();
         for (const auto& e : failed_accounts) {
            std::string reason;
            if (e.second.is_deadline())
               reason += "deadline";
            if (e.second.is_tx_cpu_usage()) {
               if (!reason.empty())
                  reason += ", ";
               reason += "tx_cpu_usage";
            }
            if (e.second.is_eosio_assert()) {
               if (!reason.empty())
                  reason += ", ";
               reason += "assert";
            }
            if (e.second.is_other()) {
               if (!reason.empty())
                  reason += ", ";
               reason += "other";
            }
            fc_dlog(_log, "Failed ${n} trxs, account: ${a}, sub bill: ${b}us, reason: ${r}",
                    ("n", e.second.num_failures)("b", sub_bill.get_subjective_bill(e.first, now))("a", e.first)("r", reason));
         }
      }
   }
   struct account_failure {
      enum class ex_fields : uint8_t {
         ex_deadline_exception     = 1,
         ex_tx_cpu_usage_exceeded  = 2,
         ex_eosio_assert_exception = 4,
         ex_other_exception        = 8
      };

      void add(const account_name& n, const fc::exception& e) {
         auto exception_code = e.code();
         if (exception_code == tx_cpu_usage_exceeded::code_value) {
            ex_flags = set_field(ex_flags, ex_fields::ex_tx_cpu_usage_exceeded);
         } else if (exception_code == deadline_exception::code_value) {
            ex_flags = set_field(ex_flags, ex_fields::ex_deadline_exception);
         } else if (exception_code == eosio_assert_message_exception::code_value ||
                    exception_code == eosio_assert_code_exception::code_value) {
            ex_flags = set_field(ex_flags, ex_fields::ex_eosio_assert_exception);
         } else {
            ex_flags = set_field(ex_flags, ex_fields::ex_other_exception);
            fc_dlog(_log, "Failed trx, account: ${a}, reason: ${r}, except: ${e}", ("a", n)("r", exception_code)("e", e));
         }
      }

      bool is_deadline() const { return has_field(ex_flags, ex_fields::ex_deadline_exception); }
      bool is_tx_cpu_usage() const { return has_field(ex_flags, ex_fields::ex_tx_cpu_usage_exceeded); }
      bool is_eosio_assert() const { return has_field(ex_flags, ex_fields::ex_eosio_assert_exception); }
      bool is_other() const { return has_field(ex_flags, ex_fields::ex_other_exception); }

      uint32_t num_failures = 0;
      uint8_t  ex_flags     = 0;
   };

   std::map<account_name, account_failure> failed_accounts;
   uint32_t                                max_failures_per_account        = 3;
   uint32_t                                last_reset_block_num            = 0;
   uint32_t                                reset_window_size_in_num_blocks = 1;
};

struct block_time_tracker {

   struct trx_time_tracker {
      enum class time_status { success, fail, other };

      trx_time_tracker(block_time_tracker& btt, bool transient)
          : _block_time_tracker(btt), _is_transient(transient) {}

      trx_time_tracker(trx_time_tracker&&) = default;

      trx_time_tracker() = delete;
      trx_time_tracker(const trx_time_tracker&) = delete;
      trx_time_tracker& operator=(const trx_time_tracker&) = delete;
      trx_time_tracker& operator=(trx_time_tracker&&) = delete;

      void trx_success() { _time_status = time_status::success; }

      // Neither success nor fail, will be reported as other
      void cancel() { _time_status = time_status::other; }

      // updates block_time_tracker
      ~trx_time_tracker() {
         switch (_time_status) {
         case time_status::success:
            _block_time_tracker.add_success_time(_is_transient);
            break;
         case time_status::fail:
            _block_time_tracker.add_fail_time(_is_transient);
            break;
         case time_status::other:
            _block_time_tracker.add_other_time();
            break;
         }
      }

    private:
      block_time_tracker& _block_time_tracker;
      time_status _time_status = time_status::fail;
      bool _is_transient;
   };

   trx_time_tracker start_trx(bool is_transient, fc::time_point now = fc::time_point::now()) {
      assert(!paused);
      add_other_time(now);
      return {*this, is_transient};
   }

   void add_other_time(fc::time_point now = fc::time_point::now()) {
      assert(!paused);
      other_time += now - last_time_point;
      last_time_point = now;
   }

   fc::microseconds add_idle_time(fc::time_point now = fc::time_point::now()) {
      assert(!paused);
      auto dur = now - last_time_point;
      block_idle_time += dur;
      last_time_point = now; // guard against calling add_idle_time() twice in a row.
      return dur;
   }

   // assumes idle time before pause
   void pause(fc::time_point now = fc::time_point::now()) {
      assert(!paused);
      add_idle_time(now);
      paused = true;
   }

   // assumes last call was to pause
   void unpause(fc::time_point now = fc::time_point::now()) {
      assert(paused);
      paused = false;
      auto pause_time = now - last_time_point;
      clear_time_point += pause_time;
      last_time_point = now;
   }

   void report(uint32_t block_num, account_name producer, producer_plugin::speculative_block_metrics& metrics) {
      using namespace std::string_literals;
      assert(!paused);
      auto now = fc::time_point::now();
      if( _log.is_enabled( fc::log_level::debug ) ) {
         auto diff = now - clear_time_point - block_idle_time - trx_success_time - trx_fail_time - transient_trx_time - other_time;
         fc_dlog( _log, "Block #${n} ${p} trx idle: ${i}us out of ${t}us, success: ${sn}, ${s}us, fail: ${fn}, ${f}us, "
                  "transient: ${ttn}, ${tt}us, other: ${o}us${rest}",
                  ("n", block_num)("p", producer)
                  ("i", block_idle_time)("t", now - clear_time_point)("sn", trx_success_num)("s", trx_success_time)
                  ("fn", trx_fail_num)("f", trx_fail_time)
                  ("ttn", transient_trx_num)("tt", transient_trx_time)
                  ("o", other_time)("rest", diff.count() > 5 ? ", diff: "s + std::to_string(diff.count()) + "us"s : ""s ) );
      }
      metrics.block_producer = producer;
      metrics.block_num = block_num;
      metrics.block_total_time_us = (now - clear_time_point).count();
      metrics.block_idle_us = block_idle_time.count();
      metrics.num_success_trx = trx_success_num;
      metrics.success_trx_time_us = trx_success_time.count();
      metrics.num_fail_trx = trx_fail_num;
      metrics.fail_trx_time_us = trx_fail_time.count();
      metrics.num_transient_trx = transient_trx_num;
      metrics.transient_trx_time_us = transient_trx_time.count();
      metrics.block_other_time_us = other_time.count();
   }

   void clear() {
      assert(!paused);
      block_idle_time = trx_fail_time = trx_success_time = transient_trx_time = other_time = fc::microseconds{};
      trx_fail_num = trx_success_num = transient_trx_num = 0;
      clear_time_point = last_time_point = fc::time_point::now();
   }

 private:
   void add_success_time(bool is_transient) {
      assert(!paused);
      auto now = fc::time_point::now();
      if( is_transient ) {
         // transient time includes both success and fail time
         transient_trx_time += now - last_time_point;
         ++transient_trx_num;
      } else {
         trx_success_time += now - last_time_point;
         ++trx_success_num;
      }
      last_time_point = now;
   }

   void add_fail_time(bool is_transient) {
      assert(!paused);
      auto now = fc::time_point::now();
      if( is_transient ) {
         // transient time includes both success and fail time
         transient_trx_time += now - last_time_point;
         ++transient_trx_num;
      } else {
         trx_fail_time += now - last_time_point;
         ++trx_fail_num;
      }
      last_time_point = now;
   }

 private:
   fc::microseconds block_idle_time;
   uint32_t         trx_success_num   = 0;
   uint32_t         trx_fail_num      = 0;
   uint32_t         transient_trx_num = 0;
   fc::microseconds trx_success_time;
   fc::microseconds trx_fail_time;
   fc::microseconds transient_trx_time;
   fc::microseconds other_time;
   fc::time_point last_time_point{fc::time_point::now()};
   fc::time_point clear_time_point{fc::time_point::now()};
   bool paused = false;
};

} // anonymous namespace

class producer_plugin_impl : public std::enable_shared_from_this<producer_plugin_impl> {
public:
   producer_plugin_impl(boost::asio::io_service& io)
      : _timer(io)
      , _transaction_ack_channel(app().get_channel<compat::channels::transaction_ack>())
      , _ro_timer(io) {}

   void     schedule_production_loop();
   void     schedule_maybe_produce_block(bool exhausted);
   void     produce_block();
   bool     maybe_produce_block();
   bool     block_is_exhausted() const;
   bool     remove_expired_trxs(const fc::time_point& deadline);
   bool     remove_expired_blacklisted_trxs(const fc::time_point& deadline);
   bool     process_unapplied_trxs(const fc::time_point& deadline);
   void     process_scheduled_and_incoming_trxs(const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr);
   bool     process_incoming_trxs(const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr);

   struct push_result {
      bool block_exhausted = false;
      bool trx_exhausted   = false;
      bool failed          = false;
   };
   push_result push_transaction(const fc::time_point&                       block_deadline,
                                const transaction_metadata_ptr&             trx,
                                bool                                        api_trx,
                                bool                                        return_failure_trace,
                                block_time_tracker::trx_time_tracker&       trx_tracker,
                                const next_function<transaction_trace_ptr>& next);
   push_result handle_push_result(const transaction_metadata_ptr&             trx,
                                  const next_function<transaction_trace_ptr>& next,
                                  const fc::time_point&                       start,
                                  chain::controller&                          chain,
                                  const transaction_trace_ptr&                trace,
                                  bool                                        return_failure_trace,
                                  bool                                        disable_subjective_enforcement,
                                  account_name                                first_auth,
                                  int64_t                                     sub_bill,
                                  uint32_t                                    prev_billed_cpu_time_us);
   
   void        log_trx_results(const transaction_metadata_ptr& trx, const transaction_trace_ptr& trace, const fc::time_point& start);
   void        log_trx_results(const transaction_metadata_ptr& trx, const fc::exception_ptr& except_ptr);
   void        log_trx_results(const packed_transaction_ptr& trx,
                               const transaction_trace_ptr&  trace,
                               const fc::exception_ptr&      except_ptr,
                               uint32_t                      billed_cpu_us,
                               const fc::time_point&         start,
                               bool                          is_transient);
   
   void        add_greylist_accounts(const producer_plugin::greylist_params& params) {
      EOS_ASSERT(params.accounts.size() > 0, chain::invalid_http_request, "At least one account is required");

      chain::controller& chain = chain_plug->chain();
      for (auto& acc : params.accounts) {
         chain.add_resource_greylist(acc);
      }
   }

   void remove_greylist_accounts(const producer_plugin::greylist_params& params) {
      EOS_ASSERT(params.accounts.size() > 0, chain::invalid_http_request, "At least one account is required");

      chain::controller& chain = chain_plug->chain();
      for (auto& acc : params.accounts) {
         chain.remove_resource_greylist(acc);
      }
   }

   producer_plugin::greylist_params get_greylist() const {
      chain::controller&               chain = chain_plug->chain();
      producer_plugin::greylist_params result;
      const auto&                      list = chain.get_resource_greylist();
      result.accounts.reserve(list.size());
      for (auto& acc : list) {
         result.accounts.push_back(acc);
      }
      return result;
   }

   producer_plugin::integrity_hash_information get_integrity_hash() {
      chain::controller& chain = chain_plug->chain();

      auto reschedule = fc::make_scoped_exit([this]() { schedule_production_loop(); });

      if (chain.is_building_block()) {
         // abort the pending block
         abort_block();
      } else {
         reschedule.cancel();
      }

      return {chain.head_block_id(), chain.calculate_integrity_hash()};
   }

   void create_snapshot(producer_plugin::next_function<chain::snapshot_scheduler::snapshot_information> next) {
      chain::controller& chain = chain_plug->chain();

      auto reschedule = fc::make_scoped_exit([this]() { schedule_production_loop(); });

      auto predicate = [&]() -> void {
         if (chain.is_building_block()) {
            // abort the pending block
            abort_block();
         } else {
            reschedule.cancel();
         }
      };

      _snapshot_scheduler.create_snapshot(std::move(next), chain, predicate);
   }

   void update_runtime_options(const producer_plugin::runtime_options& options);

   producer_plugin::runtime_options get_runtime_options() const {
      return {_max_transaction_time_ms,
              _max_irreversible_block_age_us.count() < 0 ? -1 : _max_irreversible_block_age_us.count() / 1'000'000,
              _cpu_effort_us,
              _max_scheduled_transaction_time_per_block_ms,
              chain_plug->chain().get_subjective_cpu_leeway() ? chain_plug->chain().get_subjective_cpu_leeway()->count()
                                                              : std::optional<int32_t>(),
              _incoming_defer_ratio,
              chain_plug->chain().get_greylist_limit()};
   }

   void schedule_protocol_feature_activations(const producer_plugin::scheduled_protocol_feature_activations& schedule);

   void plugin_shutdown();
   void plugin_startup();
   void plugin_initialize(const boost::program_options::variables_map& options);

   boost::program_options::variables_map _options;
   bool                                  _production_enabled = false;
   bool                                  _pause_production   = false;

   using signature_provider_type = signature_provider_plugin::signature_provider_type;
   std::map<chain::public_key_type, signature_provider_type> _signature_providers;
   std::set<chain::account_name>                             _producers;
   boost::asio::deadline_timer                               _timer;
   block_timing_util::producer_watermarks            _producer_watermarks;
   pending_block_mode                                _pending_block_mode = pending_block_mode::speculating;
   unapplied_transaction_queue                       _unapplied_transactions;
   size_t                                            _thread_pool_size = config::default_controller_thread_pool_size;
   named_thread_pool<struct prod>                    _thread_pool;
   std::atomic<int32_t>                              _max_transaction_time_ms; // modified by app thread, read by net_plugin thread pool
   std::atomic<uint32_t>                             _received_block{0};       // modified by net_plugin thread pool
   fc::microseconds                                  _max_irreversible_block_age_us;
   int32_t                                           _cpu_effort_us = 0;
   fc::time_point                                    _pending_block_deadline;
   uint32_t                                          _max_block_cpu_usage_threshold_us            = 0;
   uint32_t                                          _max_block_net_usage_threshold_bytes         = 0;
   int32_t                                           _max_scheduled_transaction_time_per_block_ms = 0;
   bool                                              _disable_subjective_p2p_billing              = true;
   bool                                              _disable_subjective_api_billing              = true;
   fc::time_point                                    _irreversible_block_time;
   fc::time_point                                    _idle_trx_time{fc::time_point::now()};

   std::vector<chain::digest_type> _protocol_features_to_activate;
   bool                            _protocol_features_signaled = false; // to mark whether it has been signaled in start_block

   chain_plugin* chain_plug = nullptr;

   compat::channels::transaction_ack::channel_type& _transaction_ack_channel;

   incoming::methods::block_sync::method_type::handle        _incoming_block_sync_provider;
   incoming::methods::transaction_async::method_type::handle _incoming_transaction_async_provider;

   transaction_id_with_expiry_index _blacklisted_transactions;
   account_failures                 _account_fails;
   block_time_tracker               _time_tracker;

   std::optional<scoped_connection> _accepted_block_connection;
   std::optional<scoped_connection> _accepted_block_header_connection;
   std::optional<scoped_connection> _irreversible_block_connection;
   std::optional<scoped_connection> _block_start_connection;

   /*
    * HACK ALERT
    * Boost timers can be in a state where a handler has not yet executed but is not abortable.
    * As this method needs to mutate state handlers depend on for proper functioning to maintain
    * invariants for other code (namely accepting incoming transactions in a nearly full block)
    * the handlers capture a corelation ID at the time they are set.  When they are executed
    * they must check that correlation_id against the global ordinal.  If it does not match that
    * implies that this method has been called with the handler in the state where it should be
    * cancelled but wasn't able to be.
    */
   uint32_t _timer_corelation_id = 0;

   // keep a expected ratio between defer txn and incoming txn
   double _incoming_defer_ratio = 1.0; // 1:1

   // path to write the snapshots to
   std::filesystem::path _snapshots_dir;

   // async snapshot scheduler
   snapshot_scheduler _snapshot_scheduler;

   std::function<void(producer_plugin::produced_block_metrics)> _update_produced_block_metrics;
   std::function<void(producer_plugin::speculative_block_metrics)> _update_speculative_block_metrics;
   std::function<void(producer_plugin::incoming_block_metrics)> _update_incoming_block_metrics;

   // ro for read-only
   struct ro_trx_t {
      transaction_metadata_ptr trx;
      next_func_t              next;
   };
   // The queue storing previously exhausted read-only transactions to be re-executed by read-only threads
   // thread-safe
   class ro_trx_queue_t {
   public:
      void push_front(ro_trx_t&& t) {
         std::lock_guard g(mtx);
         queue.push_front(std::move(t));
      }

      bool empty() const {
         std::lock_guard g(mtx);
         return queue.empty();
      }

      bool pop_front(ro_trx_t& t) {
         std::unique_lock g(mtx);
         if (queue.empty())
            return false;
         t = queue.front();
         queue.pop_front();
         return true;
      }

   private:
      mutable std::mutex mtx;
      deque<ro_trx_t>    queue; // boost deque which is faster than std::deque
   };

   uint32_t _ro_thread_pool_size{0};
   // In EOS VM OC tierup, 10 pages (11 slices) virtual memory is reserved for
   // each read-only thread and 528 pages (529 slices) for the main-thread memory.
   // With maximum 128 read-only threads, virtual memory required by OC is
   // 15TB (OC's main thread uses 4TB VM (by 529 slices) and the read-only
   // threads use 11TB (128 * 11 * 8GB)). It is about 11.7% of total VM space
   // in a 64-bit Linux machine (about 128TB).
   static constexpr uint32_t         _ro_max_threads_allowed{128};
   named_thread_pool<struct read>    _ro_thread_pool;
   fc::microseconds                  _ro_write_window_time_us{200000};
   fc::microseconds                  _ro_read_window_time_us{60000};
   static constexpr fc::microseconds _ro_read_window_minimum_time_us{10000};
   fc::microseconds                  _ro_read_window_effective_time_us{0}; // calculated during option initialization
   std::atomic<int64_t>              _ro_all_threads_exec_time_us; // total time spent by all threads executing transactions.
                                                                   // use atomic for simplicity and performance
   fc::time_point                 _ro_read_window_start_time;
   fc::time_point                 _ro_window_deadline;    // only modified on app thread, read-window deadline or write-window deadline
   boost::asio::deadline_timer    _ro_timer;              // only accessible from the main thread
   fc::microseconds               _ro_max_trx_time_us{0}; // calculated during option initialization
   ro_trx_queue_t                 _ro_exhausted_trx_queue;
   std::atomic<uint32_t>          _ro_num_active_exec_tasks{0};
   std::vector<std::future<bool>> _ro_exec_tasks_fut;

   void start_write_window();
   void switch_to_write_window();
   void switch_to_read_window();
   bool read_only_execution_task(uint32_t pending_block_num);
   void repost_exhausted_transactions(const fc::time_point& deadline);
   bool push_read_only_transaction(transaction_metadata_ptr trx, next_function<transaction_trace_ptr> next);

   void on_block(const block_state_ptr& bsp) {
      auto& chain  = chain_plug->chain();
      auto  before = _unapplied_transactions.size();
      _unapplied_transactions.clear_applied(bsp);
      chain.get_mutable_subjective_billing().on_block(_log, bsp, fc::time_point::now());
      if (before > 0) {
         fc_dlog(_log, "Removed applied transactions before: ${before}, after: ${after}", ("before", before)("after", _unapplied_transactions.size()));
      }
   }

   void on_block_header(const block_state_ptr& bsp) {
      if (_producers.contains(bsp->header.producer))
         _producer_watermarks.consider_new_watermark(bsp->header.producer, bsp->block_num, bsp->block->timestamp);
   }

   void on_irreversible_block(const signed_block_ptr& lib) {
      const chain::controller& chain = chain_plug->chain();
      EOS_ASSERT(chain.is_write_window(), producer_exception, "write window is expected for on_irreversible_block signal");
      _irreversible_block_time = lib->timestamp.to_time_point();
      _snapshot_scheduler.on_irreversible_block(lib, chain);
   }

   void abort_block() {
      auto& chain = chain_plug->chain();

      std::optional<std::tuple<uint32_t, account_name>> block_info;
      if( chain.is_building_block() ) {
         block_info = std::make_tuple(chain.pending_block_num(), chain.pending_block_producer());
      }
      _unapplied_transactions.add_aborted( chain.abort_block() );
      _time_tracker.add_other_time();

      if (block_info) {
         auto[block_num, block_producer] = *block_info;
         producer_plugin::speculative_block_metrics metrics;
         _time_tracker.report(block_num, block_producer, metrics);
         if (_update_speculative_block_metrics)
            _update_speculative_block_metrics(metrics);
      }
      _time_tracker.clear();
   }

   bool on_incoming_block(const signed_block_ptr& block, const std::optional<block_id_type>& block_id, const block_state_ptr& bsp) {
      auto& chain = chain_plug->chain();
      if (in_producing_mode()) {
         fc_wlog(_log, "dropped incoming block #${num} id: ${id}", ("num", block->block_num())("id", block_id ? (*block_id).str() : "UNKNOWN"));
         return false;
      }

      // start a new speculative block, speculative start_block may have been interrupted
      auto ensure = fc::make_scoped_exit([this]() { schedule_production_loop(); });

      auto now = fc::time_point::now();
      const auto& id      = block_id ? *block_id : block->calculate_id();
      auto        blk_num = block->block_num();

      if (now - block->timestamp < fc::minutes(5) || (blk_num % 1000 == 0)) // only log every 1000 during sync
         fc_dlog(_log, "received incoming block ${n} ${id}", ("n", blk_num)("id", id));

      _time_tracker.add_idle_time(now);

      EOS_ASSERT(block->timestamp < (now + fc::seconds(7)), block_from_the_future, "received a block from the future, ignoring it: ${id}", ("id", id));

      /* de-dupe here... no point in aborting block if we already know the block */
      auto existing = chain.fetch_block_by_id(id);
      if (existing) {
         return true; // return true because the block is valid
      } 

      // start processing of block
      std::future<block_state_ptr> bsf;
      if (!bsp) {
         bsf = chain.create_block_state_future(id, block);
      }

      // abort the pending block
      abort_block();

      // push the new block
      auto handle_error = [&](const auto& e) {
         elog("Exception on block ${bn}: ${e}", ("bn", blk_num)("e", e.to_detail_string()));
         app().get_channel<channels::rejected_block>().publish(priority::medium, block);
         throw;
      };

      controller::block_report br;
      try {
         const block_state_ptr& bspr = bsp ? bsp : bsf.get();
         chain.push_block(
            br,
            bspr,
            [this](const branch_type& forked_branch) { _unapplied_transactions.add_forked(forked_branch); },
            [this](const transaction_id_type& id) { return _unapplied_transactions.get_trx(id); });
      } catch (const guard_exception& e) {
         chain_plugin::handle_guard_exception(e);
         return false;
      } catch (const std::bad_alloc&) {
         chain_apis::api_base::handle_bad_alloc();
      } catch (boost::interprocess::bad_alloc&) {
         chain_apis::api_base::handle_db_exhaustion();
      } catch (const fork_database_exception& e) {
         elog("Cannot recover from ${e}. Shutting down.", ("e", e.to_detail_string()));
         appbase::app().quit();
         return false;
      } catch (const fc::exception& e) {
         handle_error(e);
      } catch (const std::exception& e) {
         handle_error(fc::std_exception_wrapper::from_current_exception(e));
      }

      const auto& hbs = chain.head_block_state();
      now             = fc::time_point::now();
      if (hbs->header.timestamp.next().to_time_point() >= now) {
         _production_enabled = true;
      }

      if (now - block->timestamp < fc::minutes(5) || (blk_num % 1000 == 0)) {
         ilog("Received block ${id}... #${n} @ ${t} signed by ${p} "
              "[trxs: ${count}, lib: ${lib}, confirmed: ${confs}, net: ${net}, cpu: ${cpu}, elapsed: ${elapsed}, time: ${time}, latency: "
              "${latency} ms]",
              ("p", block->producer)("id", id.str().substr(8, 16))("n", blk_num)("t", block->timestamp)
              ("count", block->transactions.size())("lib", chain.last_irreversible_block_num())
              ("confs", block->confirmed)("net", br.total_net_usage)("cpu", br.total_cpu_usage_us)
              ("elapsed", br.total_elapsed_time)("time", br.total_time)("latency", (now - block->timestamp).count() / 1000));
         if (chain.get_read_mode() != db_read_mode::IRREVERSIBLE && hbs->id != id && hbs->block != nullptr) { // not applied to head
            ilog("Block not applied to head ${id}... #${n} @ ${t} signed by ${p} "
                 "[trxs: ${count}, dpos: ${dpos}, confirmed: ${confs}, net: ${net}, cpu: ${cpu}, elapsed: ${elapsed}, time: ${time}, "
                 "latency: ${latency} ms]",
                 ("p", hbs->block->producer)("id", hbs->id.str().substr(8, 16))("n", hbs->block_num)("t", hbs->block->timestamp)
                 ("count", hbs->block->transactions.size())("dpos", hbs->dpos_irreversible_blocknum)("confs", hbs->block->confirmed)
                 ("net", br.total_net_usage)("cpu", br.total_cpu_usage_us)("elapsed", br.total_elapsed_time)("time", br.total_time)
                 ("latency", (now - hbs->block->timestamp).count() / 1000));
         }
      }
      if (_update_incoming_block_metrics) {
         _update_incoming_block_metrics({.trxs_incoming_total   = block->transactions.size(),
                                         .cpu_usage_us          = br.total_cpu_usage_us,
                                         .total_elapsed_time_us = br.total_elapsed_time.count(),
                                         .total_time_us         = br.total_time.count(),
                                         .net_usage_us          = br.total_net_usage,
                                         .block_latency_us      = (now - block->timestamp).count(),
                                         .last_irreversible     = chain.last_irreversible_block_num(),
                                         .head_block_num        = blk_num});
      }

      return true;
   }

   void restart_speculative_block() {
      // abort the pending block
      abort_block();

      schedule_production_loop();
   }

   void on_incoming_transaction_async(const packed_transaction_ptr&        trx,
                                      bool                                 api_trx,
                                      transaction_metadata::trx_type       trx_type,
                                      bool                                 return_failure_traces,
                                      next_function<transaction_trace_ptr> next) {
      if (trx_type == transaction_metadata::trx_type::read_only) {
         EOS_ASSERT( _ro_thread_pool_size > 0, unsupported_feature,
                     "read-only transactions execution not enabled on API node. Set read-only-threads > 0" );

         // Post all read only trxs to read_only queue for execution.
         auto trx_metadata = transaction_metadata::create_no_recover_keys(trx, transaction_metadata::trx_type::read_only);
         app().executor().post(priority::low, exec_queue::read_exclusive, [this, trx{std::move(trx_metadata)}, next{std::move(next)}]() mutable {
            push_read_only_transaction(std::move(trx), std::move(next));
         });
         return;
      }

      chain::controller& chain             = chain_plug->chain();
      const auto         max_trx_time_ms   = (trx_type == transaction_metadata::trx_type::read_only) ? -1 : _max_transaction_time_ms.load();
      fc::microseconds   max_trx_cpu_usage = max_trx_time_ms < 0 ? fc::microseconds::maximum() : fc::milliseconds(max_trx_time_ms);

      auto future = transaction_metadata::start_recover_keys(trx,
                                                             _thread_pool.get_executor(),
                                                             chain.get_chain_id(),
                                                             fc::microseconds(max_trx_cpu_usage),
                                                             trx_type,
                                                             chain.configured_subjective_signature_length_limit());

      auto is_transient = (trx_type == transaction_metadata::trx_type::read_only || trx_type == transaction_metadata::trx_type::dry_run);
      if (!is_transient) {
         next = [this, trx, next{std::move(next)}](const next_function_variant<transaction_trace_ptr>& response) {
            next(response);

            fc::exception_ptr except_ptr; // rejected
            if (std::holds_alternative<fc::exception_ptr>(response)) {
               except_ptr = std::get<fc::exception_ptr>(response);
            } else if (std::get<transaction_trace_ptr>(response)->except) {
               except_ptr = std::get<transaction_trace_ptr>(response)->except->dynamic_copy_exception();
            }

            _transaction_ack_channel.publish(priority::low, std::pair<fc::exception_ptr, packed_transaction_ptr>(except_ptr, trx));
         };
      }

      boost::asio::post(_thread_pool.get_executor(),
                        [self = this, future{std::move(future)}, api_trx, is_transient, return_failure_traces,
                         next{std::move(next)}, trx = trx]() mutable {
                           if (future.valid()) {
                              future.wait();
                              app().executor().post(priority::low, exec_queue::read_write,
                                 [self, future{std::move(future)}, api_trx, is_transient, next{std::move(next)}, trx{std::move(trx)},
                                  return_failure_traces]() mutable {
                                    auto start       = fc::time_point::now();
                                    auto idle_time   = self->_time_tracker.add_idle_time(start);
                                    auto trx_tracker = self->_time_tracker.start_trx(is_transient, start);
                                    fc_tlog(_log, "Time since last trx: ${t}us", ("t", idle_time));

                                    auto exception_handler =
                                       [self, is_transient, &next, trx{std::move(trx)}, &start](fc::exception_ptr ex) {
                                          self->log_trx_results(trx, nullptr, ex, 0, start, is_transient);
                                          next(std::move(ex));
                                       };
                                    try {
                                       auto result = future.get();
                                       if (!self->process_incoming_transaction_async(result, api_trx, return_failure_traces, trx_tracker, next)) {
                                          if (self->in_producing_mode()) {
                                             self->schedule_maybe_produce_block(true);
                                          } else {
                                             self->restart_speculative_block();
                                          }
                                       }
                                    }
                                    CATCH_AND_CALL(exception_handler);
                                 });
                           }
                        });
   }

   bool process_incoming_transaction_async(const transaction_metadata_ptr&             trx,
                                           bool                                        api_trx,
                                           bool                                        return_failure_trace,
                                           block_time_tracker::trx_time_tracker&       trx_tracker,
                                           const next_function<transaction_trace_ptr>& next) {
      bool               exhausted = false;
      chain::controller& chain     = chain_plug->chain();
      try {
         const auto& id = trx->id();

         fc::time_point       bt     = chain.is_building_block() ? chain.pending_block_time() : chain.head_block_time();
         const fc::time_point expire = trx->packed_trx()->expiration().to_time_point();
         if (expire < bt) {
            auto except_ptr = std::static_pointer_cast<fc::exception>(std::make_shared<expired_tx_exception>(
               FC_LOG_MESSAGE(error, "expired transaction ${id}, expiration ${e}, block time ${bt}", ("id", id)("e", expire)("bt", bt))));
            log_trx_results(trx, except_ptr);
            next(std::move(except_ptr));
            return true;
         }

         if (chain.is_known_unexpired_transaction(id)) {
            auto except_ptr = std::static_pointer_cast<fc::exception>(
               std::make_shared<tx_duplicate>(FC_LOG_MESSAGE(error, "duplicate transaction ${id}", ("id", id))));
            next(std::move(except_ptr));
            return true;
         }

         if (!chain.is_building_block()) {
            _unapplied_transactions.add_incoming(trx, api_trx, return_failure_trace, next);
            trx_tracker.cancel();
            return true;
         }

         const auto  block_deadline = _pending_block_deadline;
         push_result pr             = push_transaction(block_deadline, trx, api_trx, return_failure_trace, trx_tracker, next);

         if (pr.trx_exhausted) {
            _unapplied_transactions.add_incoming(trx, api_trx, return_failure_trace, next);
         }

         exhausted = pr.block_exhausted;
         
         if ( !in_producing_mode() && pr.trx_exhausted )
            exhausted = true;  // report transaction exhausted if trx was exhausted in non-producing mode (so we will restart
                               // a speculative block to retry it immediately, instead of waiting to receive a new block)
         
      } catch (const guard_exception& e) {
         chain_plugin::handle_guard_exception(e);
      } catch (boost::interprocess::bad_alloc&) {
         chain_apis::api_base::handle_db_exhaustion();
      } catch (std::bad_alloc&) {
         chain_apis::api_base::handle_bad_alloc();
      }
      CATCH_AND_CALL(next);

      return !exhausted;
   }


   fc::microseconds get_irreversible_block_age() {
      auto now = fc::time_point::now();
      if (now < _irreversible_block_time) {
         return fc::microseconds(0);
      } else {
         return now - _irreversible_block_time;
      }
   }

   account_name get_pending_block_producer() {
      auto& chain = chain_plug->chain();
      if (chain.is_building_block()) {
         return chain.pending_block_producer();
      } else {
         return {};
      }
   }

   bool production_disabled_by_policy() {
      return !_production_enabled || _pause_production ||
             (_max_irreversible_block_age_us.count() >= 0 && get_irreversible_block_age() >= _max_irreversible_block_age_us);
   }

   bool is_producer_key(const chain::public_key_type& key) const {
      return _signature_providers.find(key) != _signature_providers.end();
   }

   chain::signature_type sign_compact(const chain::public_key_type& key, const fc::sha256& digest) const {
      if (key != chain::public_key_type()) {
         auto private_key_itr = _signature_providers.find(key);
         EOS_ASSERT(private_key_itr != _signature_providers.end(), producer_priv_key_not_found,
                    "Local producer has no private key in config.ini corresponding to public key ${key}", ("key", key));

         return private_key_itr->second(digest);
      } else {
         return chain::signature_type();
      }
   }

   void resume() {
      _pause_production = false;
      // it is possible that we are only speculating because of this policy which we have now changed
      // re-evaluate that now
      //
      if (in_speculating_mode()) {
         abort_block();
         fc_ilog(_log, "Producer resumed. Scheduling production.");
         schedule_production_loop();
      } else {
         fc_ilog(_log, "Producer resumed.");
      }
   }

   enum class start_block_result {
      succeeded,
      failed,
      waiting_for_block,
      waiting_for_production,
      exhausted
   };

   inline bool should_interrupt_start_block( const fc::time_point& deadline, uint32_t pending_block_num ) const;
   start_block_result start_block();

   block_timestamp_type calculate_pending_block_time() const;
   void schedule_delayed_production_loop(const std::weak_ptr<producer_plugin_impl>& weak_this, std::optional<fc::time_point> wake_up_time);

   bool in_producing_mode()   const { return _pending_block_mode == pending_block_mode::producing; }
   bool in_speculating_mode() const { return _pending_block_mode == pending_block_mode::speculating; }
};

void new_chain_banner(const eosio::chain::controller& db)
{
   std::cerr << "\n"
      "*******************************\n"
      "*                             *\n"
      "*   ------ NEW CHAIN ------   *\n"
      "*   - Welcome to Antelope -   *\n"
      "*   -----------------------   *\n"
      "*                             *\n"
      "*******************************\n"
      "\n";

   if( db.head_block_state()->header.timestamp.to_time_point() < (fc::time_point::now() - fc::milliseconds(200 * config::block_interval_ms)))
   {
      std::cerr << "Your genesis seems to have an old timestamp\n"
         "Please consider using the --genesis-timestamp option to give your genesis a recent timestamp\n"
         "\n"
         ;
   }
   return;
}

producer_plugin::producer_plugin()
   : my(new producer_plugin_impl(app().get_io_service()))
   {
   }

producer_plugin::~producer_plugin() {}

void producer_plugin::set_program_options(
   boost::program_options::options_description& command_line_options,
   boost::program_options::options_description& config_file_options)
{
   auto default_priv_key = private_key_type::regenerate<fc::ecc::private_key_shim>(fc::sha256::hash(std::string("nathan")));
   auto private_key_default = std::make_pair(default_priv_key.get_public_key(), default_priv_key );

   boost::program_options::options_description producer_options;

   producer_options.add_options()
         ("enable-stale-production,e", boost::program_options::bool_switch()->notifier([this](bool e){my->_production_enabled = e;}), "Enable block production, even if the chain is stale.")
         ("pause-on-startup,x", boost::program_options::bool_switch()->notifier([this](bool p){my->_pause_production = p;}), "Start this node in a state where production is paused")
         ("max-transaction-time", bpo::value<int32_t>()->default_value(config::block_interval_ms-1),
          "Locally lowers the max_transaction_cpu_usage limit (in milliseconds) that an input transaction is allowed to execute before being considered invalid")
         ("max-irreversible-block-age", bpo::value<int32_t>()->default_value( -1 ),
          "Limits the maximum age (in seconds) of the DPOS Irreversible Block for a chain this node will produce blocks on (use negative value to indicate unlimited)")
         ("producer-name,p", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "ID of producer controlled by this node (e.g. inita; may specify multiple times)")
         ("signature-provider", boost::program_options::value<vector<string>>()->composing()->multitoken()->default_value(
               {default_priv_key.get_public_key().to_string({}) + "=KEY:" + default_priv_key.to_string({})},
                default_priv_key.get_public_key().to_string({}) + "=KEY:" + default_priv_key.to_string({})),
               app().get_plugin<signature_provider_plugin>().signature_provider_help_text())
         ("greylist-account", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "account that can not access to extended CPU/NET virtual resources")
         ("greylist-limit", boost::program_options::value<uint32_t>()->default_value(1000),
          "Limit (between 1 and 1000) on the multiple that CPU/NET virtual resources can extend during low usage (only enforced subjectively; use 1000 to not enforce any limit)")
         ("cpu-effort-percent", bpo::value<uint32_t>()->default_value(config::default_block_cpu_effort_pct / config::percent_1),
          "Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80%")
         ("max-block-cpu-usage-threshold-us", bpo::value<uint32_t>()->default_value( 5000 ),
          "Threshold of CPU block production to consider block full; when within threshold of max-block-cpu-usage block can be produced immediately")
         ("max-block-net-usage-threshold-bytes", bpo::value<uint32_t>()->default_value( 1024 ),
          "Threshold of NET block production to consider block full; when within threshold of max-block-net-usage block can be produced immediately")
         ("max-scheduled-transaction-time-per-block-ms", boost::program_options::value<int32_t>()->default_value(100),
          "Maximum wall-clock time, in milliseconds, spent retiring scheduled transactions (and incoming transactions according to incoming-defer-ratio) in any block before returning to normal transaction processing.")
         ("subjective-cpu-leeway-us", boost::program_options::value<int32_t>()->default_value( config::default_subjective_cpu_leeway_us ),
          "Time in microseconds allowed for a transaction that starts with insufficient CPU quota to complete and cover its CPU usage.")
         ("subjective-account-max-failures", boost::program_options::value<uint32_t>()->default_value(3),
          "Sets the maximum amount of failures that are allowed for a given account per window size.")
         ("subjective-account-max-failures-window-size", boost::program_options::value<uint32_t>()->default_value(1),
          "Sets the window size in number of blocks for subjective-account-max-failures.")
         ("subjective-account-decay-time-minutes", bpo::value<uint32_t>()->default_value( config::account_cpu_usage_average_window_ms / 1000 / 60 ),
          "Sets the time to return full subjective cpu for accounts")
         ("incoming-defer-ratio", bpo::value<double>()->default_value(1.0),
          "ratio between incoming transactions and deferred transactions when both are queued for execution")
         ("incoming-transaction-queue-size-mb", bpo::value<uint16_t>()->default_value( 1024 ),
          "Maximum size (in MiB) of the incoming transaction queue. Exceeding this value will subjectively drop transaction with resource exhaustion.")
         ("disable-subjective-account-billing", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Account which is excluded from subjective CPU billing")
         ("disable-subjective-p2p-billing", bpo::value<bool>()->default_value(true),
          "Disable subjective CPU billing for P2P transactions")
         ("disable-subjective-api-billing", bpo::value<bool>()->default_value(true),
          "Disable subjective CPU billing for API transactions")
         ("producer-threads", bpo::value<uint16_t>()->default_value(my->_thread_pool_size),
          "Number of worker threads in producer thread pool")
         ("snapshots-dir", bpo::value<std::filesystem::path>()->default_value("snapshots"),
          "the location of the snapshots directory (absolute path or relative to application data dir)")
         ("read-only-threads", bpo::value<uint32_t>(),
          "Number of worker threads in read-only execution thread pool. Max 8.")
         ("read-only-write-window-time-us", bpo::value<uint32_t>()->default_value(my->_ro_write_window_time_us.count()),
          "Time in microseconds the write window lasts.")
         ("read-only-read-window-time-us", bpo::value<uint32_t>()->default_value(my->_ro_read_window_time_us.count()),
          "Time in microseconds the read window lasts.")
         ;
   config_file_options.add(producer_options);
}

bool producer_plugin::is_producer_key(const chain::public_key_type& key) const
{
   return my->is_producer_key(key);
}

chain::signature_type producer_plugin::sign_compact(const chain::public_key_type& key, const fc::sha256& digest) const
{
   return my->sign_compact(key, digest);
}

template<typename T>
T dejsonify(const string& s) {
   return fc::json::from_string(s).as<T>();
}

#define LOAD_VALUE_SET(options, op_name, container) \
if( options.count(op_name) ) { \
   const std::vector<std::string>& ops = options[op_name].as<std::vector<std::string>>(); \
   for( const auto& v : ops ) { \
      container.emplace( eosio::chain::name( v ) ); \
   } \
}

void producer_plugin_impl::plugin_initialize(const boost::program_options::variables_map& options)
{ 
   chain_plug = app().find_plugin<chain_plugin>();
   EOS_ASSERT(chain_plug, plugin_config_exception, "chain_plugin not found" );
   _options = &options;
   LOAD_VALUE_SET(options, "producer-name", _producers)

   chain::controller& chain = chain_plug->chain();

   chain.set_producer_node(!_producers.empty());

   if (options.count("signature-provider")) {
      const std::vector<std::string> key_spec_pairs = options["signature-provider"].as<std::vector<std::string>>();
      for (const auto& key_spec_pair : key_spec_pairs) {
         try {
            const auto& [pubkey, provider] = app().get_plugin<signature_provider_plugin>().signature_provider_for_specification(key_spec_pair);
            _signature_providers[pubkey] = provider;
         } catch(secure_enclave_exception& e) {
            elog("Error with Secure Enclave signature provider: ${e}; ignoring ${val}", ("e", e.top_message())("val", key_spec_pair));
         } catch (fc::exception& e) {
            elog("Malformed signature provider: \"${val}\": ${e}, ignoring!", ("val", key_spec_pair)("e", e));
         } catch (...) {
            elog("Malformed signature provider: \"${val}\", ignoring!", ("val", key_spec_pair));
         }
      }
   }

   auto subjective_account_max_failures_window_size = options.at("subjective-account-max-failures-window-size").as<uint32_t>();
   EOS_ASSERT(subjective_account_max_failures_window_size > 0, plugin_config_exception,
              "subjective-account-max-failures-window-size ${s} must be greater than 0", ("s", subjective_account_max_failures_window_size));

   _account_fails.set_max_failures_per_account(options.at("subjective-account-max-failures").as<uint32_t>(),
                                               subjective_account_max_failures_window_size);

   uint32_t cpu_effort_pct = options.at("cpu-effort-percent").as<uint32_t>();
   EOS_ASSERT(cpu_effort_pct >= 0 && cpu_effort_pct <= 100, plugin_config_exception,
              "cpu-effort-percent ${pct} must be 0 - 100", ("pct", cpu_effort_pct));
   cpu_effort_pct *= config::percent_1;

   _cpu_effort_us = EOS_PERCENT(config::block_interval_us, cpu_effort_pct);

   _max_block_cpu_usage_threshold_us = options.at("max-block-cpu-usage-threshold-us").as<uint32_t>();
   EOS_ASSERT(_max_block_cpu_usage_threshold_us < config::block_interval_us,
              plugin_config_exception,
              "max-block-cpu-usage-threshold-us ${t} must be 0 .. ${bi}",
              ("bi", config::block_interval_us)("t", _max_block_cpu_usage_threshold_us));

   _max_block_net_usage_threshold_bytes = options.at("max-block-net-usage-threshold-bytes").as<uint32_t>();

   _max_scheduled_transaction_time_per_block_ms = options.at("max-scheduled-transaction-time-per-block-ms").as<int32_t>();

   if (options.at("subjective-cpu-leeway-us").as<int32_t>() != config::default_subjective_cpu_leeway_us) {
      chain.set_subjective_cpu_leeway(fc::microseconds(options.at("subjective-cpu-leeway-us").as<int32_t>()));
   }

   fc::microseconds subjective_account_decay_time = fc::minutes(options.at("subjective-account-decay-time-minutes").as<uint32_t>());
   EOS_ASSERT(subjective_account_decay_time.count() > 0,
              plugin_config_exception,
              "subjective-account-decay-time-minutes ${dt} must be greater than 0",
              ("dt", subjective_account_decay_time.to_seconds() / 60));
   chain.get_mutable_subjective_billing().set_expired_accumulator_average_window(subjective_account_decay_time);

   _max_transaction_time_ms = options.at("max-transaction-time").as<int32_t>();

   _max_irreversible_block_age_us = fc::seconds(options.at("max-irreversible-block-age").as<int32_t>());

   auto max_incoming_transaction_queue_size = options.at("incoming-transaction-queue-size-mb").as<uint16_t>() * 1024 * 1024;

   EOS_ASSERT(max_incoming_transaction_queue_size > 0, plugin_config_exception,
              "incoming-transaction-queue-size-mb ${mb} must be greater than 0", ("mb", max_incoming_transaction_queue_size));

   _unapplied_transactions.set_max_transaction_queue_size(max_incoming_transaction_queue_size);

   _incoming_defer_ratio = options.at("incoming-defer-ratio").as<double>();

   _disable_subjective_p2p_billing = options.at("disable-subjective-p2p-billing").as<bool>();
   _disable_subjective_api_billing = options.at("disable-subjective-api-billing").as<bool>();
   dlog("disable-subjective-p2p-billing: ${p2p}, disable-subjective-api-billing: ${api}",
        ("p2p", _disable_subjective_p2p_billing)("api", _disable_subjective_api_billing));
   if (_disable_subjective_p2p_billing && _disable_subjective_api_billing) {
      chain.get_mutable_subjective_billing().disable();
      ilog("Subjective CPU billing disabled");
   } else if (!_disable_subjective_p2p_billing && !_disable_subjective_api_billing) {
      ilog("Subjective CPU billing enabled");
   } else {
      if (_disable_subjective_p2p_billing)
         ilog("Subjective CPU billing of P2P trxs disabled ");
      if (_disable_subjective_api_billing)
         ilog("Subjective CPU billing of API trxs disabled ");
   }

   _thread_pool_size = options.at("producer-threads").as<uint16_t>();
   EOS_ASSERT(_thread_pool_size > 0, plugin_config_exception, "producer-threads ${num} must be greater than 0", ("num", _thread_pool_size));

   if (options.count("snapshots-dir")) {
      auto sd = options.at("snapshots-dir").as<std::filesystem::path>();
      if (sd.is_relative()) {
         _snapshots_dir = app().data_dir() / sd;
         if (!std::filesystem::exists(_snapshots_dir)) {
            std::filesystem::create_directories(_snapshots_dir);
         }
      } else {
         _snapshots_dir = sd;
      }

      EOS_ASSERT(std::filesystem::is_directory(_snapshots_dir),
                 snapshot_directory_not_found_exception,
                 "No such directory '${dir}'",
                 ("dir", _snapshots_dir));

      if (auto resmon_plugin = app().find_plugin<resource_monitor_plugin>()) {
         resmon_plugin->monitor_directory(_snapshots_dir);
      }
   }

   if (options.count("read-only-threads")) {
      _ro_thread_pool_size = options.at("read-only-threads").as<uint32_t>();
   } else if (_producers.empty()) {
      if (options.count("plugin")) {
         const auto& v = options.at("plugin").as<std::vector<std::string>>();
         auto        i = std::find_if(v.cbegin(), v.cend(), [](const std::string& p) { return p == "eosio::chain_api_plugin"; });
         if (i != v.cend()) {
            // default to 3 threads for non producer nodes running chain_api_plugin if not specified
            _ro_thread_pool_size = 3;
            ilog("chain_api_plugin configured, defaulting read-only-threads to ${t}", ("t", _ro_thread_pool_size));
         }
      }
   }
   EOS_ASSERT(producer_plugin::test_mode_ || _ro_thread_pool_size == 0 || _producers.empty(), plugin_config_exception,
              "read-only-threads not allowed on producer node");

   // only initialize other read-only options when read-only thread pool is enabled
   if (_ro_thread_pool_size > 0) {
      EOS_ASSERT(_ro_thread_pool_size <= _ro_max_threads_allowed,
                 plugin_config_exception,
                 "read-only-threads (${th}) greater than the number of threads allowed (${allowed})",
                 ("th", _ro_thread_pool_size)("allowed", _ro_max_threads_allowed));

      _ro_write_window_time_us = fc::microseconds(options.at("read-only-write-window-time-us").as<uint32_t>());
      _ro_read_window_time_us  = fc::microseconds(options.at("read-only-read-window-time-us").as<uint32_t>());
      EOS_ASSERT(_ro_read_window_time_us > _ro_read_window_minimum_time_us,
                 plugin_config_exception,
                 "read-only-read-window-time-us (${read}) must be at least greater than  ${min} us",
                 ("read", _ro_read_window_time_us)("min", _ro_read_window_minimum_time_us));
      _ro_read_window_effective_time_us = _ro_read_window_time_us - _ro_read_window_minimum_time_us;

      ilog("read-only-write-window-time-us: ${ww} us, read-only-read-window-time-us: ${rw} us, effective read window time to be used: ${w} us",
           ("ww", _ro_write_window_time_us)("rw", _ro_read_window_time_us)("w", _ro_read_window_effective_time_us));
   }

   // Make sure _ro_max_trx_time_us is always set.
   // Make sure a read-only transaction can finish within the read
   // window if scheduled at the very beginning of the window.
   // Add _ro_read_window_minimum_time_us for safety margin.
   if (_max_transaction_time_ms.load() > 0) {
      _ro_max_trx_time_us = fc::milliseconds(_max_transaction_time_ms.load());
   } else {
      // max-transaction-time can be set to negative for unlimited time
      _ro_max_trx_time_us = fc::microseconds::maximum();
   }
   if (_ro_max_trx_time_us > _ro_read_window_effective_time_us) {
      _ro_max_trx_time_us = _ro_read_window_effective_time_us;
   }
   ilog("Read-only max transaction time ${rot}us set to fit in the effective read-only window ${row}us.",
        ("rot", _ro_max_trx_time_us)("row", _ro_read_window_effective_time_us));
   ilog("read-only-threads ${s}, max read-only trx time to be enforced: ${t} us", ("s", _ro_thread_pool_size)("t", _ro_max_trx_time_us));

   _incoming_block_sync_provider = app().get_method<incoming::methods::block_sync>().register_provider(
      [this](const signed_block_ptr& block, const std::optional<block_id_type>& block_id, const block_state_ptr& bsp) {
         return on_incoming_block(block, block_id, bsp);
      });

   _incoming_transaction_async_provider =
      app().get_method<incoming::methods::transaction_async>().register_provider(
         [this](const packed_transaction_ptr& trx, bool api_trx, transaction_metadata::trx_type trx_type,
                bool return_failure_traces, next_function<transaction_trace_ptr> next) -> void {
         return on_incoming_transaction_async(trx, api_trx, trx_type, return_failure_traces, next);
      });

   if (options.count("greylist-account")) {
      std::vector<std::string>         greylist = options["greylist-account"].as<std::vector<std::string>>();
      producer_plugin::greylist_params param;
      for (auto& a : greylist) {
         param.accounts.push_back(account_name(a));
      }
      add_greylist_accounts(param);
   }

   {
      uint32_t greylist_limit = options.at("greylist-limit").as<uint32_t>();
      chain.set_greylist_limit(greylist_limit);
   }

   if (options.count("disable-subjective-account-billing")) {
      std::vector<std::string> accounts = options["disable-subjective-account-billing"].as<std::vector<std::string>>();
      for (const auto& a : accounts) {
         chain.get_mutable_subjective_billing().disable_account(account_name(a));
      }
   }

   _snapshot_scheduler.set_db_path(_snapshots_dir);
   _snapshot_scheduler.set_snapshots_path(_snapshots_dir);
}

void producer_plugin::plugin_initialize(const boost::program_options::variables_map& options) {
   try {
      handle_sighup(); // Sets loggers
      my->plugin_initialize(options);
   }
   FC_LOG_AND_RETHROW()
}

using namespace std::chrono_literals;
void producer_plugin_impl::plugin_startup() {
   try {
      try {
         ilog("producer plugin:  plugin_startup() begin");

         _thread_pool.start(_thread_pool_size, [](const fc::exception& e) {
            fc_elog(_log, "Exception in producer thread pool, exiting: ${e}", ("e", e.to_detail_string()));
            app().quit();
         });


         chain::controller& chain = chain_plug->chain();
         EOS_ASSERT(_producers.empty() || chain.get_read_mode() != chain::db_read_mode::IRREVERSIBLE, plugin_config_exception,
                    "node cannot have any producer-name configured because block production is impossible when read_mode is \"irreversible\"");

         EOS_ASSERT(_producers.empty() || chain.get_validation_mode() == chain::validation_mode::FULL, plugin_config_exception,
                    "node cannot have any producer-name configured because block production is not safe when validation_mode is not \"full\"");

         EOS_ASSERT(_producers.empty() || chain_plug->accept_transactions(), plugin_config_exception,
                    "node cannot have any producer-name configured because no block production is possible with no [api|p2p]-accepted-transactions");

         _accepted_block_connection.emplace(chain.accepted_block.connect([this](const auto& bsp) { on_block(bsp); }));
         _accepted_block_header_connection.emplace(chain.accepted_block_header.connect([this](const auto& bsp) { on_block_header(bsp); }));
         _irreversible_block_connection.emplace(
            chain.irreversible_block.connect([this](const auto& bsp) { on_irreversible_block(bsp->block); }));

         _block_start_connection.emplace(chain.block_start.connect([this, &chain](uint32_t bs) {
            try {
               _snapshot_scheduler.on_start_block(bs, chain);
            } catch (const snapshot_execution_exception& e) {
               fc_elog(_log, "Exception during snapshot execution: ${e}", ("e", e.to_detail_string()));
               app().quit();
            }
         }));

         const auto lib_num = chain.last_irreversible_block_num();
         const auto lib     = chain.fetch_block_by_number(lib_num);
         if (lib) {
            on_irreversible_block(lib);
         } else {
            _irreversible_block_time = fc::time_point::maximum();
         }

         if (!_producers.empty()) {
            ilog("Launching block production for ${n} producers at ${time}.", ("n", _producers.size())("time", fc::time_point::now()));

            if (_production_enabled) {
               if (chain.head_block_num() == 0) {
                  new_chain_banner(chain);
               }
            }
         }

         if (_ro_thread_pool_size > 0) {
            _ro_thread_pool.start(
               _ro_thread_pool_size,
               [](const fc::exception& e) {
                  fc_elog(_log, "Exception in read-only thread pool, exiting: ${e}", ("e", e.to_detail_string()));
                  app().quit();
               },
               [&]() {
                  chain.init_thread_local_data();
               });

            _time_tracker.pause(); // start_write_window assumes time_tracker is paused
            start_write_window();
         }

         schedule_production_loop();

         ilog("producer plugin:  plugin_startup() end");
      } catch (...) {
         // always call plugin_shutdown, even on exception
         plugin_shutdown();
         throw;
      }
   }
   FC_CAPTURE_AND_RETHROW()
}

void producer_plugin::plugin_startup() {
   my->plugin_startup();
}

void producer_plugin_impl::plugin_shutdown() {
   boost::system::error_code ec;
   _timer.cancel(ec);
   _ro_timer.cancel(ec);
   app().executor().stop();
   _ro_thread_pool.stop();
   _thread_pool.stop();
   _unapplied_transactions.clear();

   app().executor().post(0, [me = shared_from_this()]() {}); // keep my pointer alive until queue is drained

   fc_ilog(_log, "exit shutdown");
}

void producer_plugin::plugin_shutdown() {
   my->plugin_shutdown();
}

void producer_plugin::handle_sighup() {
   fc::logger::update(logger_name, _log);
   fc::logger::update(trx_successful_trace_logger_name, _trx_successful_trace_log);
   fc::logger::update(trx_failed_trace_logger_name, _trx_failed_trace_log);
   fc::logger::update(trx_trace_success_logger_name, _trx_trace_success_log);
   fc::logger::update(trx_trace_failure_logger_name, _trx_trace_failure_log);
   fc::logger::update(trx_logger_name, _trx_log);
   fc::logger::update(transient_trx_successful_trace_logger_name, _transient_trx_successful_trace_log);
   fc::logger::update(transient_trx_failed_trace_logger_name, _transient_trx_failed_trace_log);
}

void producer_plugin::pause() {
   fc_ilog(_log, "Producer paused.");
   my->_pause_production = true;
}

void producer_plugin::resume() {
   my->resume();
}

bool producer_plugin::paused() const {
   return my->_pause_production;
}

void producer_plugin_impl::update_runtime_options(const producer_plugin::runtime_options& options) {
   chain::controller& chain             = chain_plug->chain();
   bool               check_speculating = false;

   if (options.max_transaction_time) {
      _max_transaction_time_ms = *options.max_transaction_time;
   }

   if (options.max_irreversible_block_age) {
      _max_irreversible_block_age_us = fc::seconds(*options.max_irreversible_block_age);
      check_speculating              = true;
   }

   if (options.cpu_effort_us) {
      _cpu_effort_us = *options.cpu_effort_us;
   }

   if (options.max_scheduled_transaction_time_per_block_ms) {
      _max_scheduled_transaction_time_per_block_ms = *options.max_scheduled_transaction_time_per_block_ms;
   }

   if (options.incoming_defer_ratio) {
      _incoming_defer_ratio = *options.incoming_defer_ratio;
   }

   if (check_speculating && in_speculating_mode()) {
      abort_block();
      schedule_production_loop();
   }

   if (options.subjective_cpu_leeway_us) {
      chain.set_subjective_cpu_leeway(fc::microseconds(*options.subjective_cpu_leeway_us));
   }

   if (options.greylist_limit) {
      chain.set_greylist_limit(*options.greylist_limit);
   }
}

void producer_plugin::update_runtime_options(const runtime_options& options) {
   my->update_runtime_options(options);
}

producer_plugin::runtime_options producer_plugin::get_runtime_options() const {
   return my->get_runtime_options();
}

void producer_plugin::add_greylist_accounts(const greylist_params& params) {
   my->add_greylist_accounts(params);
}

void producer_plugin::remove_greylist_accounts(const greylist_params& params) {
   my->remove_greylist_accounts(params);
}

producer_plugin::greylist_params producer_plugin::get_greylist() const {
   return my->get_greylist();
}

producer_plugin::whitelist_blacklist producer_plugin::get_whitelist_blacklist() const {
   chain::controller& chain = my->chain_plug->chain();
   return {chain.get_actor_whitelist(),
           chain.get_actor_blacklist(),
           chain.get_contract_whitelist(),
           chain.get_contract_blacklist(),
           chain.get_action_blacklist(),
           chain.get_key_blacklist()};
}

void producer_plugin::set_whitelist_blacklist(const producer_plugin::whitelist_blacklist& params) {
   EOS_ASSERT(params.actor_whitelist || params.actor_blacklist || params.contract_whitelist || params.contract_blacklist ||
                 params.action_blacklist || params.key_blacklist,
              chain::invalid_http_request,
              "At least one of actor_whitelist, actor_blacklist, contract_whitelist, contract_blacklist, action_blacklist, and "
              "key_blacklist is required");

   chain::controller& chain = my->chain_plug->chain();
   if (params.actor_whitelist)
      chain.set_actor_whitelist(*params.actor_whitelist);
   if (params.actor_blacklist)
      chain.set_actor_blacklist(*params.actor_blacklist);
   if (params.contract_whitelist)
      chain.set_contract_whitelist(*params.contract_whitelist);
   if (params.contract_blacklist)
      chain.set_contract_blacklist(*params.contract_blacklist);
   if (params.action_blacklist)
      chain.set_action_blacklist(*params.action_blacklist);
   if (params.key_blacklist)
      chain.set_key_blacklist(*params.key_blacklist);
}

producer_plugin::integrity_hash_information producer_plugin::get_integrity_hash() const {
   return my->get_integrity_hash();
}

void producer_plugin::create_snapshot(producer_plugin::next_function<chain::snapshot_scheduler::snapshot_information> next) {
   my->create_snapshot(std::move(next));
}

chain::snapshot_scheduler::snapshot_schedule_result
producer_plugin::schedule_snapshot(const chain::snapshot_scheduler::snapshot_request_params& srp) {
   chain::controller& chain = my->chain_plug->chain();
   const auto head_block_num = chain.head_block_num();

   // missing start/end is set to head block num, missing end to UINT32_MAX
   chain::snapshot_scheduler::snapshot_request_information sri = {
      .block_spacing   = srp.block_spacing ? *srp.block_spacing : 0, 
      .start_block_num = srp.start_block_num ? *srp.start_block_num : head_block_num + 1,
      .end_block_num   = srp.end_block_num ? *srp.end_block_num : std::numeric_limits<uint32_t>::max(),
      .snapshot_description = srp.snapshot_description ? *srp.snapshot_description : ""
   };

   return my->_snapshot_scheduler.schedule_snapshot(sri);
}

chain::snapshot_scheduler::snapshot_schedule_result
producer_plugin::unschedule_snapshot(const chain::snapshot_scheduler::snapshot_request_id_information& sri) {
   return my->_snapshot_scheduler.unschedule_snapshot(sri.snapshot_request_id);
}

chain::snapshot_scheduler::get_snapshot_requests_result producer_plugin::get_snapshot_requests() const {
   return my->_snapshot_scheduler.get_snapshot_requests();
}

producer_plugin::scheduled_protocol_feature_activations producer_plugin::get_scheduled_protocol_feature_activations() const {
   return {my->_protocol_features_to_activate};
}

void producer_plugin_impl::schedule_protocol_feature_activations(const producer_plugin::scheduled_protocol_feature_activations& schedule) {
   const chain::controller& chain = chain_plug->chain();
   std::set<digest_type>    set_of_features_to_activate(schedule.protocol_features_to_activate.begin(),
                                                     schedule.protocol_features_to_activate.end());
   EOS_ASSERT(set_of_features_to_activate.size() == schedule.protocol_features_to_activate.size(), invalid_protocol_features_to_activate,
              "duplicate digests");
   chain.validate_protocol_features(schedule.protocol_features_to_activate);
   const auto& pfs = chain.get_protocol_feature_manager().get_protocol_feature_set();
   for (auto& feature_digest : set_of_features_to_activate) {
      const auto& pf = pfs.get_protocol_feature(feature_digest);
      EOS_ASSERT(!pf.preactivation_required, protocol_feature_exception,
                 "protocol feature requires preactivation: ${digest}", ("digest", feature_digest));
   }
   _protocol_features_to_activate = schedule.protocol_features_to_activate;
   _protocol_features_signaled    = false;
}

void producer_plugin::schedule_protocol_feature_activations(const scheduled_protocol_feature_activations& schedule) {
   my->schedule_protocol_feature_activations(schedule);
}

fc::variants producer_plugin::get_supported_protocol_features(const get_supported_protocol_features_params& params) const {
   fc::variants             results;
   const chain::controller& chain           = my->chain_plug->chain();
   const auto&              pfs             = chain.get_protocol_feature_manager().get_protocol_feature_set();
   const auto               next_block_time = chain.head_block_time() + fc::milliseconds(config::block_interval_ms);

   flat_map<digest_type, bool> visited_protocol_features;
   visited_protocol_features.reserve(pfs.size());

   std::function<bool(const protocol_feature&)> add_feature =
      [&results, &pfs, &params, next_block_time, &visited_protocol_features, &add_feature](const protocol_feature& pf) -> bool {
      if ((params.exclude_disabled || params.exclude_unactivatable) && !pf.enabled)
         return false;
      if (params.exclude_unactivatable && (next_block_time < pf.earliest_allowed_activation_time))
         return false;

      auto res = visited_protocol_features.emplace(pf.feature_digest, false);
      if (!res.second)
         return res.first->second;

      const auto original_size = results.size();
      for (const auto& dependency : pf.dependencies) {
         if (!add_feature(pfs.get_protocol_feature(dependency))) {
            results.resize(original_size);
            return false;
         }
      }

      res.first->second = true;
      results.emplace_back(pf.to_variant(true));
      return true;
   };

   for (const auto& pf : pfs) {
      add_feature(pf);
   }

   return results;
}

producer_plugin::get_account_ram_corrections_result
producer_plugin::get_account_ram_corrections(const get_account_ram_corrections_params& params) const {
   get_account_ram_corrections_result result;
   const auto&                        db = my->chain_plug->chain().db();

   const auto&  idx = db.get_index<chain::account_ram_correction_index, chain::by_name>();
   account_name lower_bound_value{std::numeric_limits<uint64_t>::lowest()};
   account_name upper_bound_value{std::numeric_limits<uint64_t>::max()};

   if (params.lower_bound) {
      lower_bound_value = *params.lower_bound;
   }

   if (params.upper_bound) {
      upper_bound_value = *params.upper_bound;
   }

   if (upper_bound_value < lower_bound_value)
      return result;

   auto walk_range = [&](auto itr, auto end_itr) {
      for (unsigned int count = 0; count < params.limit && itr != end_itr; ++itr) {
         result.rows.push_back(fc::variant(*itr));
         ++count;
      }
      if (itr != end_itr) {
         result.more = itr->name;
      }
   };

   auto lower = idx.lower_bound(lower_bound_value);
   auto upper = idx.upper_bound(upper_bound_value);
   if (params.reverse) {
      walk_range(boost::make_reverse_iterator(upper), boost::make_reverse_iterator(lower));
   } else {
      walk_range(lower, upper);
   }

   return result;
}

producer_plugin::get_unapplied_transactions_result producer_plugin::get_unapplied_transactions(const get_unapplied_transactions_params& p,
                                                                                               const fc::time_point& deadline) const {

   fc::time_point params_deadline =
      p.time_limit_ms ? std::min(fc::time_point::now().safe_add(fc::milliseconds(*p.time_limit_ms)), deadline) : deadline;

   auto& ua = my->_unapplied_transactions;

   auto itr = ([&]() {
      if (!p.lower_bound.empty()) {
         try {
            auto trx_id = transaction_id_type(p.lower_bound);
            return ua.lower_bound(trx_id);
         } catch (...) {
            return ua.end();
         }
      } else {
         return ua.begin();
      }
   })();

   auto get_trx_type = [&](trx_enum_type t, transaction_metadata::trx_type type) {
      if (type == transaction_metadata::trx_type::dry_run)
         return "dry_run";
      if (type == transaction_metadata::trx_type::read_only)
         return "read_only";
      switch (t) {
      case trx_enum_type::unknown:
         return "unknown";
      case trx_enum_type::forked:
         return "forked";
      case trx_enum_type::aborted:
         return "aborted";
      case trx_enum_type::incoming_api:
         return "incoming_api";
      case trx_enum_type::incoming_p2p:
         return "incoming_p2p";
      }
      return "unknown type";
   };

   get_unapplied_transactions_result result;
   result.size          = ua.size();
   result.incoming_size = ua.incoming_size();

   uint32_t remaining = p.limit ? *p.limit : std::numeric_limits<uint32_t>::max();
   if (deadline != fc::time_point::maximum() && remaining > 1000)
      remaining = 1000;
   while (itr != ua.end() && remaining > 0) {
      auto& r             = result.trxs.emplace_back();
      r.trx_id            = itr->id();
      r.expiration        = itr->expiration();
      const auto& pt      = itr->trx_meta->packed_trx();
      r.trx_type          = get_trx_type(itr->trx_type, itr->trx_meta->get_trx_type());
      r.first_auth        = pt->get_transaction().first_authorizer();
      const auto& actions = pt->get_transaction().actions;
      if (!actions.empty()) {
         r.first_receiver = actions[0].account;
         r.first_action   = actions[0].name;
      }
      r.total_actions      = pt->get_transaction().total_actions();
      r.billed_cpu_time_us = itr->trx_meta->billed_cpu_time_us;
      r.size               = pt->get_estimated_size();

      ++itr;
      remaining--;
      if (fc::time_point::now() >= params_deadline)
         break;
   }

   if (itr != ua.end()) {
      result.more = itr->id();
   }

   return result;
}

block_timestamp_type producer_plugin_impl::calculate_pending_block_time() const {
   const chain::controller& chain = chain_plug->chain();
   const fc::time_point     now   = fc::time_point::now();
   const fc::time_point     base  = std::max<fc::time_point>(now, chain.head_block_time());
   return block_timestamp_type(base).next();
}

bool producer_plugin_impl::should_interrupt_start_block(const fc::time_point& deadline, uint32_t pending_block_num) const {
   if (in_producing_mode()) {
      return deadline <= fc::time_point::now();
   }
   // if we can produce then honor deadline so production starts on time
   return (!_producers.empty() && deadline <= fc::time_point::now()) || (_received_block >= pending_block_num);
}

producer_plugin_impl::start_block_result producer_plugin_impl::start_block() {
   chain::controller& chain = chain_plug->chain();

   if (!chain_plug->accept_transactions())
      return start_block_result::waiting_for_block;

   const auto& hbs = chain.head_block_state();

   if (chain.get_terminate_at_block() > 0 && chain.get_terminate_at_block() <= chain.head_block_num()) {
      ilog("Reached configured maximum block ${num}; terminating", ("num", chain.get_terminate_at_block()));
      app().quit();
      return start_block_result::failed;
   }

   const fc::time_point       now               = fc::time_point::now();
   const block_timestamp_type block_time        = calculate_pending_block_time();
   const uint32_t             pending_block_num = hbs->block_num + 1;

   _pending_block_mode = pending_block_mode::producing;

   // Not our turn
   const auto& scheduled_producer = hbs->get_scheduled_producer(block_time);

   const auto current_watermark = _producer_watermarks.get_watermark(scheduled_producer.producer_name);

   size_t num_relevant_signatures = 0;
   scheduled_producer.for_each_key([&](const public_key_type& key) {
      const auto& iter = _signature_providers.find(key);
      if (iter != _signature_providers.end()) {
         num_relevant_signatures++;
      }
   });

   auto irreversible_block_age = get_irreversible_block_age();

   // If the next block production opportunity is in the present or future, we're synced.
   if (!_production_enabled) {
      _pending_block_mode = pending_block_mode::speculating;
   } else if (_producers.find(scheduled_producer.producer_name) == _producers.end()) {
      _pending_block_mode = pending_block_mode::speculating;
   } else if (num_relevant_signatures == 0) {
      elog("Not producing block because I don't have any private keys relevant to authority: ${authority}",
           ("authority", scheduled_producer.authority));
      _pending_block_mode = pending_block_mode::speculating;
   } else if (_pause_production) {
      elog("Not producing block because production is explicitly paused");
      _pending_block_mode = pending_block_mode::speculating;
   } else if (_max_irreversible_block_age_us.count() >= 0 && irreversible_block_age >= _max_irreversible_block_age_us) {
      elog("Not producing block because the irreversible block is too old [age:${age}s, max:${max}s]",
           ("age", irreversible_block_age.count() / 1'000'000)("max", _max_irreversible_block_age_us.count() / 1'000'000));
      _pending_block_mode = pending_block_mode::speculating;
   }

   if (in_producing_mode()) {
      // determine if our watermark excludes us from producing at this point
      if (current_watermark) {
         const block_timestamp_type block_timestamp{block_time};
         if (current_watermark->first > hbs->block_num) {
            elog("Not producing block because \"${producer}\" signed a block at a higher block number (${watermark}) than the current "
                 "fork's head (${head_block_num})",
                 ("producer", scheduled_producer.producer_name)("watermark", current_watermark->first)("head_block_num", hbs->block_num));
            _pending_block_mode = pending_block_mode::speculating;
         } else if (current_watermark->second >= block_timestamp) {
            elog("Not producing block because \"${producer}\" signed a block at the next block time or later (${watermark}) than the pending "
                 "block time (${block_timestamp})",
                 ("producer", scheduled_producer.producer_name)("watermark", current_watermark->second)("block_timestamp", block_timestamp));
            _pending_block_mode = pending_block_mode::speculating;
         }
      }
   }

   if (in_speculating_mode()) {
      static fc::time_point last_start_block_time = fc::time_point::maximum(); // always start with speculative block
      // Determine if we are syncing: if we have recently started an old block then assume we are syncing
      if (last_start_block_time < now + fc::microseconds(config::block_interval_us)) {
         auto head_block_age = now - chain.head_block_time();
         if (head_block_age > fc::seconds(5))
            return start_block_result::waiting_for_block; // if syncing no need to create a block just to immediately abort it
      }
      last_start_block_time = now;
   }

   // create speculative blocks at regular intervals, so we create blocks with "current" block time
   _pending_block_deadline = now + fc::microseconds(config::block_interval_us);
   if (in_producing_mode()) {
      uint32_t production_round_index = block_timestamp_type(block_time).slot % chain::config::producer_repetitions;
      if (production_round_index == 0) {
         // first block of our round, wait for block production window
         const auto start_block_time = block_time.to_time_point() - fc::microseconds(config::block_interval_us);
         if (now < start_block_time) {
            fc_dlog(_log, "Not starting block until ${bt}", ("bt", start_block_time));
            schedule_delayed_production_loop(weak_from_this(), start_block_time);
            return start_block_result::waiting_for_production;
         }
      }

      _pending_block_deadline = block_timing_util::calculate_producing_block_deadline(_cpu_effort_us, block_time);
   } else if (!_producers.empty()) {
      // cpu effort percent doesn't matter for the first block of the round, use max (block_interval_us) for cpu effort
      auto wake_time = block_timing_util::calculate_producer_wake_up_time(config::block_interval_us, chain.head_block_num(), chain.head_block_time(),
                                                                          _producers, chain.head_block_state()->active_schedule.producers,
                                                                          _producer_watermarks);
      if (wake_time)
         _pending_block_deadline = std::min(*wake_time, _pending_block_deadline);
   }

   const auto& preprocess_deadline = _pending_block_deadline;

   fc_dlog(_log, "Starting block #${n} at ${time} producer ${p}", ("n", pending_block_num)("time", now)("p", scheduled_producer.producer_name));

   try {
      uint16_t blocks_to_confirm = 0;

      if (in_producing_mode()) {
         // determine how many blocks this producer can confirm
         // 1) if it is not a producer from this node, assume no confirmations (we will discard this block anyway)
         // 2) if it is a producer on this node that has never produced, the conservative approach is to assume no
         //    confirmations to make sure we don't double sign after a crash TODO: make these watermarks durable?
         // 3) if it is a producer on this node where this node knows the last block it produced, safely set it -UNLESS-
         // 4) the producer on this node's last watermark is higher (meaning on a different fork)
         if (current_watermark) {
            auto watermark_bn = current_watermark->first;
            if (watermark_bn < hbs->block_num) {
               blocks_to_confirm = (uint16_t)(std::min<uint32_t>(std::numeric_limits<uint16_t>::max(), (uint32_t)(hbs->block_num - watermark_bn)));
            }
         }

         // can not confirm irreversible blocks
         blocks_to_confirm = (uint16_t)(std::min<uint32_t>(blocks_to_confirm, (uint32_t)(hbs->block_num - hbs->dpos_irreversible_blocknum)));
      }

      abort_block();

      auto features_to_activate = chain.get_preactivated_protocol_features();
      if (in_producing_mode() && _protocol_features_to_activate.size() > 0) {
         bool drop_features_to_activate = false;
         try {
            chain.validate_protocol_features(_protocol_features_to_activate);
         } catch (const std::bad_alloc&) {
            chain_apis::api_base::handle_bad_alloc();
         } catch (const boost::interprocess::bad_alloc&) {
            chain_apis::api_base::handle_bad_alloc();
         } catch (const fc::exception& e) {
            wlog("protocol features to activate are no longer all valid: ${details}", ("details", e.to_detail_string()));
            drop_features_to_activate = true;
         } catch (const std::exception& e) {
            wlog("protocol features to activate are no longer all valid: ${details}",
                 ("details", fc::std_exception_wrapper::from_current_exception(e).to_detail_string()));
            drop_features_to_activate = true;
         }

         if (drop_features_to_activate) {
            _protocol_features_to_activate.clear();
         } else {
            auto protocol_features_to_activate = _protocol_features_to_activate; // do a copy as pending_block might be aborted
            if (features_to_activate.size() > 0) {
               protocol_features_to_activate.reserve(protocol_features_to_activate.size() + features_to_activate.size());
               std::set<digest_type> set_of_features_to_activate(protocol_features_to_activate.begin(),
                                                                 protocol_features_to_activate.end());
               for (const auto& f : features_to_activate) {
                  auto res = set_of_features_to_activate.insert(f);
                  if (res.second) {
                     protocol_features_to_activate.push_back(f);
                  }
               }
               features_to_activate.clear();
            }
            std::swap(features_to_activate, protocol_features_to_activate);
            _protocol_features_signaled = true;
            ilog("signaling activation of the following protocol features in block ${num}: ${features_to_activate}",
                 ("num", pending_block_num)("features_to_activate", features_to_activate));
         }
      }

      controller::block_status bs =
         in_producing_mode() ? controller::block_status::incomplete : controller::block_status::ephemeral;
      chain.start_block(block_time, blocks_to_confirm, features_to_activate, bs, preprocess_deadline);
   }
   LOG_AND_DROP();

   if (chain.is_building_block()) {
      const auto& pending_block_signing_authority = chain.pending_block_signing_authority();

      if (in_producing_mode() && pending_block_signing_authority != scheduled_producer.authority) {
         elog("Unexpected block signing authority, reverting to speculative mode! [expected: \"${expected}\", actual: \"${actual\"",
              ("expected", scheduled_producer.authority)("actual", pending_block_signing_authority));
         _pending_block_mode = pending_block_mode::speculating;
      }

      try {
         chain::subjective_billing& subjective_bill = chain.get_mutable_subjective_billing();
         _account_fails.report_and_clear(hbs->block_num, subjective_bill);

         if (!remove_expired_trxs(preprocess_deadline))
            return start_block_result::exhausted;
         if (!remove_expired_blacklisted_trxs(preprocess_deadline))
            return start_block_result::exhausted;
         if (!subjective_bill.remove_expired(_log, chain.pending_block_time(), fc::time_point::now(), [&]() {
                return should_interrupt_start_block(preprocess_deadline, pending_block_num);
             })) {
            return start_block_result::exhausted;
         }

         // limit execution of pending incoming to once per block
         auto incoming_itr = _unapplied_transactions.incoming_begin();

         if (in_producing_mode()) {
            if (!process_unapplied_trxs(preprocess_deadline))
               return start_block_result::exhausted;


            auto scheduled_trx_deadline = preprocess_deadline;
            if (_max_scheduled_transaction_time_per_block_ms >= 0) {
               scheduled_trx_deadline = std::min<fc::time_point>(
                  scheduled_trx_deadline, fc::time_point::now() + fc::milliseconds(_max_scheduled_transaction_time_per_block_ms));
            }
            // may exhaust scheduled_trx_deadline but not preprocess_deadline, exhausted preprocess_deadline checked below
            process_scheduled_and_incoming_trxs(scheduled_trx_deadline, incoming_itr);
         }

         repost_exhausted_transactions(preprocess_deadline);

         if (app().is_quiting()) // db guard exception above in LOG_AND_DROP could have called app().quit()
            return start_block_result::failed;
         if (should_interrupt_start_block(preprocess_deadline, pending_block_num) || block_is_exhausted()) {
            return start_block_result::exhausted;
         }

         if (!process_incoming_trxs(preprocess_deadline, incoming_itr))
            return start_block_result::exhausted;

         return start_block_result::succeeded;

      } catch (const guard_exception& e) {
         chain_plugin::handle_guard_exception(e);
         return start_block_result::failed;
      } catch (std::bad_alloc&) {
         chain_apis::api_base::handle_bad_alloc();
      } catch (boost::interprocess::bad_alloc&) {
         chain_apis::api_base::handle_db_exhaustion();
      }
   }

   return start_block_result::failed;
}

bool producer_plugin_impl::remove_expired_trxs(const fc::time_point& deadline) {
   chain::controller& chain              = chain_plug->chain();
   auto               pending_block_time = chain.pending_block_time();
   auto               pending_block_num  = chain.pending_block_num();

   // remove all expired transactions
   size_t num_expired = 0;
   size_t orig_count  = _unapplied_transactions.size();
   bool   exhausted   = !_unapplied_transactions.clear_expired(
      pending_block_time,
      [&]() { return should_interrupt_start_block(deadline, pending_block_num); },
      [&num_expired](const packed_transaction_ptr& packed_trx_ptr, trx_enum_type trx_type) {
         // expired exception is logged as part of next() call
         ++num_expired;
      });

   if (exhausted && in_producing_mode()) {
      fc_wlog(_log, "Unable to process all expired transactions of the ${n} transactions in the unapplied queue before deadline, "
              "Expired ${expired}", ("n", orig_count)("expired", num_expired));
   } else {
      fc_dlog(_log, "Processed ${ex} expired transactions of the ${n} transactions in the unapplied queue.", ("n", orig_count)("ex", num_expired));
   }

   return !exhausted;
}

bool producer_plugin_impl::remove_expired_blacklisted_trxs(const fc::time_point& deadline) {
   bool  exhausted           = false;
   auto& blacklist_by_expiry = _blacklisted_transactions.get<by_expiry>();
   if (!blacklist_by_expiry.empty()) {
      const chain::controller& chain             = chain_plug->chain();
      const auto               lib_time          = chain.last_irreversible_block_time();
      const auto               pending_block_num = chain.pending_block_num();

      int num_expired = 0;
      int orig_count  = _blacklisted_transactions.size();

      while (!blacklist_by_expiry.empty() && blacklist_by_expiry.begin()->expiry <= lib_time) {
         if (should_interrupt_start_block(deadline, pending_block_num)) {
            exhausted = true;
            break;
         }
         blacklist_by_expiry.erase(blacklist_by_expiry.begin());
         num_expired++;
      }

      fc_dlog(_log, "Processed ${n} blacklisted transactions, Expired ${expired}", ("n", orig_count)("expired", num_expired));
   }
   return !exhausted;
}

// Returns contract name, action name, and exception text of an exception that occurred in a contract
inline std::string get_detailed_contract_except_info(const packed_transaction_ptr& trx,
                                                     const transaction_trace_ptr&  trace,
                                                     const fc::exception_ptr&      except_ptr) {
   std::string contract_name;
   std::string act_name;
   if (trace && !trace->action_traces.empty()) {
      auto last_action_ordinal = trace->action_traces.size() - 1;
      contract_name            = trace->action_traces[last_action_ordinal].receiver.to_string();
      act_name                 = trace->action_traces[last_action_ordinal].act.name.to_string();
   } else if (trx) {
      const auto& actions = trx->get_transaction().actions;
      if (actions.empty())
         return {}; // should not be possible
      contract_name = actions[0].account.to_string();
      act_name      = actions[0].name.to_string();
   }

   std::string details = except_ptr ? except_ptr->top_message() : ((trace && trace->except) ? trace->except->top_message() : std::string());
   fc::escape_str(details, fc::escape_control_chars::on, 1024);

   // this format is parsed by external tools
   return "action: " + contract_name + ":" + act_name + ", " + details;
}

void producer_plugin_impl::log_trx_results(const transaction_metadata_ptr& trx,
                                           const transaction_trace_ptr&    trace,
                                           const fc::time_point&           start) {
   uint32_t billed_cpu_time_us = (trace && trace->receipt) ? trace->receipt->cpu_usage_us : 0;
   log_trx_results(trx->packed_trx(), trace, nullptr, billed_cpu_time_us, start, trx->is_transient());
}

void producer_plugin_impl::log_trx_results(const transaction_metadata_ptr& trx, const fc::exception_ptr& except_ptr) {
   uint32_t billed_cpu_time_us = trx ? trx->billed_cpu_time_us : 0;
   log_trx_results(trx->packed_trx(), nullptr, except_ptr, billed_cpu_time_us, fc::time_point::now(), trx->is_transient());
}

void producer_plugin_impl::log_trx_results(const packed_transaction_ptr& trx,
                                           const transaction_trace_ptr&  trace,
                                           const fc::exception_ptr&      except_ptr,
                                           uint32_t                      billed_cpu_us,
                                           const fc::time_point&         start,
                                           bool                          is_transient) {
   chain::controller& chain = chain_plug->chain();

   auto get_trace = [&](const transaction_trace_ptr& trace, const fc::exception_ptr& except_ptr) -> fc::variant {
      if (trace) {
         return chain_plug->get_log_trx_trace(trace);
      } else {
         return fc::variant{except_ptr};
      }
   };

   bool except = except_ptr || (trace && trace->except);
   if (except) {
      if (in_producing_mode()) {
         fc_dlog(is_transient ? _transient_trx_failed_trace_log : _trx_failed_trace_log,
                 "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING ${desc}tx: ${txid}, auth: ${a}, ${details}",
                 ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())("desc", is_transient ? "transient " : "")
                 ("txid", trx->id())("a", trx->get_transaction().first_authorizer())
                 ("details", get_detailed_contract_except_info(trx, trace, except_ptr)));

         if (!is_transient) {
            fc_dlog(_trx_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING tx: ${trx}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_failure_log,
                    "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING tx: ${entire_trace}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("entire_trace", get_trace(trace, except_ptr)));
         }
      } else {
         fc_dlog(is_transient ? _transient_trx_failed_trace_log : _trx_failed_trace_log,
                 "[TRX_TRACE] Speculative execution is REJECTING ${desc}tx: ${txid}, auth: ${a} : ${details}",
                 ("desc", is_transient ? "transient " : "")("txid", trx->id())
                 ("a", trx->get_transaction().first_authorizer())("details", get_detailed_contract_except_info(trx, trace, except_ptr)));
         if (!is_transient) {
            fc_dlog(_trx_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${trx} ",
                    ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_failure_log,
                    "[TRX_TRACE] Speculative execution is REJECTING tx: ${entire_trace} ",
                    ("entire_trace", get_trace(trace, except_ptr)));
         }
      }
   } else {
      if (in_producing_mode()) {
         fc_dlog(is_transient ? _transient_trx_successful_trace_log : _trx_successful_trace_log,
                 "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING ${desc}tx: ${txid}, auth: ${a}, cpu: ${cpu}",
                 ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())("desc", is_transient ? "transient " : "")
                 ("txid", trx->id())("a", trx->get_transaction().first_authorizer())("cpu", billed_cpu_us));
         if (!is_transient) {
            fc_dlog(_trx_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING tx: ${trx}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_success_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING tx: ${entire_trace}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("entire_trace", get_trace(trace, except_ptr)));
         }
      } else {
         fc_dlog(is_transient ? _transient_trx_successful_trace_log : _trx_successful_trace_log,
                 "[TRX_TRACE] Speculative execution is ACCEPTING ${desc}tx: ${txid}, auth: ${a}, cpu: ${cpu}",
                 ("desc", is_transient ? "transient " : "")("txid", trx->id())("a", trx->get_transaction().first_authorizer())
                 ("cpu", billed_cpu_us));
         if (!is_transient) {
            fc_dlog(_trx_log, "[TRX_TRACE] Speculative execution is ACCEPTING tx: ${trx}", ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_success_log, "[TRX_TRACE] Speculative execution is ACCEPTING tx: ${entire_trace}",
                    ("entire_trace", get_trace(trace, except_ptr)));
         }
      }
   }
}

// Does not modify unapplied_transaction_queue
producer_plugin_impl::push_result producer_plugin_impl::push_transaction(const fc::time_point&                       block_deadline,
                                                                         const transaction_metadata_ptr&             trx,
                                                                         bool                                        api_trx,
                                                                         bool                                        return_failure_trace,
                                                                         block_time_tracker::trx_time_tracker&       trx_tracker,
                                                                         const next_function<transaction_trace_ptr>& next) {
   auto start = fc::time_point::now();
   EOS_ASSERT(!trx->is_read_only(), producer_exception, "Unexpected read-only trx");

   chain::controller&         chain           = chain_plug->chain();
   chain::subjective_billing& subjective_bill = chain.get_mutable_subjective_billing();

   auto first_auth = trx->packed_trx()->get_transaction().first_authorizer();

   bool disable_subjective_enforcement = (api_trx && _disable_subjective_api_billing) ||
                                         (!api_trx && _disable_subjective_p2p_billing) ||
                                         subjective_bill.is_account_disabled(first_auth) ||
                                         trx->is_transient();

   if (!disable_subjective_enforcement && _account_fails.failure_limit(first_auth)) {
      if (next) {
         auto except_ptr = std::static_pointer_cast<fc::exception>(std::make_shared<tx_cpu_usage_exceeded>(
            FC_LOG_MESSAGE(error, "transaction ${id} exceeded failure limit for account ${a} until ${next_reset_time}",
                           ("id", trx->id())("a", first_auth)
                           ("next_reset_time", _account_fails.next_reset_timepoint(chain.head_block_num(), chain.head_block_time())))));
         log_trx_results(trx, except_ptr);
         next(except_ptr);
      }
      return push_result{.failed = true};
   }

   fc::microseconds max_trx_time = fc::milliseconds(_max_transaction_time_ms.load());
   if (max_trx_time.count() < 0)
      max_trx_time = fc::microseconds::maximum();

   int64_t sub_bill = 0;
   if (!disable_subjective_enforcement)
      sub_bill = subjective_bill.get_subjective_bill(first_auth, fc::time_point::now());

   auto prev_billed_cpu_time_us = trx->billed_cpu_time_us;
   if (in_producing_mode() && prev_billed_cpu_time_us > 0) {
      const auto& rl = chain.get_resource_limits_manager();
      if (!subjective_bill.is_account_disabled(first_auth) && !rl.is_unlimited_cpu(first_auth)) {
         int64_t prev_billed_plus100_us = prev_billed_cpu_time_us + EOS_PERCENT(prev_billed_cpu_time_us, 100 * config::percent_1);
         if (prev_billed_plus100_us < max_trx_time.count())
            max_trx_time = fc::microseconds(prev_billed_plus100_us);
      }
   }

   auto trace = chain.push_transaction(trx, block_deadline, max_trx_time, prev_billed_cpu_time_us, false, sub_bill);

   auto pr = handle_push_result(trx, next, start, chain, trace, return_failure_trace, disable_subjective_enforcement, first_auth, sub_bill, prev_billed_cpu_time_us);

   if (!pr.failed) {
      trx_tracker.trx_success();
   }
   return pr;
}

producer_plugin_impl::push_result
producer_plugin_impl::handle_push_result(const transaction_metadata_ptr&             trx,
                                         const next_function<transaction_trace_ptr>& next,
                                         const fc::time_point&                       start,
                                         chain::controller&                          chain,
                                         const transaction_trace_ptr&                trace,
                                         bool                                        return_failure_trace,
                                         bool         disable_subjective_enforcement,
                                         account_name first_auth,
                                         int64_t      sub_bill,
                                         uint32_t     prev_billed_cpu_time_us) {
   auto                       end             = fc::time_point::now();
   chain::subjective_billing& subjective_bill = chain.get_mutable_subjective_billing();

   push_result pr;
   if( trace->except ) {
      if( exception_is_exhausted( *trace->except ) ) {
         if( in_producing_mode() ) {
            fc_dlog(trx->is_transient() ? _transient_trx_failed_trace_log : _trx_failed_trace_log,
                    "[TRX_TRACE] Block ${block_num} for producer ${prod} COULD NOT FIT, tx: ${txid} RETRYING ",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())("txid", trx->id()));
         } else {
            fc_dlog(trx->is_transient() ? _transient_trx_failed_trace_log : _trx_failed_trace_log,
                    "[TRX_TRACE] Speculative execution COULD NOT FIT tx: ${txid} RETRYING", ("txid", trx->id()));
         }
         if (!trx->is_read_only())
            pr.block_exhausted = block_is_exhausted(); // smaller trx might fit
         pr.trx_exhausted = true;
      } else {
         pr.failed              = true;
         const fc::exception& e = *trace->except;
         if (e.code() != tx_duplicate::code_value) {
            fc_tlog(_log, "Subjective bill for failed ${a}: ${b} elapsed ${t}us, time ${r}us",
                    ("a", first_auth)("b", sub_bill)("t", trace->elapsed)("r", end - start));
            if (!disable_subjective_enforcement) // subjectively bill failure when producing since not in objective cpu account billing
               subjective_bill.subjective_bill_failure(first_auth, trace->elapsed, fc::time_point::now());

            log_trx_results(trx, trace, start);
            // this failed our configured maximum transaction time, we don't want to replay it
            fc_tlog(_log, "Failed ${c} trx, auth: ${a}, prev billed: ${p}us, ran: ${r}us, id: ${id}, except: ${e}",
                    ("c", e.code())("a", first_auth)("p", prev_billed_cpu_time_us)("r", end - start)("id", trx->id())("e", e));
            if (!disable_subjective_enforcement)
               _account_fails.add(first_auth, e);
         }
         if (next) {
            if (return_failure_trace) {
               next(trace);
            } else {
               auto e_ptr = trace->except->dynamic_copy_exception();
               next(e_ptr);
            }
         }
      }
   } else {
      fc_tlog(_log, "Subjective bill for success ${a}: ${b} elapsed ${t}us, time ${r}us",
              ("a", first_auth)("b", sub_bill)("t", trace->elapsed)("r", end - start));
      log_trx_results(trx, trace, start);
      // if producing then trx is in objective cpu account billing
      if (!disable_subjective_enforcement && !in_producing_mode()) {
         subjective_bill.subjective_bill(trx->id(), trx->packed_trx()->expiration(), first_auth, trace->elapsed);
      }
      if (next)
         next(trace);
   }

   return pr;
}

bool producer_plugin_impl::process_unapplied_trxs(const fc::time_point& deadline) {
   bool exhausted = false;
   if (!_unapplied_transactions.empty()) {
      const chain::controller& chain             = chain_plug->chain();
      const auto               pending_block_num = chain.pending_block_num();
      int                      num_applied = 0, num_failed = 0, num_processed = 0;
      auto                     unapplied_trxs_size = _unapplied_transactions.size();
      auto                     itr                 = _unapplied_transactions.unapplied_begin();
      auto                     end_itr             = _unapplied_transactions.unapplied_end();
      while (itr != end_itr) {
         if (should_interrupt_start_block(deadline, pending_block_num)) {
            exhausted = true;
            break;
         }

         ++num_processed;
         try {
            auto trx_tracker = _time_tracker.start_trx(itr->trx_meta->is_transient());
            push_result pr = push_transaction(deadline, itr->trx_meta, false, itr->return_failure_trace, trx_tracker, itr->next);

            exhausted = pr.block_exhausted;
            if (exhausted) {
               break;
            } else {
               if (pr.failed) {
                  ++num_failed;
               } else {
                  ++num_applied;
               }
            }
            if (!pr.trx_exhausted) {
               itr = _unapplied_transactions.erase(itr);
            } else {
               ++itr; // keep exhausted
            }
            continue;
         }
         LOG_AND_DROP();
         ++num_failed;
         ++itr;
      }

      fc_dlog(_log, "Processed ${m} of ${n} previously applied transactions, Applied ${applied}, Failed/Dropped ${failed}",
              ("m", num_processed)("n", unapplied_trxs_size)("applied", num_applied)("failed", num_failed));
   }
   return !exhausted;
}

void producer_plugin_impl::process_scheduled_and_incoming_trxs(const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr) {
   // scheduled transactions
   int    num_applied         = 0;
   int    num_failed          = 0;
   int    num_processed       = 0;
   bool   exhausted           = false;
   double incoming_trx_weight = 0.0;

   auto&              blacklist_by_id     = _blacklisted_transactions.get<by_id>();
   chain::controller& chain               = chain_plug->chain();
   time_point         pending_block_time  = chain.pending_block_time();
   auto               end                 = _unapplied_transactions.incoming_end();
   const auto&        sch_idx             = chain.db().get_index<generated_transaction_multi_index, by_delay>();
   const auto         scheduled_trxs_size = sch_idx.size();
   auto               sch_itr             = sch_idx.begin();
   while (sch_itr != sch_idx.end()) {
      if (sch_itr->delay_until > pending_block_time)
         break; // not scheduled yet
      if (exhausted || deadline <= fc::time_point::now()) {
         exhausted = true;
         break;
      }
      if (sch_itr->published >= pending_block_time) {
         ++sch_itr;
         continue; // do not allow schedule and execute in same block
      }

      if (blacklist_by_id.find(sch_itr->trx_id) != blacklist_by_id.end()) {
         ++sch_itr;
         continue;
      }

      const transaction_id_type trx_id         = sch_itr->trx_id; // make copy since reference could be invalidated
      const auto                sch_expiration = sch_itr->expiration;
      auto                      sch_itr_next   = sch_itr; // save off next since sch_itr may be invalidated by loop
      ++sch_itr_next;
      const auto next_delay_until = sch_itr_next != sch_idx.end() ? sch_itr_next->delay_until : sch_itr->delay_until;
      const auto next_id          = sch_itr_next != sch_idx.end() ? sch_itr_next->id : sch_itr->id;

      num_processed++;

      // configurable ratio of incoming txns vs deferred txns
      while (incoming_trx_weight >= 1.0 && itr != end) {
         if (deadline <= fc::time_point::now()) {
            exhausted = true;
            break;
         }

         incoming_trx_weight -= 1.0;

         auto trx_meta = itr->trx_meta;
         bool api_trx  = itr->trx_type == trx_enum_type::incoming_api;

         auto trx_tracker = _time_tracker.start_trx(trx_meta->is_transient());
         push_result pr = push_transaction(deadline, trx_meta, api_trx, itr->return_failure_trace, trx_tracker, itr->next);

         exhausted = pr.block_exhausted;
         if (pr.trx_exhausted) {
            ++itr; // leave in incoming
         } else {
            itr = _unapplied_transactions.erase(itr);
         }

         if (exhausted)
            break;
      }

      if (exhausted || deadline <= fc::time_point::now()) {
         exhausted = true;
         break;
      }

      auto get_first_authorizer = [&](const transaction_trace_ptr& trace) {
         for (const auto& a : trace->action_traces) {
            for (const auto& u : a.act.authorization)
               return u.actor;
         }
         return account_name();
      };

      try {
         auto             start        = fc::time_point::now();
         auto             trx_tracker  = _time_tracker.start_trx(false, start); // delayed transaction cannot be transient
         fc::microseconds max_trx_time = fc::milliseconds(_max_transaction_time_ms.load());
         if (max_trx_time.count() < 0)
            max_trx_time = fc::microseconds::maximum();

         auto trace = chain.push_scheduled_transaction(trx_id, deadline, max_trx_time, 0, false);
         auto end   = fc::time_point::now();
         if (trace->except) {
            if (exception_is_exhausted(*trace->except)) {
               if (block_is_exhausted()) {
                  exhausted = true;
                  break;
               }
            } else {
               fc_dlog(_trx_failed_trace_log,
                       "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING scheduled tx: ${txid}, time: ${r}, auth: ${a} : ${details}",
                       ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())("txid", trx_id)("r", end - start)
                       ("a", get_first_authorizer(trace))("details", get_detailed_contract_except_info(nullptr, trace, nullptr)));
               fc_dlog(_trx_trace_failure_log,
                       "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING scheduled tx: ${entire_trace}",
                       ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                       ("entire_trace", chain_plug->get_log_trx_trace(trace)));
               // this failed our configured maximum transaction time, we don't want to replay it add it to a blacklist
               _blacklisted_transactions.insert(transaction_id_with_expiry{trx_id, sch_expiration});
               num_failed++;
            }
         } else {
            trx_tracker.trx_success();
            fc_dlog(_trx_successful_trace_log,
                    "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING scheduled tx: ${txid}, time: ${r}, auth: ${a}, cpu: ${cpu}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())("txid", trx_id)("r", end - start)
                    ("a", get_first_authorizer(trace))("cpu", trace->receipt ? trace->receipt->cpu_usage_us : 0));
            fc_dlog(_trx_trace_success_log,
                    "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING scheduled tx: ${entire_trace}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("entire_trace", chain_plug->get_log_trx_trace(trace)));
            num_applied++;
         }
      }
      LOG_AND_DROP();

      incoming_trx_weight += _incoming_defer_ratio;

      if (sch_itr_next == sch_idx.end())
         break;
      sch_itr = sch_idx.lower_bound(boost::make_tuple(next_delay_until, next_id));
   }

   if (scheduled_trxs_size > 0) {
      fc_dlog(_log, "Processed ${m} of ${n} scheduled transactions, Applied ${applied}, Failed/Dropped ${failed}",
              ("m", num_processed)("n", scheduled_trxs_size)("applied", num_applied)("failed", num_failed));
   }
}

bool producer_plugin_impl::process_incoming_trxs(const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr) {
   bool exhausted = false;
   auto end       = _unapplied_transactions.incoming_end();
   if (itr != end) {
      size_t processed = 0;
      fc_dlog(_log, "Processing ${n} pending transactions", ("n", _unapplied_transactions.incoming_size()));
      const chain::controller& chain             = chain_plug->chain();
      const auto               pending_block_num = chain.pending_block_num();
      while (itr != end) {
         if (should_interrupt_start_block(deadline, pending_block_num)) {
            exhausted = true;
            break;
         }

         auto trx_meta = itr->trx_meta;
         bool api_trx  = itr->trx_type == trx_enum_type::incoming_api;

         auto trx_tracker = _time_tracker.start_trx(trx_meta->is_transient());
         push_result pr = push_transaction(deadline, trx_meta, api_trx, itr->return_failure_trace, trx_tracker, itr->next);

         exhausted = pr.block_exhausted;
         if (pr.trx_exhausted) {
            ++itr; // leave in incoming
         } else {
            itr = _unapplied_transactions.erase(itr);
         }

         if (exhausted)
            break;
         ++processed;
      }
      fc_dlog(_log, "Processed ${n} pending transactions, ${p} left", ("n", processed)("p", _unapplied_transactions.incoming_size()));
   }
   return !exhausted;
}

bool producer_plugin_impl::block_is_exhausted() const {
   const chain::controller& chain = chain_plug->chain();
   const auto&              rl    = chain.get_resource_limits_manager();

   const uint64_t cpu_limit = rl.get_block_cpu_limit();
   if (cpu_limit < _max_block_cpu_usage_threshold_us)
      return true;
   const uint64_t net_limit = rl.get_block_net_limit();
   if (net_limit < _max_block_net_usage_threshold_bytes)
      return true;
   return false;
}

// Example:
// --> Start block A (block time x.500) at time x.000
// -> start_block()
// --> deadline, produce block x.500 at time x.400 (assuming 80% cpu block effort)
// -> Idle
// --> Start block B (block time y.000) at time x.500
void producer_plugin_impl::schedule_production_loop() {
   _timer.cancel();

   auto result = start_block();

   if (result == start_block_result::failed) {
      elog("Failed to start a pending block, will try again later");
      _timer.expires_from_now(boost::posix_time::microseconds(config::block_interval_us / 10));

      // we failed to start a block, so try again later?
      _timer.async_wait(
         app().executor().wrap(priority::high,
                               exec_queue::read_write,
                               [weak_this = weak_from_this(), cid = ++_timer_corelation_id](const boost::system::error_code& ec) {
                                  auto self = weak_this.lock();
                                  if (self && ec != boost::asio::error::operation_aborted && cid == self->_timer_corelation_id) {
                                     self->schedule_production_loop();
                                  }
                               }));
   } else if (result == start_block_result::waiting_for_block) {
      if (!_producers.empty() && !production_disabled_by_policy()) {
         chain::controller& chain = chain_plug->chain();
         fc_dlog(_log, "Waiting till another block is received and scheduling Speculative/Production Change");
         auto wake_time = block_timing_util::calculate_producer_wake_up_time(_cpu_effort_us, chain.head_block_num(), calculate_pending_block_time(),
                                                                             _producers, chain.head_block_state()->active_schedule.producers,
                                                                             _producer_watermarks);
         schedule_delayed_production_loop(weak_from_this(), wake_time);
      } else {
         fc_tlog(_log, "Waiting till another block is received");
         // nothing to do until more blocks arrive
      }

   } else if (result == start_block_result::waiting_for_production) {
      // scheduled in start_block()

   } else if (in_producing_mode()) {
      schedule_maybe_produce_block(result == start_block_result::exhausted);

   } else if (in_speculating_mode() && !_producers.empty() && !production_disabled_by_policy()) {
      chain::controller& chain = chain_plug->chain();
      fc_dlog(_log, "Speculative Block Created; Scheduling Speculative/Production Change");
      EOS_ASSERT(chain.is_building_block(), missing_pending_block_state, "speculating without pending_block_state");
      auto wake_time = block_timing_util::calculate_producer_wake_up_time(_cpu_effort_us, chain.pending_block_num(), chain.pending_block_timestamp(),
                                                                          _producers, chain.head_block_state()->active_schedule.producers,
                                                                          _producer_watermarks);
      schedule_delayed_production_loop(weak_from_this(), wake_time);
   } else {
      fc_dlog(_log, "Speculative Block Created");
   }

   _time_tracker.add_other_time();
}

void producer_plugin_impl::schedule_maybe_produce_block(bool exhausted) {
   chain::controller& chain = chain_plug->chain();

   assert(in_producing_mode());
   // we succeeded but block may be exhausted
   static const boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
   auto deadline = block_timing_util::calculate_producing_block_deadline(_cpu_effort_us, chain.pending_block_time());

   if (!exhausted && deadline > fc::time_point::now()) {
      // ship this block off no later than its deadline
      EOS_ASSERT(chain.is_building_block(), missing_pending_block_state, "producing without pending_block_state, start_block succeeded");
      _timer.expires_at(epoch + boost::posix_time::microseconds(deadline.time_since_epoch().count()));
      fc_dlog(_log, "Scheduling Block Production on Normal Block #${num} for ${time}",
              ("num", chain.head_block_num() + 1)("time", deadline));
   } else {
      EOS_ASSERT(chain.is_building_block(), missing_pending_block_state, "producing without pending_block_state");
      _timer.expires_from_now(boost::posix_time::microseconds(0));
      fc_dlog(_log, "Scheduling Block Production on ${desc} Block #${num} immediately",
              ("num", chain.head_block_num() + 1)("desc", block_is_exhausted() ? "Exhausted" : "Deadline exceeded"));
   }

   _timer.async_wait(app().executor().wrap(
      priority::high,
      exec_queue::read_write,
      [&chain, weak_this = weak_from_this(), cid = ++_timer_corelation_id](const boost::system::error_code& ec) {
         auto self = weak_this.lock();
         if (self && ec != boost::asio::error::operation_aborted && cid == self->_timer_corelation_id) {
            // pending_block_state expected, but can't assert inside async_wait
            auto block_num = chain.is_building_block() ? chain.head_block_num() + 1 : 0;
            fc_dlog(_log, "Produce block timer for ${num} running at ${time}", ("num", block_num)("time", fc::time_point::now()));
            auto res = self->maybe_produce_block();
            fc_dlog(_log, "Producing Block #${num} returned: ${res}", ("num", block_num)("res", res));
         }
      }));
}

void producer_plugin_impl::schedule_delayed_production_loop(const std::weak_ptr<producer_plugin_impl>& weak_this,
                                                            std::optional<fc::time_point>              wake_up_time) {
   if (wake_up_time) {
      fc_dlog(_log, "Scheduling Speculative/Production Change at ${time}", ("time", wake_up_time));
      static const boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
      _timer.expires_at(epoch + boost::posix_time::microseconds(wake_up_time->time_since_epoch().count()));
      _timer.async_wait(app().executor().wrap(
         priority::high, exec_queue::read_write, [weak_this, cid = ++_timer_corelation_id](const boost::system::error_code& ec) {
            auto self = weak_this.lock();
            if (self && ec != boost::asio::error::operation_aborted && cid == self->_timer_corelation_id) {
               self->schedule_production_loop();
            }
         }));
   } else {
      fc_dlog(_log, "Not Scheduling Speculative/Production, no local producers had valid wake up times");
   }
}


bool producer_plugin_impl::maybe_produce_block() {
   auto reschedule = fc::make_scoped_exit([this] { schedule_production_loop(); });

   try {
      produce_block();
      return true;
   }
   LOG_AND_DROP();

   fc_dlog(_log, "Aborting block due to produce_block error");
   abort_block();
   return false;
}

static auto make_debug_time_logger() {
   auto start = fc::time_point::now();
   return fc::make_scoped_exit([=]() { fc_dlog(_log, "Signing took ${ms}us", ("ms", fc::time_point::now() - start)); });
}

static auto maybe_make_debug_time_logger() -> std::optional<decltype(make_debug_time_logger())> {
   if (_log.is_enabled(fc::log_level::debug)) {
      return make_debug_time_logger();
   } else {
      return {};
   }
}

void producer_plugin_impl::produce_block() {
   auto start = fc::time_point::now();
   _time_tracker.add_idle_time(start);

   EOS_ASSERT(in_producing_mode(), producer_exception, "called produce_block while not actually producing");
   chain::controller& chain = chain_plug->chain();
   EOS_ASSERT(chain.is_building_block(), missing_pending_block_state,
              "pending_block_state does not exist but it should, another plugin may have corrupted it");

   const auto&                                                        auth = chain.pending_block_signing_authority();
   std::vector<std::reference_wrapper<const signature_provider_type>> relevant_providers;

   relevant_providers.reserve(_signature_providers.size());

   producer_authority::for_each_key(auth, [&](const public_key_type& key) {
      const auto& iter = _signature_providers.find(key);
      if (iter != _signature_providers.end()) {
         relevant_providers.emplace_back(iter->second);
      }
   });

   EOS_ASSERT(relevant_providers.size() > 0, producer_priv_key_not_found,
              "Attempting to produce a block for which we don't have any relevant private keys");

   if (_protocol_features_signaled) {
      _protocol_features_to_activate.clear(); // clear _protocol_features_to_activate as it is already set in pending_block
      _protocol_features_signaled = false;
   }

   // idump( (fc::time_point::now() - chain.pending_block_time()) );
   controller::block_report br;
   chain.finalize_block(br, [&](const digest_type& d) {
      auto                   debug_logger = maybe_make_debug_time_logger();
      vector<signature_type> sigs;
      sigs.reserve(relevant_providers.size());

      // sign with all relevant public keys
      for (const auto& p : relevant_providers) {
         sigs.emplace_back(p.get()(d));
      }
      return sigs;
   });

   chain.commit_block();

   block_state_ptr new_bs = chain.head_block_state();
   producer_plugin::produced_block_metrics metrics;
   br.total_time += fc::time_point::now() - start;

   ilog("Produced block ${id}... #${n} @ ${t} signed by ${p} "
        "[trxs: ${count}, lib: ${lib}, confirmed: ${confs}, net: ${net}, cpu: ${cpu}, elapsed: ${et}, time: ${tt}]",
        ("p", new_bs->header.producer)("id", new_bs->id.str().substr(8, 16))("n", new_bs->block_num)("t", new_bs->header.timestamp)
        ("count", new_bs->block->transactions.size())("lib", chain.last_irreversible_block_num())("net", br.total_net_usage)
        ("cpu", br.total_cpu_usage_us)("et", br.total_elapsed_time)("tt", br.total_time)("confs", new_bs->header.confirmed));

   _time_tracker.add_other_time();
   _time_tracker.report(new_bs->block_num, new_bs->block->producer, metrics);
   _time_tracker.clear();

   if (_update_produced_block_metrics) {
      metrics.unapplied_transactions_total = _unapplied_transactions.size();
      metrics.blacklisted_transactions_total = _blacklisted_transactions.size();
      metrics.subjective_bill_account_size_total = chain.get_subjective_billing().get_account_cache_size();
      metrics.scheduled_trxs_total = chain.db().get_index<generated_transaction_multi_index, by_delay>().size();
      metrics.trxs_produced_total = new_bs->block->transactions.size();
      metrics.cpu_usage_us = br.total_cpu_usage_us;
      metrics.total_elapsed_time_us = br.total_elapsed_time.count();
      metrics.total_time_us = br.total_time.count();
      metrics.net_usage_us = br.total_net_usage;
      metrics.last_irreversible = chain.last_irreversible_block_num();
      metrics.head_block_num = chain.head_block_num();
      _update_produced_block_metrics(metrics);
   }
}

void producer_plugin::received_block(uint32_t block_num) {
   my->_received_block = block_num;
}

void producer_plugin::log_failed_transaction(const transaction_id_type&    trx_id,
                                             const packed_transaction_ptr& packed_trx_ptr,
                                             const char*                   reason) const {
   fc_dlog(_trx_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${trx}",
           ("entire_trx", packed_trx_ptr ? my->chain_plug->get_log_trx(packed_trx_ptr->get_transaction()) : fc::variant{trx_id}));
   fc_dlog(_trx_failed_trace_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${txid} : ${why}", ("txid", trx_id)("why", reason));
   fc_dlog(_trx_trace_failure_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${entire_trx}",
           ("entire_trx", packed_trx_ptr ? my->chain_plug->get_log_trx(packed_trx_ptr->get_transaction()) : fc::variant{trx_id}));
}

// Called from only one read_only thread
void producer_plugin_impl::switch_to_write_window() {
   if (_log.is_enabled(fc::log_level::debug)) {
      auto now = fc::time_point::now();
      fc_dlog(_log, "Read-only threads ${n}, read window ${r}us, total all threads ${t}us",
              ("n", _ro_thread_pool_size)("r", now - _ro_read_window_start_time)("t", _ro_all_threads_exec_time_us.load()));
   }

   chain::controller& chain = chain_plug->chain();

   // this method can be called from multiple places. it is possible
   // we are already in write window.
   if (chain.is_write_window()) {
      return;
   }

   EOS_ASSERT(_ro_num_active_exec_tasks.load() == 0 && _ro_exec_tasks_fut.empty(), producer_exception,
              "no read-only tasks should be running before switching to write window");

   start_write_window();
}

// Called from app thread on plugin_startup
// Called from only one read_only thread & called from app thread, but not concurrently
void producer_plugin_impl::start_write_window() {
   chain::controller& chain = chain_plug->chain();

   app().executor().set_to_write_window();
   chain.set_to_write_window();
   chain.unset_db_read_only_mode();
   auto now = fc::time_point::now();
   _time_tracker.unpause(now);

   _ro_window_deadline = now + _ro_write_window_time_us; // not allowed on block producers, so no need to limit to block deadline
   auto expire_time = boost::posix_time::microseconds(_ro_write_window_time_us.count());
   _ro_timer.expires_from_now(expire_time);
   _ro_timer.async_wait(app().executor().wrap( // stay on app thread
      priority::high,
      exec_queue::read_write, // placed in read_write so only called from main thread
      [weak_this = weak_from_this()](const boost::system::error_code& ec) {
         auto self = weak_this.lock();
         if (self && ec != boost::asio::error::operation_aborted) {
            self->switch_to_read_window();
         }
      }));
}

// Called only from app thread
void producer_plugin_impl::switch_to_read_window() {
   chain::controller& chain = chain_plug->chain();
   EOS_ASSERT(chain.is_write_window(), producer_exception, "expected to be in write window");
   EOS_ASSERT(_ro_num_active_exec_tasks.load() == 0 && _ro_exec_tasks_fut.empty(), producer_exception, "_ro_exec_tasks_fut expected to be empty");

   _time_tracker.pause();

   // we are in write window, so no read-only trx threads are processing transactions.
   app().get_io_service().poll(); // make sure we schedule any ready
   if (app().executor().read_only_queue_empty() && app().executor().read_exclusive_queue_empty()) { // no read-only tasks to process. stay in write window
      start_write_window();                          // restart write window timer for next round
      return;
   }

   uint32_t pending_block_num = chain.head_block_num() + 1;
   _ro_read_window_start_time = fc::time_point::now();
   _ro_window_deadline        = _ro_read_window_start_time + _ro_read_window_effective_time_us;
   app().executor().set_to_read_window(
      _ro_thread_pool_size, [received_block = &_received_block, pending_block_num, ro_window_deadline = _ro_window_deadline]() {
         return fc::time_point::now() >= ro_window_deadline || (received_block->load() >= pending_block_num); // should_exit()
      });
   chain.set_to_read_window();
   chain.set_db_read_only_mode();
   _ro_all_threads_exec_time_us = 0;

   // start a read-only execution task in each thread in the thread pool
   _ro_num_active_exec_tasks = _ro_thread_pool_size;
   _ro_exec_tasks_fut.resize(0);
   for (uint32_t i = 0; i < _ro_thread_pool_size; ++i) {
      _ro_exec_tasks_fut.emplace_back(post_async_task(
         _ro_thread_pool.get_executor(), [self = this, pending_block_num]() { return self->read_only_execution_task(pending_block_num); }));
   }

   auto expire_time = boost::posix_time::microseconds(_ro_read_window_time_us.count());
   _ro_timer.expires_from_now(expire_time);
   // Needs to be on read_only because that is what is being processed until switch_to_write_window().
   _ro_timer.async_wait(
      app().executor().wrap(priority::high, exec_queue::read_only, [weak_this = weak_from_this()](const boost::system::error_code& ec) {
         auto self = weak_this.lock();
         if (self && ec != boost::asio::error::operation_aborted) {
            // use future to make sure all read-only tasks finished before switching to write window
            for (auto& task : self->_ro_exec_tasks_fut) {
               task.get();
            }
            self->_ro_exec_tasks_fut.clear();
            // will be executed from the main app thread because all read-only threads are idle now
            self->switch_to_write_window();
         } else if (self) {
            self->_ro_exec_tasks_fut.clear();
         }
      }));
}

// Called from a read only thread. Run in parallel with app and other read only threads
bool producer_plugin_impl::read_only_execution_task(uint32_t pending_block_num) {
   // We have 3 ways to break out the while loop:
   // 1. pass read window deadline
   // 2. net_plugin receives a block
   // 3. no read-only tasks to execute
   while (fc::time_point::now() < _ro_window_deadline && _received_block < pending_block_num) {
      bool more = app().executor().execute_highest_read(); // blocks until all read only threads are idle
      if (!more) {
         break;
      }
   }

   // If all tasks are finished, do not wait until end of read window; switch to write window now.
   if (--_ro_num_active_exec_tasks == 0) {
      // Needs to be on read_only because that is what is being processed until switch_to_write_window().
      app().executor().post(priority::high, exec_queue::read_only, [self = this]() {
         self->_ro_exec_tasks_fut.clear();
         // will be executed from the main app thread because all read-only threads are idle now
         self->switch_to_write_window();
      });
      // last thread post any exhausted back into read_only queue with slightly higher priority (low+1) so they are executed first
      ro_trx_t t;
      while (_ro_exhausted_trx_queue.pop_front(t)) {
         app().executor().post(priority::low + 1, exec_queue::read_exclusive, [this, trx{std::move(t.trx)}, next{std::move(t.next)}]() mutable {
            push_read_only_transaction(std::move(trx), std::move(next));
         });
      }
   }

   return true;
}

// Called from app thread during start block.
// Reschedule any exhausted read-only transactions from the last block
void producer_plugin_impl::repost_exhausted_transactions(const fc::time_point& deadline) {
   if (!_ro_exhausted_trx_queue.empty()) {
      chain::controller& chain             = chain_plug->chain();
      uint32_t           pending_block_num = chain.pending_block_num();
      // post any exhausted back into read_only queue with slightly higher priority (low+1) so they are executed first
      ro_trx_t t;
      while (!should_interrupt_start_block(deadline, pending_block_num) && _ro_exhausted_trx_queue.pop_front(t)) {
         app().executor().post(priority::low + 1, exec_queue::read_exclusive, [this, trx{std::move(t.trx)}, next{std::move(t.next)}]() mutable {
            push_read_only_transaction(std::move(trx), std::move(next));
         });
      }
   }
}

// Called from a read_only_trx execution thread, or from app thread when executing exclusively
// Return whether the trx needs to be retried in next read window
bool producer_plugin_impl::push_read_only_transaction(transaction_metadata_ptr trx, next_function<transaction_trace_ptr> next) {
   auto retry = false;

   try {
      auto               start = fc::time_point::now();
      chain::controller& chain = chain_plug->chain();
      if (!chain.is_building_block()) {
         _ro_exhausted_trx_queue.push_front({std::move(trx), std::move(next)});
         return true;
      }

      assert(!chain.is_write_window());

      // use read-window/write-window deadline
      auto window_deadline = _ro_window_deadline;

      // Ensure the trx to finish by the end of read-window or write-window or block_deadline depending on
      auto trace = chain.push_transaction(trx, window_deadline, _ro_max_trx_time_us, 0, false, 0);
      _ro_all_threads_exec_time_us += (fc::time_point::now() - start).count();
      auto pr = handle_push_result(trx, next, start, chain, trace,
                                   true, // return_failure_trace
                                   true, // disable_subjective_enforcement
                                   {},   // first_auth
                                   0,    // sub_bill
                                   0);   // prev_billed_cpu_time_us
      // If a transaction was exhausted, that indicates we are close to
      // the end of read window. Retry in next round.
      retry = pr.trx_exhausted;
      if (retry) {
         _ro_exhausted_trx_queue.push_front({std::move(trx), std::move(next)});
      }

   } catch (const guard_exception& e) {
      chain_plugin::handle_guard_exception(e);
   } catch (boost::interprocess::bad_alloc&) {
      chain_apis::api_base::handle_db_exhaustion();
   } catch (std::bad_alloc&) {
      chain_apis::api_base::handle_bad_alloc();
   }
   CATCH_AND_CALL(next);

   return retry;
}

const std::set<account_name>& producer_plugin::producer_accounts() const {
   return my->_producers;
}

void producer_plugin::register_update_produced_block_metrics(std::function<void(producer_plugin::produced_block_metrics)>&& fun) {
   my->_update_produced_block_metrics = std::move(fun);
}

void producer_plugin::register_update_speculative_block_metrics(std::function<void(speculative_block_metrics)> && fun) {
   my->_update_speculative_block_metrics = std::move(fun);
}

void producer_plugin::register_update_incoming_block_metrics(std::function<void(producer_plugin::incoming_block_metrics)>&& fun) {
   my->_update_incoming_block_metrics = std::move(fun);
}

} // namespace eosio
