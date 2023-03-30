#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/producer_plugin/pending_snapshot.hpp>
#include <eosio/producer_plugin/subjective_billing.hpp>
#include <eosio/producer_plugin/snapshot_scheduler.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/transaction_object.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/unapplied_transaction_queue.hpp>
#include <eosio/resource_monitor_plugin/resource_monitor_plugin.hpp>

#include <fc/io/json.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/scoped_exit.hpp>
#include <fc/time.hpp>

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <iostream>
#include <algorithm>
#include <mutex>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/signals2/connection.hpp>

namespace bmi = boost::multi_index;
using bmi::indexed_by;
using bmi::ordered_non_unique;
using bmi::member;
using bmi::tag;
using bmi::hashed_unique;

using boost::multi_index_container;

using std::string;
using std::vector;
using boost::signals2::scoped_connection;

#undef FC_LOG_AND_DROP
#define LOG_AND_DROP()  \
   catch ( const guard_exception& e ) { \
      chain_plugin::handle_guard_exception(e); \
   } catch ( const std::bad_alloc& ) { \
      chain_plugin::handle_bad_alloc(); \
   } catch ( boost::interprocess::bad_alloc& ) { \
      chain_plugin::handle_db_exhaustion(); \
   } catch( fc::exception& er ) { \
      wlog( "${details}", ("details",er.to_detail_string()) ); \
   } catch( const std::exception& e ) {  \
      fc::exception fce( \
                FC_LOG_MESSAGE( warn, "std::exception: ${what}: ",("what",e.what()) ), \
                fc::std_exception_code,\
                BOOST_CORE_TYPEID(e).name(), \
                e.what() ) ; \
      wlog( "${details}", ("details",fce.to_detail_string()) ); \
   } catch( ... ) {  \
      fc::unhandled_exception e( \
                FC_LOG_MESSAGE( warn, "unknown: ",  ), \
                std::current_exception() ); \
      wlog( "${details}", ("details",e.to_detail_string()) ); \
   }

const std::string logger_name("producer_plugin");
fc::logger _log;

const std::string trx_successful_trace_logger_name("transaction_success_tracing");
fc::logger       _trx_successful_trace_log;

const std::string trx_failed_trace_logger_name("transaction_failure_tracing");
fc::logger       _trx_failed_trace_log;

const std::string trx_trace_success_logger_name("transaction_trace_success");
fc::logger       _trx_trace_success_log;

const std::string trx_trace_failure_logger_name("transaction_trace_failure");
fc::logger       _trx_trace_failure_log;

const std::string trx_logger_name("transaction");
fc::logger       _trx_log;

const std::string transient_trx_successful_trace_logger_name("transient_trx_success_tracing");
fc::logger       _transient_trx_successful_trace_log;

const std::string transient_trx_failed_trace_logger_name("transient_trx_failure_tracing");
fc::logger       _transient_trx_failed_trace_log;

namespace eosio {

   static auto _producer_plugin = application::register_plugin<producer_plugin>();

using namespace eosio::chain;
using namespace eosio::chain::plugin_interface;

namespace {
   bool exception_is_exhausted(const fc::exception& e) {
      auto code = e.code();
      return (code == block_cpu_usage_exceeded::code_value) ||
             (code == block_net_usage_exceeded::code_value) ||
             (code == deadline_exception::code_value);
   }
}

struct transaction_id_with_expiry {
   transaction_id_type     trx_id;
   fc::time_point          expiry;
};

struct by_id;
struct by_expiry;

using transaction_id_with_expiry_index = multi_index_container<
   transaction_id_with_expiry,
   indexed_by<
      hashed_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER(transaction_id_with_expiry, transaction_id_type, trx_id)>,
      ordered_non_unique<tag<by_expiry>, BOOST_MULTI_INDEX_MEMBER(transaction_id_with_expiry, fc::time_point, expiry)>
   >
>;

struct by_height;

using pending_snapshot_index = multi_index_container<
   pending_snapshot,
   indexed_by<
      hashed_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER(pending_snapshot, block_id_type, block_id)>,
      ordered_non_unique<tag<by_height>, BOOST_MULTI_INDEX_CONST_MEM_FUN( pending_snapshot, uint32_t, get_height)>
   >
>;

enum class pending_block_mode {
   producing,
   speculating
};

namespace {

// track multiple failures on unapplied transactions
class account_failures {
public:

   //lifetime of sb must outlive account_failures
   explicit account_failures( const eosio::subjective_billing& sb )
   : subjective_billing(sb)
   {
   }

   void set_max_failures_per_account( uint32_t max_failures, uint32_t size ) {
      max_failures_per_account = max_failures;
      reset_window_size_in_num_blocks = size;
   }

   void add( const account_name& n, const fc::exception& e ) {
      auto& fa = failed_accounts[n];
      ++fa.num_failures;
      fa.add( n, e );
   }

   // return true if exceeds max_failures_per_account and should be dropped
   bool failure_limit( const account_name& n ) {
      auto fitr = failed_accounts.find( n );
      bool is_whitelisted = subjective_billing.is_account_disabled( n );
      if( !is_whitelisted && fitr != failed_accounts.end() && fitr->second.num_failures >= max_failures_per_account ) {
         ++fitr->second.num_failures;
         return true;
      }
      return false;
   }

   void report_and_clear(uint32_t block_num) {
      if (last_reset_block_num != block_num && (block_num % reset_window_size_in_num_blocks == 0) ) {
         report(block_num);
         failed_accounts.clear();
         last_reset_block_num = block_num;
      }
   }

   fc::time_point next_reset_timepoint(uint32_t current_block_num, fc::time_point current_block_time) const {
      auto num_blocks_to_reset = reset_window_size_in_num_blocks - (current_block_num % reset_window_size_in_num_blocks);
      return current_block_time + fc::milliseconds(num_blocks_to_reset * eosio::chain::config::block_interval_ms);
   }

private:
   void report(uint32_t block_num) const {
      if( _log.is_enabled(fc::log_level::debug)) {
         auto now = fc::time_point::now();
         for ( const auto& e : failed_accounts ) {
            std::string reason;
            if( e.second.is_deadline() ) reason += "deadline";
            if( e.second.is_tx_cpu_usage() ) {
               if( !reason.empty() ) reason += ", ";
               reason += "tx_cpu_usage";
            }
            if( e.second.is_eosio_assert() ) {
               if( !reason.empty() ) reason += ", ";
               reason += "assert";
            }
            if( e.second.is_other() ) {
               if( !reason.empty() ) reason += ", ";
               reason += "other";
            }
            fc_dlog( _log, "Failed ${n} trxs, account: ${a}, sub bill: ${b}us, reason: ${r}",
                     ("n", e.second.num_failures)("b", subjective_billing.get_subjective_bill(e.first, now))
                     ("a", e.first)("r", reason) );
         }
      }
   }
   struct account_failure {
      enum class ex_fields : uint8_t {
         ex_deadline_exception = 1,
         ex_tx_cpu_usage_exceeded = 2,
         ex_eosio_assert_exception = 4,
         ex_other_exception = 8
      };

      void add( const account_name& n, const fc::exception& e ) {
         auto exception_code = e.code();
         if( exception_code == tx_cpu_usage_exceeded::code_value ) {
            ex_flags = set_field( ex_flags, ex_fields::ex_tx_cpu_usage_exceeded );
         } else if( exception_code == deadline_exception::code_value ) {
            ex_flags = set_field( ex_flags, ex_fields::ex_deadline_exception );
         } else if( exception_code == eosio_assert_message_exception::code_value ||
                    exception_code == eosio_assert_code_exception::code_value ) {
            ex_flags = set_field( ex_flags, ex_fields::ex_eosio_assert_exception );
         } else {
            ex_flags = set_field( ex_flags, ex_fields::ex_other_exception );
            fc_dlog( _log, "Failed trx, account: ${a}, reason: ${r}, except: ${e}",
                     ("a", n)("r", exception_code)("e", e) );
         }
      }

      bool is_deadline() const { return has_field( ex_flags, ex_fields::ex_deadline_exception ); }
      bool is_tx_cpu_usage() const { return has_field( ex_flags, ex_fields::ex_tx_cpu_usage_exceeded ); }
      bool is_eosio_assert() const { return has_field( ex_flags, ex_fields::ex_eosio_assert_exception ); }
      bool is_other() const { return has_field( ex_flags, ex_fields::ex_other_exception ); }

      uint32_t num_failures = 0;
      uint8_t ex_flags = 0;
   };

   std::map<account_name, account_failure> failed_accounts;
   uint32_t max_failures_per_account = 3;
   uint32_t last_reset_block_num = 0;
   uint32_t reset_window_size_in_num_blocks = 1;
   const eosio::subjective_billing& subjective_billing;
};

struct block_time_tracker {

   void add_idle_time( const fc::microseconds& idle ) {
      block_idle_time += idle;
   }

   void add_fail_time( const fc::microseconds& fail_time, bool is_transient ) {
      if( is_transient ) {
         // transient time includes both success and fail time
         transient_trx_time += fail_time;
         ++transient_trx_num;
      } else {
         trx_fail_time += fail_time;
         ++trx_fail_num;
      }
   }

   void add_success_time( const fc::microseconds& time, bool is_transient ) {
      if( is_transient ) {
         transient_trx_time += time;
         ++transient_trx_num;
      } else {
         trx_success_time += time;
         ++trx_success_num;
      }
   }

   void report( const fc::time_point& idle_trx_time, uint32_t block_num ) {
      if( _log.is_enabled( fc::log_level::debug ) ) {
         auto now = fc::time_point::now();
         add_idle_time( now - idle_trx_time );
         fc_dlog( _log, "Block #${n} trx idle: ${i}us out of ${t}us, success: ${sn}, ${s}us, fail: ${fn}, ${f}us, transient: ${tn}, ${t}us, other: ${o}us",
                  ("n", block_num)
                  ("i", block_idle_time)("t", now - clear_time)("sn", trx_success_num)("s", trx_success_time)
                  ("fn", trx_fail_num)("f", trx_fail_time)
                  ("tn", transient_trx_num)("t", transient_trx_time)
                  ("o", (now - clear_time) - block_idle_time - trx_success_time - trx_fail_time - transient_trx_time) );
      }
   }

   void clear() {
      block_idle_time = trx_fail_time = trx_success_time = transient_trx_time = fc::microseconds{};
      trx_fail_num = trx_success_num = transient_trx_num = 0;
      clear_time = fc::time_point::now();
   }

   fc::microseconds block_idle_time;
   uint32_t trx_success_num = 0;
   uint32_t trx_fail_num = 0;
   uint32_t transient_trx_num = 0;
   fc::microseconds trx_success_time;
   fc::microseconds trx_fail_time;
   fc::microseconds transient_trx_time;
   fc::time_point clear_time{fc::time_point::now()};
};

} // anonymous namespace

class producer_plugin_impl : public std::enable_shared_from_this<producer_plugin_impl> {
   public:
      producer_plugin_impl(boost::asio::io_service& io)
      :_timer(io)
      ,_transaction_ack_channel(app().get_channel<compat::channels::transaction_ack>())
      ,_ro_timer(io)
      {
      }

      std::optional<fc::time_point> calculate_next_block_time(const account_name& producer_name, const block_timestamp_type& current_block_time) const;
      void schedule_production_loop();
      void schedule_maybe_produce_block( bool exhausted );
      void produce_block();
      bool maybe_produce_block();
      bool block_is_exhausted() const;
      bool remove_expired_trxs( const fc::time_point& deadline );
      bool remove_expired_blacklisted_trxs( const fc::time_point& deadline );
      bool process_unapplied_trxs( const fc::time_point& deadline );
      void process_scheduled_and_incoming_trxs( const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr );
      bool process_incoming_trxs( const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr );

      struct push_result {
         bool block_exhausted = false;
         bool trx_exhausted = false;
         bool failed = false;
      };
      push_result push_transaction( const fc::time_point& block_deadline,
                                    const transaction_metadata_ptr& trx,
                                    bool api_trx, bool return_failure_trace,
                                    const next_function<transaction_trace_ptr>& next );
      push_result handle_push_result( const transaction_metadata_ptr& trx,
                                      const next_function<transaction_trace_ptr>& next,
                                      const fc::time_point& start,
                                      const chain::controller& chain,
                                      const transaction_trace_ptr& trace,
                                      bool return_failure_trace,
                                      bool disable_subjective_enforcement,
                                      account_name first_auth,
                                      int64_t sub_bill,
                                      uint32_t prev_billed_cpu_time_us );
      void log_trx_results( const transaction_metadata_ptr& trx, const transaction_trace_ptr& trace, const fc::time_point& start );
      void log_trx_results( const transaction_metadata_ptr& trx, const fc::exception_ptr& except_ptr );
      void log_trx_results( const packed_transaction_ptr& trx, const transaction_trace_ptr& trace,
                            const fc::exception_ptr& except_ptr, uint32_t billed_cpu_us, const fc::time_point& start, bool is_transient );

      boost::program_options::variables_map _options;
      bool     _production_enabled                 = false;
      bool     _pause_production                   = false;

      using signature_provider_type = signature_provider_plugin::signature_provider_type;
      std::map<chain::public_key_type, signature_provider_type> _signature_providers;
      std::set<chain::account_name>                             _producers;
      boost::asio::deadline_timer                               _timer;
      using producer_watermark = std::pair<uint32_t, block_timestamp_type>;
      std::map<chain::account_name, producer_watermark>         _producer_watermarks;
      pending_block_mode                                        _pending_block_mode = pending_block_mode::speculating;
      unapplied_transaction_queue                               _unapplied_transactions;
      size_t                                                    _thread_pool_size = config::default_controller_thread_pool_size;
      named_thread_pool<struct prod>                            _thread_pool;

      std::atomic<int32_t>                                      _max_transaction_time_ms; // modified by app thread, read by net_plugin thread pool
      std::atomic<uint32_t>                                     _received_block{0}; // modified by net_plugin thread pool
      fc::microseconds                                          _max_irreversible_block_age_us;
      int32_t                                                   _produce_time_offset_us = 0;
      int32_t                                                   _last_block_time_offset_us = 0;
      uint32_t                                                  _max_block_cpu_usage_threshold_us = 0;
      uint32_t                                                  _max_block_net_usage_threshold_bytes = 0;
      int32_t                                                   _max_scheduled_transaction_time_per_block_ms = 0;
      bool                                                      _disable_subjective_p2p_billing = true;
      bool                                                      _disable_subjective_api_billing = true;
      fc::time_point                                            _irreversible_block_time;
      fc::time_point                                            _idle_trx_time{fc::time_point::now()};

      std::vector<chain::digest_type>                           _protocol_features_to_activate;
      bool                                                      _protocol_features_signaled = false; // to mark whether it has been signaled in start_block

      chain_plugin* chain_plug = nullptr;

      compat::channels::transaction_ack::channel_type&          _transaction_ack_channel;

      incoming::methods::block_sync::method_type::handle        _incoming_block_sync_provider;
      incoming::methods::transaction_async::method_type::handle _incoming_transaction_async_provider;

      transaction_id_with_expiry_index                         _blacklisted_transactions;
      pending_snapshot_index                                   _pending_snapshot_index;
      subjective_billing                                       _subjective_billing;
      account_failures                                         _account_fails{_subjective_billing};
      block_time_tracker                                       _time_tracker;

      std::optional<scoped_connection>                          _accepted_block_connection;
      std::optional<scoped_connection>                          _accepted_block_header_connection;
      std::optional<scoped_connection>                          _irreversible_block_connection;
      std::optional<scoped_connection>                          _block_start_connection;

      producer_plugin_metrics                                   _metrics;

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
      bfs::path _snapshots_dir;

      // async snapshot scheduler
      snapshot_scheduler _snapshot_scheduler;
      
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
         mutable std::mutex      mtx;
         std::deque<ro_trx_t>    queue;
      };

      uint16_t                        _ro_thread_pool_size{ 0 };
      static constexpr uint16_t       _ro_max_eos_vm_oc_threads_allowed{ 8 }; // Due to uncertainty to get total virtual memory size on a 5-level paging system, set a hard limit
      named_thread_pool<struct read>  _ro_thread_pool;
      fc::microseconds                _ro_write_window_time_us{ 200000 };
      fc::microseconds                _ro_read_window_time_us{ 60000 };
      static constexpr fc::microseconds _ro_read_window_minimum_time_us{ 10000 };
      fc::microseconds                _ro_read_window_effective_time_us{ 0 }; // calculated during option initialization
      std::atomic<int64_t>            _ro_all_threads_exec_time_us; // total time spent by all threads executing transactions. use atomic for simplicity and performance
      fc::time_point                  _ro_read_window_start_time;
      fc::time_point                  _ro_window_deadline; // only modified on app thread, read-window deadline or write-window deadline
      boost::asio::deadline_timer     _ro_timer;
      fc::microseconds                _ro_max_trx_time_us{ 0 }; // calculated during option initialization
      ro_trx_queue_t                  _ro_exhausted_trx_queue;
      std::atomic<uint32_t>           _ro_num_active_exec_tasks{ 0 };
      bool                            _ro_in_read_only_mode{false}; // only modified on app thread
      std::vector<std::future<bool>>  _ro_exec_tasks_fut;

      void start_write_window();
      void switch_to_write_window();
      void switch_to_read_window();
      bool read_only_execution_task(uint32_t pending_block_num);
      void repost_exhausted_transactions(const fc::time_point& deadline);
      bool push_read_only_transaction(transaction_metadata_ptr trx, next_function<transaction_trace_ptr> next);

      void consider_new_watermark( account_name producer, uint32_t block_num, block_timestamp_type timestamp) {
         auto itr = _producer_watermarks.find( producer );
         if( itr != _producer_watermarks.end() ) {
            itr->second.first = std::max( itr->second.first, block_num );
            itr->second.second = std::max( itr->second.second, timestamp );
         } else if( _producers.count( producer ) > 0 ) {
            _producer_watermarks.emplace( producer, std::make_pair(block_num, timestamp) );
         }
      }

      std::optional<producer_watermark> get_watermark( account_name producer ) const {
         auto itr = _producer_watermarks.find( producer );

         if( itr == _producer_watermarks.end() ) return {};

         return itr->second;
      }

      void on_block( const block_state_ptr& bsp ) {
         auto before = _unapplied_transactions.size();
         _unapplied_transactions.clear_applied( bsp );
         _subjective_billing.on_block( _log, bsp, fc::time_point::now() );
         if (before > 0) {
            fc_dlog( _log, "Removed applied transactions before: ${before}, after: ${after}",
                     ("before", before)("after", _unapplied_transactions.size()) );
         }
      }

      void on_block_header( const block_state_ptr& bsp ) {
         consider_new_watermark( bsp->header.producer, bsp->block_num, bsp->block->timestamp );
      }

      void on_irreversible_block( const signed_block_ptr& lib ) {
         _irreversible_block_time = lib->timestamp.to_time_point();
         const chain::controller& chain = chain_plug->chain();

         // promote any pending snapshots
         auto& snapshots_by_height = _pending_snapshot_index.get<by_height>();
         uint32_t lib_height = lib->block_num();

         while (!snapshots_by_height.empty() && snapshots_by_height.begin()->get_height() <= lib_height) {
            const auto& pending = snapshots_by_height.begin();
            auto next = pending->next;

            try {
               next(pending->finalize(chain));
            } CATCH_AND_CALL(next);

            snapshots_by_height.erase(snapshots_by_height.begin());
         }
      }

      void update_block_metrics() {
         if (_metrics.should_post()) {
            _metrics.unapplied_transactions.value = _unapplied_transactions.size();
            _metrics.subjective_bill_account_size.value = _subjective_billing.get_account_cache_size();
            _metrics.blacklisted_transactions.value = _blacklisted_transactions.size();
            _metrics.unapplied_transactions.value = _unapplied_transactions.size();

            auto &chain = chain_plug->chain();
            _metrics.last_irreversible.value = chain.last_irreversible_block_num();
            _metrics.head_block_num.value = chain.head_block_num();

            const auto& sch_idx = chain.db().get_index<generated_transaction_multi_index, by_delay>();
            _metrics.scheduled_trxs.value = sch_idx.size();

            _metrics.post_metrics();
         }
      }

      void abort_block() {
         auto& chain = chain_plug->chain();

         if( chain.is_building_block() ) {
            _time_tracker.report( _idle_trx_time, chain.pending_block_num() );
         }
         _unapplied_transactions.add_aborted( chain.abort_block() );
         _subjective_billing.abort_block();
         _idle_trx_time = fc::time_point::now();
      }

      bool on_incoming_block(const signed_block_ptr& block, const std::optional<block_id_type>& block_id, const block_state_ptr& bsp) {
         auto& chain = chain_plug->chain();
         if ( _pending_block_mode == pending_block_mode::producing ) {
            fc_wlog( _log, "dropped incoming block #${num} id: ${id}",
                     ("num", block->block_num())("id", block_id ? (*block_id).str() : "UNKNOWN") );
            return false;
         }

         // start a new speculative block, speculative start_block may have been interrupted
         auto ensure = fc::make_scoped_exit([this](){
            schedule_production_loop();
         });

         const auto& id = block_id ? *block_id : block->calculate_id();
         auto blk_num = block->block_num();

         auto now = fc::time_point::now();
         if (now - block->timestamp < fc::minutes(5) || (blk_num % 1000 == 0)) // only log every 1000 during sync
            fc_dlog(_log, "received incoming block ${n} ${id}", ("n", blk_num)("id", id));

         EOS_ASSERT( block->timestamp < (now + fc::seconds( 7 )), block_from_the_future,
                     "received a block from the future, ignoring it: ${id}", ("id", id) );

         /* de-dupe here... no point in aborting block if we already know the block */
         auto existing = chain.fetch_block_by_id( id );
         if( existing ) { return true; } // return true because the block is valid

         // start processing of block
         std::future<block_state_ptr> bsf;
         if( !bsp ) {
            bsf = chain.create_block_state_future( id, block );
         }

         // abort the pending block
         abort_block();

         // push the new block
         auto handle_error = [&](const auto& e)
         {
            elog((e.to_detail_string()));
            app().get_channel<channels::rejected_block>().publish( priority::medium, block );
            throw;
         };

         controller::block_report br;
         try {
            const block_state_ptr& bspr = bsp ? bsp : bsf.get();
            chain.push_block( br, bspr, [this]( const branch_type& forked_branch ) {
               _unapplied_transactions.add_forked( forked_branch );
            }, [this]( const transaction_id_type& id ) {
               return _unapplied_transactions.get_trx( id );
            } );
         } catch ( const guard_exception& e ) {
            chain_plugin::handle_guard_exception(e);
            return false;
         } catch ( const std::bad_alloc& ) {
            chain_plugin::handle_bad_alloc();
         } catch ( boost::interprocess::bad_alloc& ) {
            chain_plugin::handle_db_exhaustion();
         } catch ( const fork_database_exception& e ) {
            elog("Cannot recover from ${e}. Shutting down.", ("e", e.to_detail_string()));
            appbase::app().quit();
            return false;
         } catch( const fc::exception& e ) {
            handle_error(e);
         } catch (const std::exception& e) {
            handle_error(fc::std_exception_wrapper::from_current_exception(e));
         }

         const auto& hbs = chain.head_block_state();
         now = fc::time_point::now();
         if( hbs->header.timestamp.next().to_time_point() >= now ) {
            _production_enabled = true;
         }

         if( now - block->timestamp < fc::minutes(5) || (blk_num % 1000 == 0) ) {
            ilog("Received block ${id}... #${n} @ ${t} signed by ${p} "
                 "[trxs: ${count}, lib: ${lib}, confirmed: ${confs}, net: ${net}, cpu: ${cpu}, elapsed: ${elapsed}, time: ${time}, latency: ${latency} ms]",
                 ("p",block->producer)("id",id.str().substr(8,16))("n",blk_num)("t",block->timestamp)
                 ("count",block->transactions.size())("lib",chain.last_irreversible_block_num())
                 ("confs", block->confirmed)("net", br.total_net_usage)("cpu", br.total_cpu_usage_us)
                 ("elapsed", br.total_elapsed_time)("time", br.total_time)
                 ("latency", (now - block->timestamp).count()/1000 ) );
            if( chain.get_read_mode() != db_read_mode::IRREVERSIBLE && hbs->id != id && hbs->block != nullptr ) { // not applied to head
               ilog("Block not applied to head ${id}... #${n} @ ${t} signed by ${p} "
                    "[trxs: ${count}, dpos: ${dpos}, confirmed: ${confs}, net: ${net}, cpu: ${cpu}, elapsed: ${elapsed}, time: ${time}, latency: ${latency} ms]",
                    ("p",hbs->block->producer)("id",hbs->id.str().substr(8,16))("n",hbs->block_num)("t",hbs->block->timestamp)
                    ("count",hbs->block->transactions.size())("dpos", hbs->dpos_irreversible_blocknum)
                    ("confs", hbs->block->confirmed)("net", br.total_net_usage)("cpu", br.total_cpu_usage_us)
                    ("elapsed", br.total_elapsed_time)("time", br.total_time)
                    ("latency", (now - hbs->block->timestamp).count()/1000 ) );
            }
         }

         update_block_metrics();

         return true;
      }

      void restart_speculative_block() {
         // abort the pending block
         abort_block();

         schedule_production_loop();
      }

      void on_incoming_transaction_async(const packed_transaction_ptr& trx,
                                         bool api_trx,
                                         transaction_metadata::trx_type trx_type,
                                         bool return_failure_traces,
                                         next_function<transaction_trace_ptr> next) {
         if ( trx_type == transaction_metadata::trx_type::read_only ) {
            // Post all read only trxs to read_only queue for execution.
            auto trx_metadata = transaction_metadata::create_no_recover_keys( trx, transaction_metadata::trx_type::read_only );
            app().executor().post(priority::low, exec_queue::read_only, [this, trx{std::move(trx_metadata)}, next{std::move(next)}]() mutable {
               push_read_only_transaction( std::move(trx), std::move(next) );
            } );
            return;
         }

         chain::controller& chain = chain_plug->chain();
         const auto max_trx_time_ms = ( trx_type == transaction_metadata::trx_type::read_only ) ? -1 : _max_transaction_time_ms.load();
         fc::microseconds max_trx_cpu_usage = max_trx_time_ms < 0 ? fc::microseconds::maximum() : fc::milliseconds( max_trx_time_ms );

         auto future = transaction_metadata::start_recover_keys( trx, _thread_pool.get_executor(),
                                                                 chain.get_chain_id(), fc::microseconds( max_trx_cpu_usage ),
                                                                 trx_type,
                                                                 chain.configured_subjective_signature_length_limit() );

         auto is_transient = (trx_type == transaction_metadata::trx_type::read_only || trx_type == transaction_metadata::trx_type::dry_run);
         if( !is_transient ) {
            next = [this, trx, next{std::move(next)}]( const std::variant<fc::exception_ptr, transaction_trace_ptr>& response ) {
               next( response );

               fc::exception_ptr except_ptr; // rejected
               if( std::holds_alternative<fc::exception_ptr>( response ) ) {
                  except_ptr = std::get<fc::exception_ptr>( response );
               } else if( std::get<transaction_trace_ptr>( response )->except ) {
                  except_ptr = std::get<transaction_trace_ptr>( response )->except->dynamic_copy_exception();
               }

               _transaction_ack_channel.publish( priority::low, std::pair<fc::exception_ptr, packed_transaction_ptr>( except_ptr, trx ) );
            };
         }

         boost::asio::post(_thread_pool.get_executor(), [self = this, future{std::move(future)}, api_trx, is_transient, return_failure_traces,
                                                          next{std::move(next)}, trx=trx]() mutable {
            if( future.valid() ) {
               future.wait();
               app().executor().post( priority::low, exec_queue::read_write, [self, future{std::move(future)}, api_trx, is_transient, next{std::move( next )}, trx{std::move(trx)}, return_failure_traces]() mutable {
                  auto start = fc::time_point::now();
                  auto idle_time = start - self->_idle_trx_time;
                  self->_time_tracker.add_idle_time( idle_time );
                  fc_tlog( _log, "Time since last trx: ${t}us", ("t", idle_time) );

                  auto exception_handler = [self, is_transient, &next, trx{std::move(trx)}, &start](fc::exception_ptr ex) {
                     self->_time_tracker.add_idle_time( start - self->_idle_trx_time );
                     self->log_trx_results( trx, nullptr, ex, 0, start, is_transient );
                     next( std::move(ex) );
                     self->_idle_trx_time = fc::time_point::now();
                     auto dur = self->_idle_trx_time - start;
                     self->_time_tracker.add_fail_time(dur, is_transient);
                  };
                  try {
                     auto result = future.get();
                     if( !self->process_incoming_transaction_async( result, api_trx, return_failure_traces, next) ) {
                        if( self->_pending_block_mode == pending_block_mode::producing ) {
                           self->schedule_maybe_produce_block( true );
                        } else {
                           self->restart_speculative_block();
                        }
                     }
                     self->_idle_trx_time = fc::time_point::now();
                  } CATCH_AND_CALL(exception_handler);
               } );
            }
         });
      }

      bool process_incoming_transaction_async(const transaction_metadata_ptr& trx,
                                              bool api_trx,
                                              bool return_failure_trace,
                                              const next_function<transaction_trace_ptr>& next) {
         bool exhausted = false;
         chain::controller& chain = chain_plug->chain();
         try {
            const auto& id = trx->id();

            fc::time_point bt = chain.is_building_block() ? chain.pending_block_time() : chain.head_block_time();
            const fc::time_point expire = trx->packed_trx()->expiration();
            if( expire < bt ) {
               auto except_ptr = std::static_pointer_cast<fc::exception>(
                     std::make_shared<expired_tx_exception>(
                           FC_LOG_MESSAGE( error, "expired transaction ${id}, expiration ${e}, block time ${bt}",
                                           ("id", id)("e", expire)("bt", bt))));
               log_trx_results( trx, except_ptr );
               next( std::move(except_ptr) );
               return true;
            }

            if( chain.is_known_unexpired_transaction( id )) {
               auto except_ptr = std::static_pointer_cast<fc::exception>( std::make_shared<tx_duplicate>(
                     FC_LOG_MESSAGE( error, "duplicate transaction ${id}", ("id", id))));
               next( std::move(except_ptr) );
               return true;
            }

            if( !chain.is_building_block()) {
               _unapplied_transactions.add_incoming( trx, api_trx, return_failure_trace, next );
               return true;
            }

            const auto block_deadline = calculate_block_deadline( chain.pending_block_time() );
            push_result pr = push_transaction( block_deadline, trx, api_trx, return_failure_trace, next );

            exhausted = pr.block_exhausted;
            if( pr.trx_exhausted ) {
               _unapplied_transactions.add_incoming( trx, api_trx, return_failure_trace, next );
            }

         } catch ( const guard_exception& e ) {
            chain_plugin::handle_guard_exception(e);
         } catch ( boost::interprocess::bad_alloc& ) {
            chain_plugin::handle_db_exhaustion();
         } catch ( std::bad_alloc& ) {
            chain_plugin::handle_bad_alloc();
         } CATCH_AND_CALL(next);

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
         return !_production_enabled || _pause_production || (_max_irreversible_block_age_us.count() >= 0 && get_irreversible_block_age() >= _max_irreversible_block_age_us);
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

      fc::time_point calculate_pending_block_time() const;
      fc::time_point calculate_block_deadline( const fc::time_point& ) const;
      void schedule_delayed_production_loop(const std::weak_ptr<producer_plugin_impl>& weak_this, std::optional<fc::time_point> wake_up_time);
      std::optional<fc::time_point> calculate_producer_wake_up_time( const block_timestamp_type& ref_block_time ) const;

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
         ("max-transaction-time", bpo::value<int32_t>()->default_value(30),
          "Limits the maximum time (in milliseconds) that is allowed a pushed transaction's code to execute before being considered invalid")
         ("max-irreversible-block-age", bpo::value<int32_t>()->default_value( -1 ),
          "Limits the maximum age (in seconds) of the DPOS Irreversible Block for a chain this node will produce blocks on (use negative value to indicate unlimited)")
         ("producer-name,p", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "ID of producer controlled by this node (e.g. inita; may specify multiple times)")
         ("signature-provider", boost::program_options::value<vector<string>>()->composing()->multitoken()->default_value(
               {default_priv_key.get_public_key().to_string() + "=KEY:" + default_priv_key.to_string()},
                default_priv_key.get_public_key().to_string() + "=KEY:" + default_priv_key.to_string()),
               app().get_plugin<signature_provider_plugin>().signature_provider_help_text())
         ("greylist-account", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "account that can not access to extended CPU/NET virtual resources")
         ("greylist-limit", boost::program_options::value<uint32_t>()->default_value(1000),
          "Limit (between 1 and 1000) on the multiple that CPU/NET virtual resources can extend during low usage (only enforced subjectively; use 1000 to not enforce any limit)")
         ("produce-time-offset-us", boost::program_options::value<int32_t>()->default_value(0),
          "Offset of non last block producing time in microseconds. Valid range 0 .. -block_time_interval.")
         ("last-block-time-offset-us", boost::program_options::value<int32_t>()->default_value(-200000),
          "Offset of last block producing time in microseconds. Valid range 0 .. -block_time_interval.")
         ("cpu-effort-percent", bpo::value<uint32_t>()->default_value(config::default_block_cpu_effort_pct / config::percent_1),
          "Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80%")
         ("last-block-cpu-effort-percent", bpo::value<uint32_t>()->default_value(config::default_block_cpu_effort_pct / config::percent_1),
          "Percentage of cpu block production time used to produce last block. Whole number percentages, e.g. 80 for 80%")
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
         ("disable-subjective-billing", bpo::value<bool>()->default_value(true),
          "Disable subjective CPU billing for API/P2P transactions")
         ("disable-subjective-account-billing", boost::program_options::value<vector<string>>()->composing()->multitoken(),
          "Account which is excluded from subjective CPU billing")
         ("disable-subjective-p2p-billing", bpo::value<bool>()->default_value(true),
          "Disable subjective CPU billing for P2P transactions")
         ("disable-subjective-api-billing", bpo::value<bool>()->default_value(true),
          "Disable subjective CPU billing for API transactions")
         ("producer-threads", bpo::value<uint16_t>()->default_value(my->_thread_pool_size),
          "Number of worker threads in producer thread pool")
         ("snapshots-dir", bpo::value<bfs::path>()->default_value("snapshots"),
          "the location of the snapshots directory (absolute path or relative to application data dir)")
         ("read-only-threads", bpo::value<uint16_t>(),
          "Number of worker threads in read-only execution thread pool")
         ("read-only-write-window-time-us", bpo::value<uint32_t>()->default_value(my->_ro_write_window_time_us.count()),
          "time in microseconds the write window lasts")
         ("read-only-read-window-time-us", bpo::value<uint32_t>()->default_value(my->_ro_read_window_time_us.count()),
          "time in microseconds the read window lasts")
         ;
   config_file_options.add(producer_options);
}

bool producer_plugin::is_producer_key(const chain::public_key_type& key) const
{
  auto private_key_itr = my->_signature_providers.find(key);
  if(private_key_itr != my->_signature_providers.end())
    return true;
  return false;
}

int64_t producer_plugin::get_subjective_bill( const account_name& first_auth, const fc::time_point& now ) const
{
   return my->_subjective_billing.get_subjective_bill( first_auth, now );
}

chain::signature_type producer_plugin::sign_compact(const chain::public_key_type& key, const fc::sha256& digest) const
{
  if(key != chain::public_key_type()) {
    auto private_key_itr = my->_signature_providers.find(key);
    EOS_ASSERT(private_key_itr != my->_signature_providers.end(), producer_priv_key_not_found, "Local producer has no private key in config.ini corresponding to public key ${key}", ("key", key));

    return private_key_itr->second(digest);
  }
  else {
    return chain::signature_type();
  }
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

void producer_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {
   handle_sighup(); // Sets loggers

   my->chain_plug = app().find_plugin<chain_plugin>();
   EOS_ASSERT( my->chain_plug, plugin_config_exception, "chain_plugin not found" );
   my->_options = &options;
   LOAD_VALUE_SET(options, "producer-name", my->_producers)

   chain::controller& chain = my->chain_plug->chain();

   if( options.count("signature-provider") ) {
      const std::vector<std::string> key_spec_pairs = options["signature-provider"].as<std::vector<std::string>>();
      for (const auto& key_spec_pair : key_spec_pairs) {
         try {
            const auto& [pubkey, provider] = app().get_plugin<signature_provider_plugin>().signature_provider_for_specification(key_spec_pair);
            my->_signature_providers[pubkey] = provider;
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
   EOS_ASSERT( subjective_account_max_failures_window_size > 0, plugin_config_exception,
               "subjective-account-max-failures-window-size ${s} must be greater than 0", ("s", subjective_account_max_failures_window_size) );

   my->_account_fails.set_max_failures_per_account( options.at("subjective-account-max-failures").as<uint32_t>(),
                                                    subjective_account_max_failures_window_size );

   my->_produce_time_offset_us = options.at("produce-time-offset-us").as<int32_t>();
   EOS_ASSERT( my->_produce_time_offset_us <= 0 && my->_produce_time_offset_us >= -config::block_interval_us, plugin_config_exception,
               "produce-time-offset-us ${o} must be 0 .. -${bi}", ("bi", config::block_interval_us)("o", my->_produce_time_offset_us) );

   my->_last_block_time_offset_us = options.at("last-block-time-offset-us").as<int32_t>();
   EOS_ASSERT( my->_last_block_time_offset_us <= 0 && my->_last_block_time_offset_us >= -config::block_interval_us, plugin_config_exception,
               "last-block-time-offset-us ${o} must be 0 .. -${bi}", ("bi", config::block_interval_us)("o", my->_last_block_time_offset_us) );

   uint32_t cpu_effort_pct = options.at("cpu-effort-percent").as<uint32_t>();
   EOS_ASSERT( cpu_effort_pct >= 0 && cpu_effort_pct <= 100, plugin_config_exception,
               "cpu-effort-percent ${pct} must be 0 - 100", ("pct", cpu_effort_pct) );
      cpu_effort_pct *= config::percent_1;
   int32_t cpu_effort_offset_us =
         -EOS_PERCENT( config::block_interval_us, chain::config::percent_100 - cpu_effort_pct );

   uint32_t last_block_cpu_effort_pct = options.at("last-block-cpu-effort-percent").as<uint32_t>();
   EOS_ASSERT( last_block_cpu_effort_pct >= 0 && last_block_cpu_effort_pct <= 100, plugin_config_exception,
               "last-block-cpu-effort-percent ${pct} must be 0 - 100", ("pct", last_block_cpu_effort_pct) );
      last_block_cpu_effort_pct *= config::percent_1;
   int32_t last_block_cpu_effort_offset_us =
         -EOS_PERCENT( config::block_interval_us, chain::config::percent_100 - last_block_cpu_effort_pct );

   my->_produce_time_offset_us = std::min( my->_produce_time_offset_us, cpu_effort_offset_us );
   my->_last_block_time_offset_us = std::min( my->_last_block_time_offset_us, last_block_cpu_effort_offset_us );

   my->_max_block_cpu_usage_threshold_us = options.at( "max-block-cpu-usage-threshold-us" ).as<uint32_t>();
   EOS_ASSERT( my->_max_block_cpu_usage_threshold_us < config::block_interval_us, plugin_config_exception,
               "max-block-cpu-usage-threshold-us ${t} must be 0 .. ${bi}", ("bi", config::block_interval_us)("t", my->_max_block_cpu_usage_threshold_us) );

   my->_max_block_net_usage_threshold_bytes = options.at( "max-block-net-usage-threshold-bytes" ).as<uint32_t>();

   my->_max_scheduled_transaction_time_per_block_ms = options.at("max-scheduled-transaction-time-per-block-ms").as<int32_t>();

   if( options.at( "subjective-cpu-leeway-us" ).as<int32_t>() != config::default_subjective_cpu_leeway_us ) {
      chain.set_subjective_cpu_leeway( fc::microseconds( options.at( "subjective-cpu-leeway-us" ).as<int32_t>() ) );
   }

   fc::microseconds subjective_account_decay_time = fc::minutes(options.at( "subjective-account-decay-time-minutes" ).as<uint32_t>());
   EOS_ASSERT( subjective_account_decay_time.count() > 0, plugin_config_exception,
               "subjective-account-decay-time-minutes ${dt} must be greater than 0", ("dt", subjective_account_decay_time.to_seconds() / 60));
   my->_subjective_billing.set_expired_accumulator_average_window( subjective_account_decay_time );

   my->_max_transaction_time_ms = options.at("max-transaction-time").as<int32_t>();

   my->_max_irreversible_block_age_us = fc::seconds(options.at("max-irreversible-block-age").as<int32_t>());

   auto max_incoming_transaction_queue_size = options.at("incoming-transaction-queue-size-mb").as<uint16_t>() * 1024*1024;

   EOS_ASSERT( max_incoming_transaction_queue_size > 0, plugin_config_exception,
               "incoming-transaction-queue-size-mb ${mb} must be greater than 0", ("mb", max_incoming_transaction_queue_size) );

   my->_unapplied_transactions.set_max_transaction_queue_size( max_incoming_transaction_queue_size );

   my->_incoming_defer_ratio = options.at("incoming-defer-ratio").as<double>();

   bool disable_subjective_billing = options.at("disable-subjective-billing").as<bool>();
   my->_disable_subjective_p2p_billing = options.at("disable-subjective-p2p-billing").as<bool>();
   my->_disable_subjective_api_billing = options.at("disable-subjective-api-billing").as<bool>();
   dlog( "disable-subjective-billing: ${s}, disable-subjective-p2p-billing: ${p2p}, disable-subjective-api-billing: ${api}",
         ("s", disable_subjective_billing)("p2p", my->_disable_subjective_p2p_billing)("api", my->_disable_subjective_api_billing) );
   if( !disable_subjective_billing ) {
       my->_disable_subjective_p2p_billing = my->_disable_subjective_api_billing = false;
   } else if( !my->_disable_subjective_p2p_billing || !my->_disable_subjective_api_billing ) {
       disable_subjective_billing = false;
   }
   if( disable_subjective_billing ) {
       my->_subjective_billing.disable();
       ilog( "Subjective CPU billing disabled" );
   } else if( !my->_disable_subjective_p2p_billing && !my->_disable_subjective_api_billing ) {
       ilog( "Subjective CPU billing enabled" );
   } else {
       if( my->_disable_subjective_p2p_billing ) ilog( "Subjective CPU billing of P2P trxs disabled " );
       if( my->_disable_subjective_api_billing ) ilog( "Subjective CPU billing of API trxs disabled " );
   }

   my->_thread_pool_size = options.at( "producer-threads" ).as<uint16_t>();
   EOS_ASSERT( my->_thread_pool_size > 0, plugin_config_exception,
               "producer-threads ${num} must be greater than 0", ("num", my->_thread_pool_size));

   if( options.count( "snapshots-dir" )) {
      auto sd = options.at( "snapshots-dir" ).as<bfs::path>();
      if( sd.is_relative()) {
         my->_snapshots_dir = app().data_dir() / sd;
         if (!fc::exists(my->_snapshots_dir)) {
            fc::create_directories(my->_snapshots_dir);
         }
      } else {
         my->_snapshots_dir = sd;
      }

      EOS_ASSERT( fc::is_directory(my->_snapshots_dir), snapshot_directory_not_found_exception,
                  "No such directory '${dir}'", ("dir", my->_snapshots_dir.generic_string()) );

      if (auto resmon_plugin = app().find_plugin<resource_monitor_plugin>()) {
         resmon_plugin->monitor_directory(my->_snapshots_dir);
      }
   }

   if ( options.count( "read-only-threads" ) ) {
      my->_ro_thread_pool_size = options.at( "read-only-threads" ).as<uint16_t>();
   } else if ( my->_producers.empty() ) {
      if( options.count( "plugin" ) ) {
         const auto& v = options.at( "plugin" ).as<std::vector<std::string>>();
         auto i = std::find_if( v.cbegin(), v.cend(), []( const std::string& p ) { return p == "eosio::chain_api_plugin"; } );
         if( i != v.cend() ) {
            // default to 3 threads for non producer nodes running chain_api_plugin if not specified
            my->_ro_thread_pool_size = 3;
            ilog( "chain_api_plugin configured, defaulting read-only-threads to ${t}", ("t", my->_ro_thread_pool_size) );
         }
      }
   }
   EOS_ASSERT( test_mode_ || my->_ro_thread_pool_size == 0 || my->_producers.empty(), plugin_config_exception, "--read-only-threads not allowed on producer node" );
   // only initialize other read-only options when read-only thread pool is enabled
   if ( my->_ro_thread_pool_size > 0 ) {
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
      if (chain.is_eos_vm_oc_enabled()) {
         // EOS VM OC requires 4.2TB Virtual for each executing thread. Make sure the memory
         // required by configured read-only threads does not exceed the total system virtual memory.
         std::string attr_name;
         size_t vm_total_kb { 0 };
         size_t vm_used_kb { 0 };
         std::ifstream meminfo_file("/proc/meminfo");
         while (meminfo_file >> attr_name) {
            if (attr_name == "VmallocTotal:") {
               if ( !(meminfo_file >> vm_total_kb) )
                  break;
            } else if (attr_name == "VmallocUsed:") {
               if ( !(meminfo_file >> vm_used_kb) )
                  break;
            }
            meminfo_file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
         }

         EOS_ASSERT( vm_total_kb > 0, plugin_config_exception, "Unable to get system virtual memory size (not a Linux?), therefore cannot determine if the system has enough virtual memory for multi-threaded read-only transactions on EOS VM OC");
         // reserve 1 for the app thread, 1 for anything else which might use VM
         int num_threads_supported = (vm_total_kb - vm_used_kb) / 4200000000 - 2;
         ilog("vm total in kb: ${total}, vm used in kb: ${used}, number of EOS VM OC threads supported ((vm total - vm used)/4.2 TB - 2): ${supp}", ("total", vm_total_kb) ("used", vm_used_kb) ("supp", num_threads_supported));
         EOS_ASSERT( num_threads_supported >= my->_ro_thread_pool_size, plugin_config_exception, "--read-only-threads (${th}) greater than number of threads supported for EOS VM OC (${supp}) by the system virtual memory size", ("th", my->_ro_thread_pool_size) ("supp", num_threads_supported) );

         if ( my->_ro_thread_pool_size > my->_ro_max_eos_vm_oc_threads_allowed ) {
            wlog("--read-only-threads (${th}) greater than maximum number of threads allowed (${allowed}) for EOS Vm OC. Set it to ${allowed}", ("th", my->_ro_thread_pool_size) ("allowed", my->_ro_max_eos_vm_oc_threads_allowed));
            my->_ro_thread_pool_size = my->_ro_max_eos_vm_oc_threads_allowed;
         }
      }
#endif
   }

   my->_ro_write_window_time_us = fc::microseconds( options.at( "read-only-write-window-time-us" ).as<uint32_t>() );
   my->_ro_read_window_time_us = fc::microseconds( options.at( "read-only-read-window-time-us" ).as<uint32_t>() );
   EOS_ASSERT( my->_ro_read_window_time_us > my->_ro_read_window_minimum_time_us, plugin_config_exception, "minimum of --read-only-read-window-time-us (${read}) must be ${min} microseconds", ("read", my->_ro_read_window_time_us) ("min", my->_ro_read_window_minimum_time_us) );
   my->_ro_read_window_effective_time_us = my->_ro_read_window_time_us - my->_ro_read_window_minimum_time_us;

   // Make sure a read-only transaction can finish within the read
   // window if scheduled at the very beginning of the window.
   // Use _ro_read_window_effective_time_us instead of _ro_read_window_time_us
   // for safety margin
   if ( my->_max_transaction_time_ms.load() > 0 ) {
      EOS_ASSERT( my->_ro_read_window_effective_time_us > fc::milliseconds(my->_max_transaction_time_ms.load()), plugin_config_exception, "--read-only-read-window-time-us (${read}) must be greater than --max-transaction-time ${trx_time} ms plus a margin of ${min} us", ("read", my->_ro_read_window_time_us) ("trx_time", my->_max_transaction_time_ms.load()) ("min", my->_ro_read_window_minimum_time_us) );
      my->_ro_max_trx_time_us = fc::milliseconds(my->_max_transaction_time_ms.load());
   } else {
      // _max_transaction_time_ms can be set to negative in testing (for unlimited)
      my->_ro_max_trx_time_us = my->_ro_read_window_effective_time_us;
   }
   ilog("ro_thread_pool_size ${s}, ro_write_window_time_us ${ww}, ro_read_window_time_us ${rw}, ro_max_trx_time_us ${t}, ro_read_window_effective_time_us ${w}",
        ("s", my->_ro_thread_pool_size)("ww", my->_ro_write_window_time_us)("rw", my->_ro_read_window_time_us)("t", my->_ro_max_trx_time_us)("w", my->_ro_read_window_effective_time_us));

   my->_incoming_block_sync_provider = app().get_method<incoming::methods::block_sync>().register_provider(
         [this](const signed_block_ptr& block, const std::optional<block_id_type>& block_id, const block_state_ptr& bsp) {
      return my->on_incoming_block(block, block_id, bsp);
   });

   my->_incoming_transaction_async_provider = app().get_method<incoming::methods::transaction_async>().register_provider(
         [this](const packed_transaction_ptr& trx, bool api_trx, transaction_metadata::trx_type trx_type, bool return_failure_traces, next_function<transaction_trace_ptr> next) -> void {
      return my->on_incoming_transaction_async(trx, api_trx, trx_type, return_failure_traces, next );
   });

   if (options.count("greylist-account")) {
      std::vector<std::string> greylist = options["greylist-account"].as<std::vector<std::string>>();
      greylist_params param;
      for (auto &a : greylist) {
         param.accounts.push_back(account_name(a));
      }
      add_greylist_accounts(param);
   }

   {
      uint32_t greylist_limit = options.at("greylist-limit").as<uint32_t>();
      chain.set_greylist_limit( greylist_limit );
   }

   if( options.count("disable-subjective-account-billing") ) {
      std::vector<std::string> accounts = options["disable-subjective-account-billing"].as<std::vector<std::string>>();
      for( const auto& a : accounts ) {
         my->_subjective_billing.disable_account( account_name(a) );
      }
   }

   my->_snapshot_scheduler.set_db_path(my->_snapshots_dir);
   my->_snapshot_scheduler.set_create_snapshot_fn([this](producer_plugin::next_function<producer_plugin::snapshot_information> next){create_snapshot(next);});
} FC_LOG_AND_RETHROW() }

void producer_plugin::plugin_startup()
{ try {
   try {
   ilog("producer plugin:  plugin_startup() begin");

   my->_thread_pool.start( my->_thread_pool_size, []( const fc::exception& e ) {
      fc_elog( _log, "Exception in producer thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
      app().quit();
   } );


   chain::controller& chain = my->chain_plug->chain();
   EOS_ASSERT( my->_producers.empty() || chain.get_read_mode() != chain::db_read_mode::IRREVERSIBLE, plugin_config_exception,
              "node cannot have any producer-name configured because block production is impossible when read_mode is \"irreversible\"" );

   EOS_ASSERT( my->_producers.empty() || chain.get_validation_mode() == chain::validation_mode::FULL, plugin_config_exception,
              "node cannot have any producer-name configured because block production is not safe when validation_mode is not \"full\"" );

   EOS_ASSERT( my->_producers.empty() || my->chain_plug->accept_transactions(), plugin_config_exception,
              "node cannot have any producer-name configured because no block production is possible with no [api|p2p]-accepted-transactions" );

   my->_accepted_block_connection.emplace(chain.accepted_block.connect( [this]( const auto& bsp ){ my->on_block( bsp ); } ));
   my->_accepted_block_header_connection.emplace(chain.accepted_block_header.connect( [this]( const auto& bsp ){ my->on_block_header( bsp ); } ));
   my->_irreversible_block_connection.emplace(chain.irreversible_block.connect( [this]( const auto& bsp ){ my->on_irreversible_block( bsp->block ); } ));
   my->_block_start_connection.emplace(chain.block_start.connect( [this]( uint32_t bs ){ my->_snapshot_scheduler.on_start_block(bs); } ));

   const auto lib_num = chain.last_irreversible_block_num();
   const auto lib = chain.fetch_block_by_number(lib_num);
   if (lib) {
      my->on_irreversible_block(lib);
   } else {
      my->_irreversible_block_time = fc::time_point::maximum();
   }

   if (!my->_producers.empty()) {
      ilog("Launching block production for ${n} producers at ${time}.", ("n", my->_producers.size())("time",fc::time_point::now()));

      if (my->_production_enabled) {
         if (chain.head_block_num() == 0) {
            new_chain_banner(chain);
         }
      }
   }

   if ( my->_ro_thread_pool_size > 0 ) {
      my->_ro_thread_pool.start( my->_ro_thread_pool_size,
         []( const fc::exception& e ) {
            fc_elog( _log, "Exception in read-only thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
            app().quit();
         },
         [&]() {
            chain.init_thread_local_data();
         });

      my->start_write_window();
   }

   my->schedule_production_loop();

   ilog("producer plugin:  plugin_startup() end");
   } catch( ... ) {
      // always call plugin_shutdown, even on exception
      plugin_shutdown();
      throw;
   }
} FC_CAPTURE_AND_RETHROW() }

void producer_plugin::plugin_shutdown() {
   try {
      my->_timer.cancel();
   } catch ( const std::bad_alloc& ) {
     chain_plugin::handle_bad_alloc();
   } catch ( const boost::interprocess::bad_alloc& ) {
     chain_plugin::handle_bad_alloc();
   } catch(const fc::exception& e) {
      edump((e.to_detail_string()));
   } catch(const std::exception& e) {
      edump((fc::std_exception_wrapper::from_current_exception(e).to_detail_string()));
   }

   my->_thread_pool.stop();

   my->_unapplied_transactions.clear();

   app().executor().post( 0, [me = my](){} ); // keep my pointer alive until queue is drained
   fc_ilog(_log, "exit shutdown");
}

void producer_plugin::handle_sighup() {
   fc::logger::update( logger_name, _log );
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
   my->_pause_production = false;
   // it is possible that we are only speculating because of this policy which we have now changed
   // re-evaluate that now
   //
   if (my->_pending_block_mode == pending_block_mode::speculating) {
      my->abort_block();
      fc_ilog(_log, "Producer resumed. Scheduling production.");
      my->schedule_production_loop();
   } else {
      fc_ilog(_log, "Producer resumed.");
   }
}

bool producer_plugin::paused() const {
   return my->_pause_production;
}

void producer_plugin::update_runtime_options(const runtime_options& options) {
   chain::controller& chain = my->chain_plug->chain();
   bool check_speculating = false;

   if (options.max_transaction_time) {
      my->_max_transaction_time_ms = *options.max_transaction_time;
   }

   if (options.max_irreversible_block_age) {
      my->_max_irreversible_block_age_us =  fc::seconds(*options.max_irreversible_block_age);
      check_speculating = true;
   }

   if (options.produce_time_offset_us) {
      my->_produce_time_offset_us = *options.produce_time_offset_us;
   }

   if (options.last_block_time_offset_us) {
      my->_last_block_time_offset_us = *options.last_block_time_offset_us;
   }

   if (options.max_scheduled_transaction_time_per_block_ms) {
      my->_max_scheduled_transaction_time_per_block_ms = *options.max_scheduled_transaction_time_per_block_ms;
   }

   if (options.incoming_defer_ratio) {
      my->_incoming_defer_ratio = *options.incoming_defer_ratio;
   }

   if (check_speculating && my->_pending_block_mode == pending_block_mode::speculating) {
      my->abort_block();
      my->schedule_production_loop();
   }

   if (options.subjective_cpu_leeway_us) {
      chain.set_subjective_cpu_leeway(fc::microseconds(*options.subjective_cpu_leeway_us));
   }

   if (options.greylist_limit) {
      chain.set_greylist_limit(*options.greylist_limit);
   }
}

producer_plugin::runtime_options producer_plugin::get_runtime_options() const {
   return {
      my->_max_transaction_time_ms,
      my->_max_irreversible_block_age_us.count() < 0 ? -1 : my->_max_irreversible_block_age_us.count() / 1'000'000,
      my->_produce_time_offset_us,
      my->_last_block_time_offset_us,
      my->_max_scheduled_transaction_time_per_block_ms,
      my->chain_plug->chain().get_subjective_cpu_leeway() ?
            my->chain_plug->chain().get_subjective_cpu_leeway()->count() :
            std::optional<int32_t>(),
      my->_incoming_defer_ratio,
      my->chain_plug->chain().get_greylist_limit()
   };
}

void producer_plugin::add_greylist_accounts(const greylist_params& params) {
   EOS_ASSERT(params.accounts.size() > 0, chain::invalid_http_request, "At least one account is required");

   chain::controller& chain = my->chain_plug->chain();
   for (auto &acc : params.accounts) {
      chain.add_resource_greylist(acc);
   }
}

void producer_plugin::remove_greylist_accounts(const greylist_params& params) {
   EOS_ASSERT(params.accounts.size() > 0, chain::invalid_http_request, "At least one account is required");

   chain::controller& chain = my->chain_plug->chain();
   for (auto &acc : params.accounts) {
      chain.remove_resource_greylist(acc);
   }
}

producer_plugin::greylist_params producer_plugin::get_greylist() const {
   chain::controller& chain = my->chain_plug->chain();
   greylist_params result;
   const auto& list = chain.get_resource_greylist();
   result.accounts.reserve(list.size());
   for (auto &acc: list) {
      result.accounts.push_back(acc);
   }
   return result;
}

producer_plugin::whitelist_blacklist producer_plugin::get_whitelist_blacklist() const {
   chain::controller& chain = my->chain_plug->chain();
   return {
      chain.get_actor_whitelist(),
      chain.get_actor_blacklist(),
      chain.get_contract_whitelist(),
      chain.get_contract_blacklist(),
      chain.get_action_blacklist(),
      chain.get_key_blacklist()
   };
}

void producer_plugin::set_whitelist_blacklist(const producer_plugin::whitelist_blacklist& params) {
   EOS_ASSERT(params.actor_whitelist || params.actor_blacklist || params.contract_whitelist || params.contract_blacklist || params.action_blacklist || params.key_blacklist,
              chain::invalid_http_request,
              "At least one of actor_whitelist, actor_blacklist, contract_whitelist, contract_blacklist, action_blacklist, and key_blacklist is required"
             );

   chain::controller& chain = my->chain_plug->chain();
   if(params.actor_whitelist) chain.set_actor_whitelist(*params.actor_whitelist);
   if(params.actor_blacklist) chain.set_actor_blacklist(*params.actor_blacklist);
   if(params.contract_whitelist) chain.set_contract_whitelist(*params.contract_whitelist);
   if(params.contract_blacklist) chain.set_contract_blacklist(*params.contract_blacklist);
   if(params.action_blacklist) chain.set_action_blacklist(*params.action_blacklist);
   if(params.key_blacklist) chain.set_key_blacklist(*params.key_blacklist);
}

producer_plugin::integrity_hash_information producer_plugin::get_integrity_hash() const {
   chain::controller& chain = my->chain_plug->chain();

   auto reschedule = fc::make_scoped_exit([this](){
      my->schedule_production_loop();
   });

   if (chain.is_building_block()) {
      // abort the pending block
      my->abort_block();
   } else {
      reschedule.cancel();
   }

   return {chain.head_block_id(), chain.calculate_integrity_hash()};
}

void producer_plugin::create_snapshot(producer_plugin::next_function<producer_plugin::snapshot_information> next) {
   chain::controller& chain = my->chain_plug->chain();

   auto head_id = chain.head_block_id();
   const auto head_block_num = chain.head_block_num();
   const auto head_block_time = chain.head_block_time();
   const auto& snapshot_path = pending_snapshot::get_final_path(head_id, my->_snapshots_dir);
   const auto& temp_path     = pending_snapshot::get_temp_path(head_id, my->_snapshots_dir);

   // maintain legacy exception if the snapshot exists
   if( fc::is_regular_file(snapshot_path) ) {
      auto ex = snapshot_exists_exception( FC_LOG_MESSAGE( error, "snapshot named ${name} already exists", ("name", snapshot_path.generic_string()) ) );
      next(ex.dynamic_copy_exception());
      return;
   }

   auto write_snapshot = [&]( const bfs::path& p ) -> void {
      auto reschedule = fc::make_scoped_exit([this](){
         my->schedule_production_loop();
      });

      if (chain.is_building_block()) {
         // abort the pending block
         my->abort_block();
      } else {
         reschedule.cancel();
      }

      bfs::create_directory( p.parent_path() );

      // create the snapshot
      auto snap_out = std::ofstream(p.generic_string(), (std::ios::out | std::ios::binary));
      auto writer = std::make_shared<ostream_snapshot_writer>(snap_out);
      chain.write_snapshot(writer);
      writer->finalize();
      snap_out.flush();
      snap_out.close();
   };

   // If in irreversible mode, create snapshot and return path to snapshot immediately.
   if( chain.get_read_mode() == db_read_mode::IRREVERSIBLE ) {
      try {
         write_snapshot( temp_path );

         boost::system::error_code ec;
         bfs::rename(temp_path, snapshot_path, ec);
         EOS_ASSERT(!ec, snapshot_finalization_exception,
               "Unable to finalize valid snapshot of block number ${bn}: [code: ${ec}] ${message}",
               ("bn", head_block_num)
               ("ec", ec.value())
               ("message", ec.message()));

         next( producer_plugin::snapshot_information{head_id, head_block_num, head_block_time, chain_snapshot_header::current_version, snapshot_path.generic_string()} );
      } CATCH_AND_CALL (next);
      return;
   }

   // Otherwise, the result will be returned when the snapshot becomes irreversible.

   // determine if this snapshot is already in-flight
   auto& pending_by_id = my->_pending_snapshot_index.get<by_id>();
   auto existing = pending_by_id.find(head_id);
   if( existing != pending_by_id.end() ) {
      // if a snapshot at this block is already pending, attach this requests handler to it
      pending_by_id.modify(existing, [&next]( auto& entry ){
         entry.next = [prev = entry.next, next](const std::variant<fc::exception_ptr, producer_plugin::snapshot_information>& res){
            prev(res);
            next(res);
         };
      });
   } else {
      const auto& pending_path = pending_snapshot::get_pending_path(head_id, my->_snapshots_dir);

      try {
         write_snapshot( temp_path ); // create a new pending snapshot

         boost::system::error_code ec;
         bfs::rename(temp_path, pending_path, ec);
         EOS_ASSERT(!ec, snapshot_finalization_exception,
               "Unable to promote temp snapshot to pending for block number ${bn}: [code: ${ec}] ${message}",
               ("bn", head_block_num)
               ("ec", ec.value())
               ("message", ec.message()));
         my->_pending_snapshot_index.emplace(head_id, next, pending_path.generic_string(), snapshot_path.generic_string());
         my->_snapshot_scheduler.add_pending_snapshot_info( producer_plugin::snapshot_information{head_id, head_block_num, head_block_time, chain_snapshot_header::current_version, pending_path.generic_string()} );
      } CATCH_AND_CALL (next);
   }
}

void producer_plugin::schedule_snapshot(const snapshot_request_information& sri)
{
   my->_snapshot_scheduler.schedule_snapshot(sri);
}

void producer_plugin::unschedule_snapshot(const snapshot_request_id_information& sri)
{
   my->_snapshot_scheduler.unschedule_snapshot(sri.snapshot_request_id);
}

producer_plugin::get_snapshot_requests_result producer_plugin::get_snapshot_requests() const
{
   return my->_snapshot_scheduler.get_snapshot_requests();
}

producer_plugin::scheduled_protocol_feature_activations
producer_plugin::get_scheduled_protocol_feature_activations()const {
   return {my->_protocol_features_to_activate};
}

void producer_plugin::schedule_protocol_feature_activations( const scheduled_protocol_feature_activations& schedule ) {
   const chain::controller& chain = my->chain_plug->chain();
   std::set<digest_type> set_of_features_to_activate( schedule.protocol_features_to_activate.begin(),
                                                      schedule.protocol_features_to_activate.end() );
   EOS_ASSERT( set_of_features_to_activate.size() == schedule.protocol_features_to_activate.size(),
               invalid_protocol_features_to_activate, "duplicate digests" );
   chain.validate_protocol_features( schedule.protocol_features_to_activate );
   const auto& pfs = chain.get_protocol_feature_manager().get_protocol_feature_set();
   for (auto &feature_digest : set_of_features_to_activate) {
      const auto& pf = pfs.get_protocol_feature(feature_digest);
      EOS_ASSERT( !pf.preactivation_required, protocol_feature_exception,
                  "protocol feature requires preactivation: ${digest}",
                  ("digest", feature_digest));
   }
   my->_protocol_features_to_activate = schedule.protocol_features_to_activate;
   my->_protocol_features_signaled = false;
}

fc::variants producer_plugin::get_supported_protocol_features( const get_supported_protocol_features_params& params ) const {
   fc::variants results;
   const chain::controller& chain = my->chain_plug->chain();
   const auto& pfs = chain.get_protocol_feature_manager().get_protocol_feature_set();
   const auto next_block_time = chain.head_block_time() + fc::milliseconds(config::block_interval_ms);

   flat_map<digest_type, bool>  visited_protocol_features;
   visited_protocol_features.reserve( pfs.size() );

   std::function<bool(const protocol_feature&)> add_feature =
   [&results, &pfs, &params, next_block_time, &visited_protocol_features, &add_feature]
   ( const protocol_feature& pf ) -> bool {
      if( ( params.exclude_disabled || params.exclude_unactivatable ) && !pf.enabled ) return false;
      if( params.exclude_unactivatable && ( next_block_time < pf.earliest_allowed_activation_time  ) ) return false;

      auto res = visited_protocol_features.emplace( pf.feature_digest, false );
      if( !res.second ) return res.first->second;

      const auto original_size = results.size();
      for( const auto& dependency : pf.dependencies ) {
         if( !add_feature( pfs.get_protocol_feature( dependency ) ) ) {
            results.resize( original_size );
            return false;
         }
      }

      res.first->second = true;
      results.emplace_back( pf.to_variant(true) );
      return true;
   };

   for( const auto& pf : pfs ) {
      add_feature( pf );
   }

   return results;
}

producer_plugin::get_account_ram_corrections_result
producer_plugin::get_account_ram_corrections( const get_account_ram_corrections_params& params ) const {
   get_account_ram_corrections_result result;
   const auto& db = my->chain_plug->chain().db();

   const auto& idx = db.get_index<chain::account_ram_correction_index, chain::by_name>();
   account_name lower_bound_value{ std::numeric_limits<uint64_t>::lowest() };
   account_name upper_bound_value{ std::numeric_limits<uint64_t>::max() };

   if( params.lower_bound ) {
      lower_bound_value = *params.lower_bound;
   }

   if( params.upper_bound ) {
      upper_bound_value = *params.upper_bound;
   }

   if( upper_bound_value < lower_bound_value )
      return result;

   auto walk_range = [&]( auto itr, auto end_itr ) {
      for( unsigned int count = 0;
           count < params.limit && itr != end_itr;
           ++itr )
      {
         result.rows.push_back( fc::variant( *itr ) );
         ++count;
      }
      if( itr != end_itr ) {
         result.more = itr->name;
      }
   };

   auto lower = idx.lower_bound( lower_bound_value );
   auto upper = idx.upper_bound( upper_bound_value );
   if( params.reverse ) {
      walk_range( boost::make_reverse_iterator(upper), boost::make_reverse_iterator(lower) );
   } else {
      walk_range( lower, upper );
   }

   return result;
}

producer_plugin::get_unapplied_transactions_result
producer_plugin::get_unapplied_transactions( const get_unapplied_transactions_params& p, const fc::time_point& deadline ) const {

   fc::microseconds params_time_limit = p.time_limit_ms ? fc::milliseconds(*p.time_limit_ms) : fc::milliseconds(10);
   fc::time_point params_deadline = fc::time_point::now() + params_time_limit;

   auto& ua = my->_unapplied_transactions;

   auto itr = ([&](){
      if (!p.lower_bound.empty()) {
         try {
            auto trx_id = transaction_id_type( p.lower_bound );
            return ua.lower_bound( trx_id );
         } catch( ... ) {
            return ua.end();
         }
      } else {
         return ua.begin();
      }
   })();

   auto get_trx_type = [&](trx_enum_type t, transaction_metadata::trx_type type) {
      if( type == transaction_metadata::trx_type::dry_run ) return "dry_run";
      if( type == transaction_metadata::trx_type::read_only ) return "read_only";
      switch( t ) {
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
   result.size = ua.size();
   result.incoming_size = ua.incoming_size();

   uint32_t remaining = p.limit ? *p.limit : std::numeric_limits<uint32_t>::max();
   while (itr != ua.end() && remaining > 0 && params_deadline > fc::time_point::now()) {
      FC_CHECK_DEADLINE(deadline);
      auto& r = result.trxs.emplace_back();
      r.trx_id = itr->id();
      r.expiration = itr->expiration();
      const auto& pt = itr->trx_meta->packed_trx();
      r.trx_type = get_trx_type( itr->trx_type, itr->trx_meta->get_trx_type() );
      r.first_auth = pt->get_transaction().first_authorizer();
      const auto& actions = pt->get_transaction().actions;
      if( !actions.empty() ) {
         r.first_receiver = actions[0].account;
         r.first_action = actions[0].name;
      }
      r.total_actions = pt->get_transaction().total_actions();
      r.billed_cpu_time_us = itr->trx_meta->billed_cpu_time_us;
      r.size = pt->get_estimated_size();

      ++itr;
      remaining--;
   }

   if (itr != ua.end()) {
      result.more = itr->id();
   }

   return result;
}


std::optional<fc::time_point> producer_plugin_impl::calculate_next_block_time(const account_name& producer_name, const block_timestamp_type& current_block_time) const {
   chain::controller& chain = chain_plug->chain();
   const auto& hbs = chain.head_block_state();
   const auto& active_schedule = hbs->active_schedule.producers;

   // determine if this producer is in the active schedule and if so, where
   auto itr = std::find_if(active_schedule.begin(), active_schedule.end(), [&](const auto& asp){ return asp.producer_name == producer_name; });
   if (itr == active_schedule.end()) {
      // this producer is not in the active producer set
      return std::optional<fc::time_point>();
   }

   size_t producer_index = itr - active_schedule.begin();
   uint32_t minimum_offset = 1; // must at least be the "next" block

   // account for a watermark in the future which is disqualifying this producer for now
   // this is conservative assuming no blocks are dropped.  If blocks are dropped the watermark will
   // disqualify this producer for longer but it is assumed they will wake up, determine that they
   // are disqualified for longer due to skipped blocks and re-caculate their next block with better
   // information then
   auto current_watermark = get_watermark(producer_name);
   if (current_watermark) {
      const auto watermark = *current_watermark;
      auto block_num = chain.head_block_state()->block_num;
      if (chain.is_building_block()) {
         ++block_num;
      }
      if (watermark.first > block_num) {
         // if I have a watermark block number then I need to wait until after that watermark
         minimum_offset = watermark.first - block_num + 1;
      }
      if (watermark.second > current_block_time) {
          // if I have a watermark block timestamp then I need to wait until after that watermark timestamp
          minimum_offset = std::max(minimum_offset, watermark.second.slot - current_block_time.slot + 1);
      }
   }

   // this producers next opportuity to produce is the next time its slot arrives after or at the calculated minimum
   uint32_t minimum_slot = current_block_time.slot + minimum_offset;
   size_t minimum_slot_producer_index = (minimum_slot % (active_schedule.size() * config::producer_repetitions)) / config::producer_repetitions;
   if ( producer_index == minimum_slot_producer_index ) {
      // this is the producer for the minimum slot, go with that
      return block_timestamp_type(minimum_slot).to_time_point();
   } else {
      // calculate how many rounds are between the minimum producer and the producer in question
      size_t producer_distance = producer_index - minimum_slot_producer_index;
      // check for unsigned underflow
      if (producer_distance > producer_index) {
         producer_distance += active_schedule.size();
      }

      // align the minimum slot to the first of its set of reps
      uint32_t first_minimum_producer_slot = minimum_slot - (minimum_slot % config::producer_repetitions);

      // offset the aligned minimum to the *earliest* next set of slots for this producer
      uint32_t next_block_slot = first_minimum_producer_slot  + (producer_distance * config::producer_repetitions);
      return block_timestamp_type(next_block_slot).to_time_point();
   }
}

fc::time_point producer_plugin_impl::calculate_pending_block_time() const {
   const chain::controller& chain = chain_plug->chain();
   const fc::time_point now = fc::time_point::now();
   const fc::time_point base = std::max<fc::time_point>(now, chain.head_block_time());
   const int64_t min_time_to_next_block = (config::block_interval_us) - (base.time_since_epoch().count() % (config::block_interval_us) );
   fc::time_point block_time = base + fc::microseconds(min_time_to_next_block);
   return block_time;
}

fc::time_point producer_plugin_impl::calculate_block_deadline( const fc::time_point& block_time ) const {
   if( _pending_block_mode == pending_block_mode::producing ) {
      bool last_block = ((block_timestamp_type( block_time ).slot % config::producer_repetitions) == config::producer_repetitions - 1);
      return block_time + fc::microseconds(last_block ? _last_block_time_offset_us : _produce_time_offset_us);
   } else {
      return block_time + fc::microseconds(_produce_time_offset_us);
   }
}

bool producer_plugin_impl::should_interrupt_start_block( const fc::time_point& deadline, uint32_t pending_block_num ) const {
   if( _pending_block_mode == pending_block_mode::producing ) {
      return deadline <= fc::time_point::now();
   }
   // if we can produce then honor deadline so production starts on time
   return (!_producers.empty() && deadline <= fc::time_point::now()) || (_received_block >= pending_block_num);
}

producer_plugin_impl::start_block_result producer_plugin_impl::start_block() {
   chain::controller& chain = chain_plug->chain();
   update_block_metrics();

   if( !chain_plug->accept_transactions() )
      return start_block_result::waiting_for_block;

   const auto& hbs = chain.head_block_state();

   if( chain.get_terminate_at_block() > 0 && chain.get_terminate_at_block() <= chain.head_block_num() ) {
      ilog("Reached configured maximum block ${num}; terminating", ("num", chain.get_terminate_at_block()));
      app().quit();
      return start_block_result::failed;
   }

   const fc::time_point now = fc::time_point::now();
   const fc::time_point block_time = calculate_pending_block_time();
   const uint32_t pending_block_num = hbs->block_num + 1;
   const fc::time_point preprocess_deadline = calculate_block_deadline(block_time);

   const pending_block_mode previous_pending_mode = _pending_block_mode;
   _pending_block_mode = pending_block_mode::producing;

   // Not our turn
   const auto& scheduled_producer = hbs->get_scheduled_producer(block_time);

   const auto current_watermark = get_watermark(scheduled_producer.producer_name);

   size_t num_relevant_signatures = 0;
   scheduled_producer.for_each_key([&](const public_key_type& key){
      const auto& iter = _signature_providers.find(key);
      if(iter != _signature_providers.end()) {
         num_relevant_signatures++;
      }
   });

   auto irreversible_block_age = get_irreversible_block_age();

   // If the next block production opportunity is in the present or future, we're synced.
   if( !_production_enabled ) {
      _pending_block_mode = pending_block_mode::speculating;
   } else if( _producers.find(scheduled_producer.producer_name) == _producers.end()) {
      _pending_block_mode = pending_block_mode::speculating;
   } else if (num_relevant_signatures == 0) {
      elog("Not producing block because I don't have any private keys relevant to authority: ${authority}", ("authority", scheduled_producer.authority));
      _pending_block_mode = pending_block_mode::speculating;
   } else if ( _pause_production ) {
      elog("Not producing block because production is explicitly paused");
      _pending_block_mode = pending_block_mode::speculating;
   } else if ( _max_irreversible_block_age_us.count() >= 0 && irreversible_block_age >= _max_irreversible_block_age_us ) {
      elog("Not producing block because the irreversible block is too old [age:${age}s, max:${max}s]", ("age", irreversible_block_age.count() / 1'000'000)( "max", _max_irreversible_block_age_us.count() / 1'000'000 ));
      _pending_block_mode = pending_block_mode::speculating;
   }

   if (_pending_block_mode == pending_block_mode::producing) {
      // determine if our watermark excludes us from producing at this point
      if (current_watermark) {
         const block_timestamp_type block_timestamp{block_time};
         if (current_watermark->first > hbs->block_num) {
            elog("Not producing block because \"${producer}\" signed a block at a higher block number (${watermark}) than the current fork's head (${head_block_num})",
                 ("producer", scheduled_producer.producer_name)
                 ("watermark", current_watermark->first)
                 ("head_block_num", hbs->block_num));
            _pending_block_mode = pending_block_mode::speculating;
         } else if (current_watermark->second >= block_timestamp) {
            elog("Not producing block because \"${producer}\" signed a block at the next block time or later (${watermark}) than the pending block time (${block_timestamp})",
                 ("producer", scheduled_producer.producer_name)
                 ("watermark", current_watermark->second)
                 ("block_timestamp", block_timestamp));
            _pending_block_mode = pending_block_mode::speculating;
         }
      }
   }

   if (_pending_block_mode == pending_block_mode::speculating) {
      auto head_block_age = now - chain.head_block_time();
      if (head_block_age > fc::seconds(5))
         return start_block_result::waiting_for_block;
   }

   if (_pending_block_mode == pending_block_mode::producing) {
      const auto start_block_time = block_time - fc::microseconds( config::block_interval_us );
      if( now < start_block_time ) {
         fc_dlog(_log, "Not producing block waiting for production window ${n} ${bt}", ("n", pending_block_num)("bt", block_time) );
         // start_block_time instead of block_time because schedule_delayed_production_loop calculates next block time from given time
         schedule_delayed_production_loop(weak_from_this(), calculate_producer_wake_up_time(start_block_time));
         return start_block_result::waiting_for_production;
      }
   } else if (previous_pending_mode == pending_block_mode::producing) {
      // just produced our last block of our round
      const auto start_block_time = block_time - fc::microseconds( config::block_interval_us );
      fc_dlog(_log, "Not starting speculative block until ${bt}", ("bt", start_block_time) );
      schedule_delayed_production_loop( weak_from_this(), start_block_time);
      return start_block_result::waiting_for_production;
   }

   fc_dlog(_log, "Starting block #${n} at ${time} producer ${p}",
           ("n", pending_block_num)("time", now)("p", scheduled_producer.producer_name));

   try {
      uint16_t blocks_to_confirm = 0;

      if (_pending_block_mode == pending_block_mode::producing) {
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
      if( _pending_block_mode == pending_block_mode::producing && _protocol_features_to_activate.size() > 0 ) {
         bool drop_features_to_activate = false;
         try {
            chain.validate_protocol_features( _protocol_features_to_activate );
         } catch ( const std::bad_alloc& ) {
           chain_plugin::handle_bad_alloc();
         } catch ( const boost::interprocess::bad_alloc& ) {
           chain_plugin::handle_bad_alloc();
         } catch( const fc::exception& e ) {
            wlog( "protocol features to activate are no longer all valid: ${details}",
                  ("details",e.to_detail_string()) );
            drop_features_to_activate = true;
         } catch( const std::exception& e ) {
            wlog( "protocol features to activate are no longer all valid: ${details}",
                  ("details",fc::std_exception_wrapper::from_current_exception(e).to_detail_string()) );
            drop_features_to_activate = true;
         }

         if( drop_features_to_activate ) {
            _protocol_features_to_activate.clear();
         } else {
            auto protocol_features_to_activate = _protocol_features_to_activate; // do a copy as pending_block might be aborted
            if( features_to_activate.size() > 0 ) {
               protocol_features_to_activate.reserve( protocol_features_to_activate.size()
                                                         + features_to_activate.size() );
               std::set<digest_type> set_of_features_to_activate( protocol_features_to_activate.begin(),
                                                                  protocol_features_to_activate.end() );
               for( const auto& f : features_to_activate ) {
                  auto res = set_of_features_to_activate.insert( f );
                  if( res.second ) {
                     protocol_features_to_activate.push_back( f );
                  }
               }
               features_to_activate.clear();
            }
            std::swap( features_to_activate, protocol_features_to_activate );
            _protocol_features_signaled = true;
            ilog( "signaling activation of the following protocol features in block ${num}: ${features_to_activate}",
                  ("num", pending_block_num)("features_to_activate", features_to_activate) );
         }
      }

      controller::block_status bs = _pending_block_mode == pending_block_mode::producing ?
            controller::block_status::incomplete : controller::block_status::ephemeral;
      chain.start_block( block_time, blocks_to_confirm, features_to_activate, bs, preprocess_deadline );
   } LOG_AND_DROP();

   if( chain.is_building_block() ) {
      const auto& pending_block_signing_authority = chain.pending_block_signing_authority();

      if (_pending_block_mode == pending_block_mode::producing && pending_block_signing_authority != scheduled_producer.authority) {
         elog("Unexpected block signing authority, reverting to speculative mode! [expected: \"${expected}\", actual: \"${actual\"", ("expected", scheduled_producer.authority)("actual", pending_block_signing_authority));
         _pending_block_mode = pending_block_mode::speculating;
      }

      try {
         _account_fails.report_and_clear(hbs->block_num);
         _time_tracker.clear();

         if( !remove_expired_trxs( preprocess_deadline ) )
            return start_block_result::exhausted;
         if( !remove_expired_blacklisted_trxs( preprocess_deadline ) )
            return start_block_result::exhausted;
         if( !_subjective_billing.remove_expired( _log, chain.pending_block_time(), fc::time_point::now(),
                                                  [&](){ return should_interrupt_start_block( preprocess_deadline, pending_block_num ); } ) ) {
            return start_block_result::exhausted;
         }

         // limit execution of pending incoming to once per block
         auto incoming_itr = _unapplied_transactions.incoming_begin();

         if (_pending_block_mode == pending_block_mode::producing) {
            if( !process_unapplied_trxs( preprocess_deadline ) )
               return start_block_result::exhausted;


            auto scheduled_trx_deadline = preprocess_deadline;
            if (_max_scheduled_transaction_time_per_block_ms >= 0) {
               scheduled_trx_deadline = std::min<fc::time_point>(
                     scheduled_trx_deadline,
                     fc::time_point::now() + fc::milliseconds(_max_scheduled_transaction_time_per_block_ms)
               );
            }
            // may exhaust scheduled_trx_deadline but not preprocess_deadline, exhausted preprocess_deadline checked below
            process_scheduled_and_incoming_trxs( scheduled_trx_deadline, incoming_itr );
         }

         repost_exhausted_transactions( preprocess_deadline );

         if( app().is_quiting() ) // db guard exception above in LOG_AND_DROP could have called app().quit()
            return start_block_result::failed;
         if ( should_interrupt_start_block( preprocess_deadline, pending_block_num ) || block_is_exhausted() ) {
            return start_block_result::exhausted;
         }

         if( !process_incoming_trxs( preprocess_deadline, incoming_itr ) )
            return start_block_result::exhausted;

         return start_block_result::succeeded;

      } catch ( const guard_exception& e ) {
         chain_plugin::handle_guard_exception(e);
         return start_block_result::failed;
      } catch ( std::bad_alloc& ) {
         chain_plugin::handle_bad_alloc();
      } catch ( boost::interprocess::bad_alloc& ) {
         chain_plugin::handle_db_exhaustion();
      }

   }

   return start_block_result::failed;
}

bool producer_plugin_impl::remove_expired_trxs( const fc::time_point& deadline )
{
   chain::controller& chain = chain_plug->chain();
   auto pending_block_time = chain.pending_block_time();
   auto pending_block_num = chain.pending_block_num();

   // remove all expired transactions
   size_t num_expired = 0;
   size_t orig_count = _unapplied_transactions.size();
   bool exhausted = !_unapplied_transactions.clear_expired( pending_block_time, [&](){ return should_interrupt_start_block(deadline, pending_block_num); },
         [&num_expired]( const packed_transaction_ptr& packed_trx_ptr, trx_enum_type trx_type ) {
            // expired exception is logged as part of next() call
            ++num_expired;
   });

   if( exhausted && _pending_block_mode == pending_block_mode::producing ) {
      fc_wlog( _log, "Unable to process all expired transactions of the ${n} transactions in the unapplied queue before deadline, "
                     "Expired ${expired}", ("n", orig_count)("expired", num_expired) );
   } else {
      fc_dlog( _log, "Processed ${ex} expired transactions of the ${n} transactions in the unapplied queue.",
               ("n", orig_count)("ex", num_expired) );
   }

   return !exhausted;
}

bool producer_plugin_impl::remove_expired_blacklisted_trxs( const fc::time_point& deadline )
{
   bool exhausted = false;
   auto& blacklist_by_expiry = _blacklisted_transactions.get<by_expiry>();
   if(!blacklist_by_expiry.empty()) {
      const chain::controller& chain = chain_plug->chain();
      const auto lib_time = chain.last_irreversible_block_time();
      const auto pending_block_num = chain.pending_block_num();

      int num_expired = 0;
      int orig_count = _blacklisted_transactions.size();

      while (!blacklist_by_expiry.empty() && blacklist_by_expiry.begin()->expiry <= lib_time) {
         if ( should_interrupt_start_block( deadline, pending_block_num ) ) {
            exhausted = true;
            break;
         }
         blacklist_by_expiry.erase(blacklist_by_expiry.begin());
         num_expired++;
      }

      fc_dlog(_log, "Processed ${n} blacklisted transactions, Expired ${expired}",
              ("n", orig_count)("expired", num_expired));
   }
   return !exhausted;
}

// Returns contract name, action name, and exception text of an exception that occurred in a contract
inline std::string get_detailed_contract_except_info(const packed_transaction_ptr& trx,
                                                     const transaction_trace_ptr& trace,
                                                     const fc::exception_ptr& except_ptr)
{
   std::string contract_name;
   std::string act_name;
   std::string details;

   if( trace && !trace->action_traces.empty() ) {
      auto last_action_ordinal = trace->action_traces.size() - 1;
      contract_name = trace->action_traces[last_action_ordinal].receiver.to_string();
      act_name = trace->action_traces[last_action_ordinal].act.name.to_string();
   } else if ( trx ) {
      const auto& actions = trx->get_transaction().actions;
      if( actions.empty() ) return details; // should not be possible
      contract_name = actions[0].account.to_string();
      act_name = actions[0].name.to_string();
   }

   details = except_ptr ? except_ptr->top_message() : (trace && trace->except) ? trace->except->top_message() : std::string();
   if (!details.empty()) {
      details = fc::format_string("${d}", fc::mutable_variant_object() ("d", details), true);  // true for limiting the formatted string size
   }

   // this format is parsed by external tools
   return "action: " + contract_name + ":" + act_name + ", " + details;
}

void producer_plugin_impl::log_trx_results( const transaction_metadata_ptr& trx,
                                            const transaction_trace_ptr& trace,
                                            const fc::time_point& start )
{
   uint32_t billed_cpu_time_us = (trace && trace->receipt) ? trace->receipt->cpu_usage_us : 0;
   log_trx_results( trx->packed_trx(), trace, nullptr, billed_cpu_time_us, start, trx->is_transient() );
}

void producer_plugin_impl::log_trx_results( const transaction_metadata_ptr& trx,
                                            const fc::exception_ptr& except_ptr )
{
   uint32_t billed_cpu_time_us = trx ? trx->billed_cpu_time_us : 0;
   log_trx_results( trx->packed_trx(), nullptr, except_ptr, billed_cpu_time_us, fc::time_point::now(), trx->is_transient() );
}

void producer_plugin_impl::log_trx_results( const packed_transaction_ptr& trx,
                                            const transaction_trace_ptr& trace,
                                            const fc::exception_ptr& except_ptr,
                                            uint32_t billed_cpu_us,
                                            const fc::time_point& start,
                                            bool is_transient )
{
   chain::controller& chain = chain_plug->chain();

   auto get_trace = [&](const transaction_trace_ptr& trace, const fc::exception_ptr& except_ptr) -> fc::variant {
      if( trace ) {
         return chain_plug->get_log_trx_trace( trace );
      } else {
         return fc::variant{except_ptr};
      }
   };

   bool except = except_ptr || (trace && trace->except);
   if (except) {
      if (_pending_block_mode == pending_block_mode::producing) {
         fc_dlog( is_transient ? _transient_trx_failed_trace_log : _trx_failed_trace_log,
            "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING ${desc}tx: ${txid}, auth: ${a}, ${details}",
            ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
            ("desc", is_transient ? "transient " : "")("txid", trx->id())
            ("a", trx->get_transaction().first_authorizer())
            ("details", get_detailed_contract_except_info(trx, trace, except_ptr)));

         if ( !is_transient ) {
            fc_dlog(_trx_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING tx: ${trx}",
                 ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                 ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_failure_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING tx: ${entire_trace}",
                 ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                 ("entire_trace", get_trace(trace, except_ptr)));
         }
      } else {
         fc_dlog( is_transient ? _transient_trx_failed_trace_log : _trx_failed_trace_log, "[TRX_TRACE] Speculative execution is REJECTING ${desc}tx: ${txid}, auth: ${a} : ${details}",
            ("desc", is_transient ? "transient " : "")
            ("txid", trx->id())("a", trx->get_transaction().first_authorizer())
            ("details", get_detailed_contract_except_info(trx, trace, except_ptr)));
         if ( !is_transient ) {
            fc_dlog(_trx_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${trx} ",
                    ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_failure_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${entire_trace} ",
                    ("entire_trace", get_trace(trace, except_ptr)));
         }
      }
   } else {
      if (_pending_block_mode == pending_block_mode::producing) {
         fc_dlog( is_transient ? _transient_trx_successful_trace_log : _trx_successful_trace_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING ${desc}tx: ${txid}, auth: ${a}, cpu: ${cpu}",
            ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())("desc", is_transient ? "transient " : "")("txid", trx->id())
            ("a", trx->get_transaction().first_authorizer())("cpu", billed_cpu_us));
         if ( !is_transient ) {
            fc_dlog(_trx_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING tx: ${trx}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_success_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING tx: ${entire_trace}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("entire_trace", get_trace(trace, except_ptr)));
         }
      } else {
         fc_dlog( is_transient ? _transient_trx_successful_trace_log : _trx_successful_trace_log, "[TRX_TRACE] Speculative execution is ACCEPTING ${desc}tx: ${txid}, auth: ${a}, cpu: ${cpu}",
            ("desc", is_transient ? "transient " : "")
            ("txid", trx->id())("a", trx->get_transaction().first_authorizer())
            ("cpu", billed_cpu_us));
         if ( !is_transient ) {
            fc_dlog(_trx_log, "[TRX_TRACE] Speculative execution is ACCEPTING tx: ${trx}",
                    ("trx", chain_plug->get_log_trx(trx->get_transaction())));
            fc_dlog(_trx_trace_success_log, "[TRX_TRACE] Speculative execution is ACCEPTING tx: ${entire_trace}",
                    ("entire_trace", get_trace(trace, except_ptr)));
         }
      }
   }
}

// Does not modify unapplied_transaction_queue
producer_plugin_impl::push_result
producer_plugin_impl::push_transaction( const fc::time_point& block_deadline,
                                        const transaction_metadata_ptr& trx,
                                        bool api_trx,
                                        bool return_failure_trace,
                                        const next_function<transaction_trace_ptr>& next )
{
   auto start = fc::time_point::now();
   EOS_ASSERT(!trx->is_read_only(), producer_exception, "Unexpected read-only trx");

   bool disable_subjective_enforcement = (api_trx && _disable_subjective_api_billing)
                                         || (!api_trx && _disable_subjective_p2p_billing)
                                         || trx->is_transient();

   chain::controller& chain = chain_plug->chain();
   auto first_auth = trx->packed_trx()->get_transaction().first_authorizer();
   if( !disable_subjective_enforcement && _account_fails.failure_limit( first_auth ) ) {
      if( next ) {
         auto except_ptr = std::static_pointer_cast<fc::exception>( std::make_shared<tx_cpu_usage_exceeded>(
               FC_LOG_MESSAGE( error, "transaction ${id} exceeded failure limit for account ${a} until ${next_reset_time}",
                               ("id", trx->id())( "a", first_auth )
                               ("next_reset_time", _account_fails.next_reset_timepoint(chain.head_block_num(),chain.head_block_time()))) ) );
         log_trx_results( trx, except_ptr );
         next( except_ptr );
      }
      _time_tracker.add_fail_time(fc::time_point::now() - start, trx->is_transient());
      return push_result{.failed = true};
   }

   fc::microseconds max_trx_time = fc::milliseconds( _max_transaction_time_ms.load() );
   if( max_trx_time.count() < 0 ) max_trx_time = fc::microseconds::maximum();

   int64_t sub_bill = 0;
   if( !disable_subjective_enforcement )
      sub_bill = _subjective_billing.get_subjective_bill( first_auth, fc::time_point::now() );

   auto prev_billed_cpu_time_us = trx->billed_cpu_time_us;
   if( _pending_block_mode == pending_block_mode::producing && prev_billed_cpu_time_us > 0 ) {
      const auto& rl = chain.get_resource_limits_manager();
      if ( !_subjective_billing.is_account_disabled( first_auth ) && !rl.is_unlimited_cpu( first_auth ) ) {
         int64_t prev_billed_plus100_us = prev_billed_cpu_time_us + EOS_PERCENT( prev_billed_cpu_time_us, 100 * config::percent_1 );
         if( prev_billed_plus100_us < max_trx_time.count() ) max_trx_time = fc::microseconds( prev_billed_plus100_us );
      }
   }

   auto trace = chain.push_transaction( trx, block_deadline, max_trx_time, prev_billed_cpu_time_us, false, sub_bill );

   return handle_push_result(trx, next, start, chain, trace, return_failure_trace, disable_subjective_enforcement, first_auth, sub_bill, prev_billed_cpu_time_us);
}

producer_plugin_impl::push_result
producer_plugin_impl::handle_push_result( const transaction_metadata_ptr& trx,
                                          const next_function<transaction_trace_ptr>& next,
                                          const fc::time_point& start,
                                          const chain::controller& chain,
                                          const transaction_trace_ptr& trace,
                                          bool return_failure_trace,
                                          bool disable_subjective_enforcement,
                                          account_name first_auth,
                                          int64_t sub_bill,
                                          uint32_t prev_billed_cpu_time_us) {
   auto end = fc::time_point::now();
   push_result pr;
   if( trace->except ) {
      if ( chain.is_on_main_thread() ) {
         auto dur = end - start;
         _time_tracker.add_fail_time(dur, trx->is_transient());
      }
      if( exception_is_exhausted( *trace->except ) ) {
         if( _pending_block_mode == pending_block_mode::producing ) {
            fc_dlog(_trx_failed_trace_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} COULD NOT FIT, tx: ${txid} RETRYING ",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())("txid", trx->id()));
         } else {
            fc_dlog(_trx_failed_trace_log, "[TRX_TRACE] Speculative execution COULD NOT FIT tx: ${txid} RETRYING", ("txid", trx->id()));
         }
         if ( !trx->is_read_only() )
            pr.block_exhausted = block_is_exhausted(); // smaller trx might fit
         pr.trx_exhausted = true;
      } else {
         pr.failed = true;
         const fc::exception& e = *trace->except;
         if( e.code() != tx_duplicate::code_value ) {
            fc_tlog( _log, "Subjective bill for failed ${a}: ${b} elapsed ${t}us, time ${r}us",
                     ("a",first_auth)("b",sub_bill)("t",trace->elapsed)("r", end - start));
            if (!disable_subjective_enforcement) // subjectively bill failure when producing since not in objective cpu account billing
               _subjective_billing.subjective_bill_failure( first_auth, trace->elapsed, fc::time_point::now() );

            log_trx_results( trx, trace, start );
            // this failed our configured maximum transaction time, we don't want to replay it
            fc_tlog( _log, "Failed ${c} trx, auth: ${a}, prev billed: ${p}us, ran: ${r}us, id: ${id}, except: ${e}",
                     ("c", e.code())("a", first_auth)("p", prev_billed_cpu_time_us)
                     ( "r", end - start)("id", trx->id())("e", e) );
            if( !disable_subjective_enforcement )
               _account_fails.add( first_auth, e );
         }
         if( next ) {
            if( return_failure_trace ) {
               next( trace );
            } else {
               auto e_ptr = trace->except->dynamic_copy_exception();
               next( e_ptr );
            }
         }
      }
   } else {
      fc_tlog( _log, "Subjective bill for success ${a}: ${b} elapsed ${t}us, time ${r}us",
               ("a",first_auth)("b",sub_bill)("t",trace->elapsed)("r", end - start));
      if ( chain.is_on_main_thread() ) {
         auto dur = end - start;
         _time_tracker.add_success_time(dur, trx->is_transient());
      }
      log_trx_results( trx, trace, start );
      // if producing then trx is in objective cpu account billing
      if (!disable_subjective_enforcement && _pending_block_mode != pending_block_mode::producing) {
         _subjective_billing.subjective_bill( trx->id(), trx->packed_trx()->expiration(), first_auth, trace->elapsed );
      }
      if( next ) next( trace );
   }

   return pr;
}

bool producer_plugin_impl::process_unapplied_trxs( const fc::time_point& deadline )
{
   bool exhausted = false;
   if( !_unapplied_transactions.empty() ) {
      const chain::controller& chain = chain_plug->chain();
      const auto pending_block_num = chain.pending_block_num();
      int num_applied = 0, num_failed = 0, num_processed = 0;
      auto unapplied_trxs_size = _unapplied_transactions.size();
      auto itr     = _unapplied_transactions.unapplied_begin();
      auto end_itr = _unapplied_transactions.unapplied_end();
      while( itr != end_itr ) {
         if( should_interrupt_start_block( deadline, pending_block_num ) ) {
            exhausted = true;
            break;
         }

         ++num_processed;
         try {
            push_result pr = push_transaction( deadline, itr->trx_meta, false, itr->return_failure_trace, itr->next );

            exhausted = pr.block_exhausted;
            if( exhausted ) {
               break;
            } else {
               if( pr.failed ) {
                  ++num_failed;
               } else {
                  ++num_applied;
               }
            }
            if( !pr.trx_exhausted ) {
               itr = _unapplied_transactions.erase( itr );
            } else {
               ++itr; // keep exhausted
            }
            continue;
         } LOG_AND_DROP();
         ++num_failed;
         ++itr;
      }

      fc_dlog( _log, "Processed ${m} of ${n} previously applied transactions, Applied ${applied}, Failed/Dropped ${failed}",
               ("m", num_processed)( "n", unapplied_trxs_size )("applied", num_applied)("failed", num_failed) );
   }
   return !exhausted;
}

void producer_plugin_impl::process_scheduled_and_incoming_trxs( const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr )
{
   // scheduled transactions
   int num_applied = 0;
   int num_failed = 0;
   int num_processed = 0;
   bool exhausted = false;
   double incoming_trx_weight = 0.0;

   auto& blacklist_by_id = _blacklisted_transactions.get<by_id>();
   chain::controller& chain = chain_plug->chain();
   time_point pending_block_time = chain.pending_block_time();
   auto end = _unapplied_transactions.incoming_end();
   const auto& sch_idx = chain.db().get_index<generated_transaction_multi_index,by_delay>();
   const auto scheduled_trxs_size = sch_idx.size();
   auto sch_itr = sch_idx.begin();
   while( sch_itr != sch_idx.end() ) {
      if( sch_itr->delay_until > pending_block_time) break;    // not scheduled yet
      if( exhausted || deadline <= fc::time_point::now() ) {
         exhausted = true;
         break;
      }
      if( sch_itr->published >= pending_block_time ) {
         ++sch_itr;
         continue; // do not allow schedule and execute in same block
      }

      if (blacklist_by_id.find(sch_itr->trx_id) != blacklist_by_id.end()) {
         ++sch_itr;
         continue;
      }

      const transaction_id_type trx_id = sch_itr->trx_id; // make copy since reference could be invalidated
      const auto sch_expiration = sch_itr->expiration;
      auto sch_itr_next = sch_itr; // save off next since sch_itr may be invalidated by loop
      ++sch_itr_next;
      const auto next_delay_until = sch_itr_next != sch_idx.end() ? sch_itr_next->delay_until : sch_itr->delay_until;
      const auto next_id = sch_itr_next != sch_idx.end() ? sch_itr_next->id : sch_itr->id;

      num_processed++;

      // configurable ratio of incoming txns vs deferred txns
      while (incoming_trx_weight >= 1.0 && itr != end ) {
         if (deadline <= fc::time_point::now()) {
            exhausted = true;
            break;
         }

         incoming_trx_weight -= 1.0;

         auto trx_meta = itr->trx_meta;
         bool api_trx = itr->trx_type == trx_enum_type::incoming_api;

         push_result pr = push_transaction( deadline, trx_meta, api_trx, itr->return_failure_trace, itr->next );

         exhausted = pr.block_exhausted;
         if( pr.trx_exhausted ) {
            ++itr; // leave in incoming
         } else {
            itr = _unapplied_transactions.erase( itr );
         }

         if( exhausted ) break;
      }

      if (exhausted || deadline <= fc::time_point::now()) {
         exhausted = true;
         break;
      }

      auto get_first_authorizer = [&](const transaction_trace_ptr& trace) {
         for( const auto& a : trace->action_traces ) {
            for( const auto& u : a.act.authorization )
               return u.actor;
         }
         return account_name();
      };

      try {
         auto start = fc::time_point::now();
         fc::microseconds max_trx_time = fc::milliseconds( _max_transaction_time_ms.load() );
         if( max_trx_time.count() < 0 ) max_trx_time = fc::microseconds::maximum();

         auto trace = chain.push_scheduled_transaction(trx_id, deadline, max_trx_time, 0, false);
         auto end = fc::time_point::now();
         if (trace->except) {
            _time_tracker.add_fail_time(end - start, false); // delayed transaction cannot be transient
            if (exception_is_exhausted(*trace->except)) {
               if( block_is_exhausted() ) {
                  exhausted = true;
                  break;
               }
            } else {
               fc_dlog(_trx_failed_trace_log,
                       "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING scheduled tx: ${txid}, time: ${r}, auth: ${a} : ${details}",
                       ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                       ("txid", trx_id)("r", end - start)("a", get_first_authorizer(trace))
                       ("details", get_detailed_contract_except_info(nullptr, trace, nullptr)));
               fc_dlog(_trx_trace_failure_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is REJECTING scheduled tx: ${entire_trace}",
                       ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                       ("entire_trace", chain_plug->get_log_trx_trace(trace)));
               // this failed our configured maximum transaction time, we don't want to replay it add it to a blacklist
               _blacklisted_transactions.insert(transaction_id_with_expiry{trx_id, sch_expiration});
               num_failed++;
            }
         } else {
            _time_tracker.add_success_time(end - start, false); // delayed transaction cannot be transient
            fc_dlog(_trx_successful_trace_log,
                    "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING scheduled tx: ${txid}, time: ${r}, auth: ${a}, cpu: ${cpu}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("txid", trx_id)("r", end - start)("a", get_first_authorizer(trace))
                    ("cpu", trace->receipt ? trace->receipt->cpu_usage_us : 0));
            fc_dlog(_trx_trace_success_log, "[TRX_TRACE] Block ${block_num} for producer ${prod} is ACCEPTING scheduled tx: ${entire_trace}",
                    ("block_num", chain.head_block_num() + 1)("prod", get_pending_block_producer())
                    ("entire_trace", chain_plug->get_log_trx_trace(trace)));
            num_applied++;
         }
      } LOG_AND_DROP();

      incoming_trx_weight += _incoming_defer_ratio;

      if( sch_itr_next == sch_idx.end() ) break;
      sch_itr = sch_idx.lower_bound( boost::make_tuple( next_delay_until, next_id ) );
   }

   if( scheduled_trxs_size > 0 ) {
      fc_dlog( _log,
               "Processed ${m} of ${n} scheduled transactions, Applied ${applied}, Failed/Dropped ${failed}",
               ( "m", num_processed )( "n", scheduled_trxs_size )( "applied", num_applied )( "failed", num_failed ) );
   }
}

bool producer_plugin_impl::process_incoming_trxs( const fc::time_point& deadline, unapplied_transaction_queue::iterator& itr )
{
   bool exhausted = false;
   auto end = _unapplied_transactions.incoming_end();
   if( itr != end ) {
      size_t processed = 0;
      fc_dlog( _log, "Processing ${n} pending transactions", ("n", _unapplied_transactions.incoming_size()) );
      const chain::controller& chain = chain_plug->chain();
      const auto pending_block_num = chain.pending_block_num();
      while( itr != end ) {
         if ( should_interrupt_start_block( deadline, pending_block_num ) ) {
            exhausted = true;
            break;
         }

         auto trx_meta = itr->trx_meta;
         bool api_trx = itr->trx_type == trx_enum_type::incoming_api;

         push_result pr = push_transaction( deadline, trx_meta, api_trx, itr->return_failure_trace, itr->next );

         exhausted = pr.block_exhausted;
         if( pr.trx_exhausted ) {
            ++itr; // leave in incoming
         } else {
            itr = _unapplied_transactions.erase( itr );
         }

         if( exhausted ) break;
         ++processed;
      }
      fc_dlog( _log, "Processed ${n} pending transactions, ${p} left", ("n", processed)("p", _unapplied_transactions.incoming_size()) );
   }
   return !exhausted;
}

bool producer_plugin_impl::block_is_exhausted() const {
   const chain::controller& chain = chain_plug->chain();
   const auto& rl = chain.get_resource_limits_manager();

   const uint64_t cpu_limit = rl.get_block_cpu_limit();
   if( cpu_limit < _max_block_cpu_usage_threshold_us ) return true;
   const uint64_t net_limit = rl.get_block_net_limit();
   if( net_limit < _max_block_net_usage_threshold_bytes ) return true;
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

   _idle_trx_time = fc::time_point::now();

   if (result == start_block_result::failed) {
      elog("Failed to start a pending block, will try again later");
      _timer.expires_from_now( boost::posix_time::microseconds( config::block_interval_us  / 10 ));

      // we failed to start a block, so try again later?
      _timer.async_wait( app().executor().wrap( priority::high, exec_queue::read_write,
          [weak_this = weak_from_this(), cid = ++_timer_corelation_id]( const boost::system::error_code& ec ) {
             auto self = weak_this.lock();
             if( self && ec != boost::asio::error::operation_aborted && cid == self->_timer_corelation_id ) {
                self->schedule_production_loop();
             }
          } ) );
   } else if (result == start_block_result::waiting_for_block){
      if (!_producers.empty() && !production_disabled_by_policy()) {
         fc_dlog(_log, "Waiting till another block is received and scheduling Speculative/Production Change");
         schedule_delayed_production_loop(weak_from_this(), calculate_producer_wake_up_time(calculate_pending_block_time()));
      } else {
         fc_tlog(_log, "Waiting till another block is received");
         // nothing to do until more blocks arrive
      }

   } else if (result == start_block_result::waiting_for_production) {
      // scheduled in start_block()

   } else if (_pending_block_mode == pending_block_mode::producing) {
      schedule_maybe_produce_block( result == start_block_result::exhausted );

   } else if (_pending_block_mode == pending_block_mode::speculating && !_producers.empty() && !production_disabled_by_policy()){
      chain::controller& chain = chain_plug->chain();
      fc_dlog(_log, "Speculative Block Created; Scheduling Speculative/Production Change");
      EOS_ASSERT( chain.is_building_block(), missing_pending_block_state, "speculating without pending_block_state" );
      schedule_delayed_production_loop(weak_from_this(), calculate_producer_wake_up_time(chain.pending_block_time()));
   } else {
      fc_dlog(_log, "Speculative Block Created");
   }
}

void producer_plugin_impl::schedule_maybe_produce_block( bool exhausted ) {
   chain::controller& chain = chain_plug->chain();

   // we succeeded but block may be exhausted
   static const boost::posix_time::ptime epoch( boost::gregorian::date( 1970, 1, 1 ) );
   auto deadline = calculate_block_deadline( chain.pending_block_time() );

   if( !exhausted && deadline > fc::time_point::now() ) {
      // ship this block off no later than its deadline
      EOS_ASSERT( chain.is_building_block(), missing_pending_block_state,
                  "producing without pending_block_state, start_block succeeded" );
      _timer.expires_at( epoch + boost::posix_time::microseconds( deadline.time_since_epoch().count() ) );
      fc_dlog( _log, "Scheduling Block Production on Normal Block #${num} for ${time}",
               ("num", chain.head_block_num() + 1)( "time", deadline ) );
   } else {
      EOS_ASSERT( chain.is_building_block(), missing_pending_block_state, "producing without pending_block_state" );
      _timer.expires_from_now( boost::posix_time::microseconds( 0 ) );
      fc_dlog( _log, "Scheduling Block Production on ${desc} Block #${num} immediately",
               ("num", chain.head_block_num() + 1)("desc", block_is_exhausted() ? "Exhausted" : "Deadline exceeded") );
   }

   _timer.async_wait( app().executor().wrap( priority::high, exec_queue::read_write,
         [&chain, weak_this = weak_from_this(), cid=++_timer_corelation_id](const boost::system::error_code& ec) {
            auto self = weak_this.lock();
            if( self && ec != boost::asio::error::operation_aborted && cid == self->_timer_corelation_id ) {
               // pending_block_state expected, but can't assert inside async_wait
               auto block_num = chain.is_building_block() ? chain.head_block_num() + 1 : 0;
               fc_dlog( _log, "Produce block timer for ${num} running at ${time}", ("num", block_num)("time", fc::time_point::now()) );
               auto res = self->maybe_produce_block();
               fc_dlog( _log, "Producing Block #${num} returned: ${res}", ("num", block_num)( "res", res ) );
            }
         } ) );
}

std::optional<fc::time_point> producer_plugin_impl::calculate_producer_wake_up_time( const block_timestamp_type& ref_block_time ) const {
   // if we have any producers then we should at least set a timer for our next available slot
   std::optional<fc::time_point> wake_up_time;
   for (const auto& p : _producers) {
      auto next_producer_block_time = calculate_next_block_time(p, ref_block_time);
      if (next_producer_block_time) {
         auto producer_wake_up_time = *next_producer_block_time - fc::microseconds(config::block_interval_us);
         if (wake_up_time) {
            // wake up with a full block interval to the deadline
            if( producer_wake_up_time < *wake_up_time ) {
               wake_up_time = producer_wake_up_time;
            }
         } else {
            wake_up_time = producer_wake_up_time;
         }
      }
   }
   if( !wake_up_time ) {
      fc_dlog(_log, "Not Scheduling Speculative/Production, no local producers had valid wake up times");
   }

   return wake_up_time;
}

void producer_plugin_impl::schedule_delayed_production_loop(const std::weak_ptr<producer_plugin_impl>& weak_this, std::optional<fc::time_point> wake_up_time) {
   if (wake_up_time) {
      fc_dlog(_log, "Scheduling Speculative/Production Change at ${time}", ("time", wake_up_time));
      static const boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
      _timer.expires_at(epoch + boost::posix_time::microseconds(wake_up_time->time_since_epoch().count()));
      _timer.async_wait( app().executor().wrap( priority::high, exec_queue::read_write,
         [weak_this,cid=++_timer_corelation_id](const boost::system::error_code& ec) {
            auto self = weak_this.lock();
            if( self && ec != boost::asio::error::operation_aborted && cid == self->_timer_corelation_id ) {
               self->schedule_production_loop();
            }
         } ) );
   }
}


bool producer_plugin_impl::maybe_produce_block() {
   auto reschedule = fc::make_scoped_exit([this]{
      schedule_production_loop();
   });

   try {
      produce_block();
      return true;
   } LOG_AND_DROP();

   fc_dlog(_log, "Aborting block due to produce_block error");
   abort_block();
   return false;
}

static auto make_debug_time_logger() {
   auto start = fc::time_point::now();
   return fc::make_scoped_exit([=](){
      fc_dlog(_log, "Signing took ${ms}us", ("ms", fc::time_point::now() - start) );
   });
}

static auto maybe_make_debug_time_logger() -> std::optional<decltype(make_debug_time_logger())> {
   if (_log.is_enabled( fc::log_level::debug ) ){
      return make_debug_time_logger();
   } else {
      return {};
   }
}

void producer_plugin_impl::produce_block() {
   //ilog("produce_block ${t}", ("t", fc::time_point::now())); // for testing _produce_time_offset_us
   auto start = fc::time_point::now();
   EOS_ASSERT(_pending_block_mode == pending_block_mode::producing, producer_exception, "called produce_block while not actually producing");
   chain::controller& chain = chain_plug->chain();
   EOS_ASSERT(chain.is_building_block(), missing_pending_block_state, "pending_block_state does not exist but it should, another plugin may have corrupted it");

   const auto& auth = chain.pending_block_signing_authority();
   std::vector<std::reference_wrapper<const signature_provider_type>> relevant_providers;

   relevant_providers.reserve(_signature_providers.size());

   producer_authority::for_each_key(auth, [&](const public_key_type& key){
      const auto& iter = _signature_providers.find(key);
      if (iter != _signature_providers.end()) {
         relevant_providers.emplace_back(iter->second);
      }
   });

   EOS_ASSERT(relevant_providers.size() > 0, producer_priv_key_not_found, "Attempting to produce a block for which we don't have any relevant private keys");

   if (_protocol_features_signaled) {
      _protocol_features_to_activate.clear(); // clear _protocol_features_to_activate as it is already set in pending_block
      _protocol_features_signaled = false;
   }

   //idump( (fc::time_point::now() - chain.pending_block_time()) );
   controller::block_report br;
   chain.finalize_block( br, [&]( const digest_type& d ) {
      auto debug_logger = maybe_make_debug_time_logger();
      vector<signature_type> sigs;
      sigs.reserve(relevant_providers.size());

      // sign with all relevant public keys
      for (const auto& p : relevant_providers) {
         sigs.emplace_back(p.get()(d));
      }
      return sigs;
   } );

   chain.commit_block();

   block_state_ptr new_bs = chain.head_block_state();

   _time_tracker.report(_idle_trx_time, new_bs->block_num);

   br.total_time += fc::time_point::now() - start;

   ++_metrics.blocks_produced.value;
   _metrics.trxs_produced.value += new_bs->block->transactions.size();

   ilog("Produced block ${id}... #${n} @ ${t} signed by ${p} "
        "[trxs: ${count}, lib: ${lib}, confirmed: ${confs}, net: ${net}, cpu: ${cpu}, elapsed: ${et}, time: ${tt}]",
        ("p",new_bs->header.producer)("id",new_bs->id.str().substr(8,16))
        ("n",new_bs->block_num)("t",new_bs->header.timestamp)
        ("count",new_bs->block->transactions.size())("lib",chain.last_irreversible_block_num())
        ("net", br.total_net_usage)("cpu", br.total_cpu_usage_us)("et", br.total_elapsed_time)("tt", br.total_time)
        ("confs", new_bs->header.confirmed));
}

void producer_plugin::received_block(uint32_t block_num) {
   my->_received_block = block_num;
}

void producer_plugin::log_failed_transaction(const transaction_id_type& trx_id, const packed_transaction_ptr& packed_trx_ptr, const char* reason) const {
   fc_dlog(_trx_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${trx}",
           ("entire_trx", packed_trx_ptr ? my->chain_plug->get_log_trx(packed_trx_ptr->get_transaction()) : fc::variant{trx_id}));
   fc_dlog(_trx_failed_trace_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${txid} : ${why}",
            ("txid", trx_id)("why", reason));
   fc_dlog(_trx_trace_failure_log, "[TRX_TRACE] Speculative execution is REJECTING tx: ${entire_trx}",
            ("entire_trx", packed_trx_ptr ? my->chain_plug->get_log_trx(packed_trx_ptr->get_transaction()) : fc::variant{trx_id}));
}

// Called from only one read_only thread
void producer_plugin_impl::switch_to_write_window() {
   if ( _log.is_enabled( fc::log_level::debug ) ) {
      auto now = fc::time_point::now();
      fc_dlog( _log, "Read-only threads ${n}, read window ${r}us, total all threads ${t}us",
               ("n", _ro_thread_pool_size)
               ("r", now - _ro_read_window_start_time)
               ("t", _ro_all_threads_exec_time_us.load()));
   }

   // this method can be called from multiple places. it is possible
   // we are already in write window.
   if ( app().executor().is_write_window() ) {
      return;
   }

   EOS_ASSERT(_ro_num_active_exec_tasks.load() == 0 && _ro_exec_tasks_fut.empty(), producer_exception, "no read-only tasks should be running before switching to write window");

   start_write_window();
}

// Called from app thread on plugin_startup
// Called from only one read_only thread & called from app thread, but not concurrently
void producer_plugin_impl::start_write_window() {
   chain::controller& chain = chain_plug->chain();

   app().executor().set_to_write_window();
   chain.unset_db_read_only_mode();
   _ro_in_read_only_mode = false;
   _idle_trx_time = _ro_window_deadline = fc::time_point::now();

   _ro_window_deadline += _ro_write_window_time_us; // not allowed on block producers, so no need to limit to block deadline
   auto expire_time = boost::posix_time::microseconds(_ro_write_window_time_us.count());
   _ro_timer.expires_from_now( expire_time );
   _ro_timer.async_wait( app().executor().wrap(  // stay on app thread
      priority::high,
      exec_queue::read_write, // placed in read_write so only called from main thread
      [weak_this = weak_from_this()]( const boost::system::error_code& ec ) {
         auto self = weak_this.lock();
         if( self && ec != boost::asio::error::operation_aborted ) {
            self->switch_to_read_window();
         }
      }));
}

// Called only from app thread
void producer_plugin_impl::switch_to_read_window() {
   EOS_ASSERT(app().executor().is_write_window(),  producer_exception, "expected to be in write window");
   EOS_ASSERT( _ro_num_active_exec_tasks.load() == 0 && _ro_exec_tasks_fut.empty(), producer_exception, "_ro_exec_tasks_fut expected to be empty" );

   _time_tracker.add_idle_time( fc::time_point::now() - _idle_trx_time );

   // we are in write window, so no read-only trx threads are processing transactions.
   if ( app().executor().read_only_queue().empty() ) { // no read-only tasks to process. stay in write window
      start_write_window(); // restart write window timer for next round
      return;
   }

   auto& chain = chain_plug->chain();
   uint32_t pending_block_num = chain.head_block_num() + 1;
   _ro_read_window_start_time = fc::time_point::now();
   _ro_window_deadline = _ro_read_window_start_time + _ro_read_window_effective_time_us;
   app().executor().set_to_read_window(_ro_thread_pool_size,
      [received_block=&_received_block, pending_block_num, ro_window_deadline=_ro_window_deadline]() {
         return fc::time_point::now() >= ro_window_deadline || (received_block->load() >= pending_block_num); // should_exit()
      });
   chain.set_db_read_only_mode();
   _ro_in_read_only_mode = true;
   _ro_all_threads_exec_time_us = 0;

   // start a read-only execution task in each thread in the thread pool
   _ro_num_active_exec_tasks = _ro_thread_pool_size;
   _ro_exec_tasks_fut.resize(0);
   for (auto i = 0; i < _ro_thread_pool_size; ++i ) {
      _ro_exec_tasks_fut.emplace_back( post_async_task( _ro_thread_pool.get_executor(), [self = this, pending_block_num] () {
         return self->read_only_execution_task(pending_block_num);
      }) );
   }

   auto expire_time = boost::posix_time::microseconds(_ro_read_window_time_us.count());
   _ro_timer.expires_from_now( expire_time );
   // Needs to be on read_only because that is what is being processed until switch_to_write_window().
   _ro_timer.async_wait( app().executor().wrap(
      priority::high,
      exec_queue::read_only,
      [weak_this = weak_from_this()]( const boost::system::error_code& ec ) {
         auto self = weak_this.lock();
         if( self && ec != boost::asio::error::operation_aborted ) {
            // use future to make sure all read-only tasks finished before switching to write window
            for ( auto& task: self->_ro_exec_tasks_fut ) {
               task.get();
            }
            self->_ro_exec_tasks_fut.clear();
            // will be executed from the main app thread because all read-only threads are idle now
            self->switch_to_write_window();
          } else if ( self ) {
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
   while ( fc::time_point::now() < _ro_window_deadline && _received_block < pending_block_num ) {
      bool more = app().executor().execute_highest_read_only(); // blocks until all read only threads are idle
      if ( !more ) {
         break;
      }
   }

   // If all tasks are finished, do not wait until end of read window; switch to write window now.
   if ( --_ro_num_active_exec_tasks == 0 ) {
      // Needs to be on read_only because that is what is being processed until switch_to_write_window().
      app().executor().post( priority::high, exec_queue::read_only, [self=this]() {
         self->_ro_exec_tasks_fut.clear();
         // will be executed from the main app thread because all read-only threads are idle now
         self->switch_to_write_window();
      } );
      // last thread post any exhausted back into read_only queue with slightly higher priority (low+1) so they are executed first
      ro_trx_t t;
      while( _ro_exhausted_trx_queue.pop_front(t) ) {
         app().executor().post(priority::low+1, exec_queue::read_only, [this, trx{std::move(t.trx)}, next{std::move(t.next)}]() mutable {
            push_read_only_transaction( std::move(trx), std::move(next) );
         } );
      }
   }

   return true;
}

// Called from app thread during start block.
// Reschedule any exhausted read-only transactions from the last block
void producer_plugin_impl::repost_exhausted_transactions(const fc::time_point& deadline) {
   if ( !_ro_exhausted_trx_queue.empty() ) {
      chain::controller& chain = chain_plug->chain();
      uint32_t pending_block_num = chain.pending_block_num();
      // post any exhausted back into read_only queue with slightly higher priority (low+1) so they are executed first
      ro_trx_t t;
      while( !should_interrupt_start_block( deadline, pending_block_num ) && _ro_exhausted_trx_queue.pop_front(t) ) {
         app().executor().post(priority::low+1, exec_queue::read_only, [this, trx{std::move(t.trx)}, next{std::move(t.next)}]() mutable {
            push_read_only_transaction( std::move(trx), std::move(next) );
         } );
      }
   }
}

// Called from a read_only_trx execution thread, or from app thread when executing exclusively
// Return whether the trx needs to be retried in next read window
bool producer_plugin_impl::push_read_only_transaction(transaction_metadata_ptr trx, next_function<transaction_trace_ptr> next) {
   auto retry = false;

   try {
      auto start = fc::time_point::now();
      chain::controller& chain = chain_plug->chain();
      if ( !chain.is_building_block() ) {
         _ro_exhausted_trx_queue.push_front( {std::move(trx), std::move(next)} );
         return true;
      }

      // when executing on the main thread while in the write window, need to switch db mode to read only
      // _ro_in_read_only_mode can only be false if running on main thread as it is only modified from the main thread
      auto db_read_only_mode_guard = fc::make_scoped_exit([&]{
         if( !_ro_in_read_only_mode )
            chain.unset_db_read_only_mode();
      });
      if( !_ro_in_read_only_mode ) {
         chain.set_db_read_only_mode();
      }
      // use read-window/write-window deadline if there are read/write windows, otherwise use block_deadline if only the app thead
      auto window_deadline = (_ro_thread_pool_size != 0) ? _ro_window_deadline : calculate_block_deadline( chain.pending_block_time() );

      // Ensure the trx to finish by the end of read-window or write-window or block_deadline depending on
      auto trace = chain.push_transaction( trx, window_deadline, _ro_max_trx_time_us, 0, false, 0 );
      _ro_all_threads_exec_time_us += (fc::time_point::now() - start).count();
      auto pr = handle_push_result(trx, next, start, chain, trace, true /*return_failure_trace*/, true /*disable_subjective_enforcement*/, {} /*first_auth*/, 0 /*sub_bill*/, 0 /*prev_billed_cpu_time_us*/);
      // If a transaction was exhausted, that indicates we are close to
      // the end of read window. Retry in next round.
      retry = pr.trx_exhausted;
      if( retry ) {
         _ro_exhausted_trx_queue.push_front( {std::move(trx), std::move(next)} );
      }
   } catch ( const guard_exception& e ) {
      chain_plugin::handle_guard_exception(e);
   } catch ( boost::interprocess::bad_alloc& ) {
      chain_plugin::handle_db_exhaustion();
   } catch ( std::bad_alloc& ) {
      chain_plugin::handle_bad_alloc();
   } CATCH_AND_CALL(next);

   return retry;
}

const std::set<account_name>& producer_plugin::producer_accounts() const {
   return my->_producers;
}

void producer_plugin::register_metrics_listener(metrics_listener listener) {
   my->_metrics.register_listener(listener);
}
} // namespace eosio
