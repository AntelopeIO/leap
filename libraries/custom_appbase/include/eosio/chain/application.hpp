#pragma once

#include <appbase/application_base.hpp>
#include <eosio/chain/exec_pri_queue.hpp>
#include <chrono>
#include <optional>
#include <mutex>

/*
 * Customize appbase to support two-queue executor.
*/
namespace appbase { 

enum class exec_window {
   read,  // the window during which operations from read_only queue
          // can be executed in parallel in the read-only thread pool
          // as well as in the app thread.
   write, // the window during which operations from both read_write and
          // parallel queues can be executed in app thread,
          // while read-only operations are not executed in read-only
          // thread pool. The read-only thread pool is not active; only
          // the main app thread is active.
};

class priority_queue_executor {
public:

   // Trade off on returning to appbase exec() loop as the overhead of poll/run can be measurable for small running tasks.
   // This adds to the total time that the main thread can be busy when a high priority task is waiting.
   static constexpr uint16_t minimum_runtime_ms = 3;

   // inform how many read_threads will be calling read_only/read_exclusive queues
   // expected to only be called at program startup, not thread safe, not safe to call after startup
   void init_read_threads(size_t num_read_threads) {
      pri_queue_.init_read_threads(num_read_threads);
   }

   // not thread safe, see init_read_threads comment
   size_t get_read_threads() const {
      return pri_queue_.get_read_threads();
   }

   // assume application is started on the main thread
   std::thread::id get_main_thread_id() const {
      return main_thread_id_;
   }

   template <typename Func>
   void post( int priority, exec_queue q, Func&& func ) {
      if (q == exec_queue::read_exclusive) {
         // no reason to post to io_service which then places this in the read_exclusive_handlers queue.
         // read_exclusive tasks are run exclusively by read threads by pulling off the read_exclusive handlers queue.
         pri_queue_.add(priority, q, --order_, std::forward<Func>(func));
      } else {
         // post to io_service as the main thread may be blocked on io_service.run_one() in application::exec()
         boost::asio::post(io_serv_, pri_queue_.wrap(priority, q, --order_, std::forward<Func>(func)));
      }
   }

   // Legacy and deprecated. To be removed after cleaning up its uses in base appbase
   template <typename Func>
   auto post( int priority, Func&& func ) {
      // safer to use read_write queue for unknown type of operation since operations
      // from read_write queue are not executed in parallel with read-only operations
      return boost::asio::post(io_serv_, pri_queue_.wrap(priority, exec_queue::read_write, --order_, std::forward<Func>(func)));
   }

   boost::asio::io_service& get_io_service() { return io_serv_; }

   // called from main thread, highest read_only and read_write
   bool execute_highest() {
      // execute for at least minimum runtime
      const auto end = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(minimum_runtime_ms);

      bool more = false;
      while (true) {
         if ( exec_window_ == exec_window::write ) {
            // During write window only main thread is accessing anything in priority_queue_executor, no locking required
            more = pri_queue_.execute_highest(exec_queue::read_write, exec_queue::read_only);
         } else {
            // When in read window, multiple threads including main app thread are accessing priority_queue_executor, locking required
            more = pri_queue_.execute_highest_locked(exec_queue::read_only);
         }
         if (!more || std::chrono::high_resolution_clock::now() > end)
            break;
      }
      return more;
   }

   bool execute_highest_read() {
      // execute for at least minimum runtime
      const auto end = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(minimum_runtime_ms);

      bool more = false;
      while (true) {
         get_io_service().poll(); // schedule any queued
         more = pri_queue_.execute_highest_blocking_locked(exec_queue::read_only, exec_queue::read_exclusive);
         if (!more || std::chrono::high_resolution_clock::now() > end)
            break;
      }
      return more;
   }

   template <typename Function>
   boost::asio::executor_binder<Function, appbase::exec_pri_queue::executor>
   wrap(int priority, exec_queue q, Function&& func ) {
      return pri_queue_.wrap(priority, q, --order_, std::forward<Function>(func));
   }

   void stop() {
      pri_queue_.stop();
   }
     
   void clear() {
      pri_queue_.clear();
   }

   void set_to_read_window(std::function<bool()> should_exit) {
      exec_window_ = exec_window::read;
      pri_queue_.enable_locking(std::move(should_exit));
   }

   void set_to_write_window() {
      exec_window_ = exec_window::write;
      pri_queue_.disable_locking();
   }

   bool is_read_window() const {
      return exec_window_ == exec_window::read;
   }

   bool is_write_window() const {
      return exec_window_ == exec_window::write;
   }

   size_t read_only_queue_size() { return pri_queue_.size(exec_queue::read_only); }
   size_t read_write_queue_size() { return pri_queue_.size(exec_queue::read_write); }
   size_t read_exclusive_queue_size() { return pri_queue_.size(exec_queue::read_exclusive); }
   bool read_only_queue_empty() { return pri_queue_.empty(exec_queue::read_only); }
   bool read_write_queue_empty() { return pri_queue_.empty(exec_queue::read_write); }
   bool read_exclusive_queue_empty() { return pri_queue_.empty(exec_queue::read_exclusive); }

   // members are ordered taking into account that the last one is destructed first
private:
   std::thread::id                    main_thread_id_{ std::this_thread::get_id() };
   boost::asio::io_service            io_serv_;
   appbase::exec_pri_queue            pri_queue_;
   std::atomic<std::size_t>           order_{ std::numeric_limits<size_t>::max() }; // to maintain FIFO ordering in all queues within priority
   exec_window                        exec_window_{ exec_window::write };
};

using application = application_t<priority_queue_executor>;
}

#include <appbase/application_instance.hpp>
