#pragma once

#include <appbase/application_base.hpp>
#include <eosio/chain/exec_pri_queue.hpp>
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

enum class exec_queue {
   read_only,          // the queue storing tasks which are safe to execute
                       // in parallel with other read-only tasks in the read-only
                       // thread pool as well as on the main app thread.
                       // Multi-thread safe as long as nothing is executed from the read_write queue.
   read_write          // the queue storing tasks which can be only executed
                       // on the app thread while read-only tasks are
                       // not being executed in read-only threads. Single threaded.
};

class two_queue_executor {
public:

   template <typename Func>
   auto post( int priority, exec_queue q, Func&& func ) {
      if ( q == exec_queue::read_write )
         return boost::asio::post(io_serv_, read_write_queue_.wrap(priority, --order_, std::forward<Func>(func)));
      else
         return boost::asio::post( io_serv_, read_only_queue_.wrap( priority, --order_, std::forward<Func>( func)));
   }

   // Legacy and deprecated. To be removed after cleaning up its uses in base appbase
   template <typename Func>
   auto post( int priority, Func&& func ) {
      // safer to use read_write queue for unknown type of operation since operations
      // from read_write queue are not executed in parallel with read-only operations
      return boost::asio::post(io_serv_, read_write_queue_.wrap(priority, --order_, std::forward<Func>(func)));
   }

   boost::asio::io_service& get_io_service() { return io_serv_; }

   bool execute_highest() {
      if ( exec_window_ == exec_window::write ) {
         // During write window only main thread is accessing anything in two_queue_executor, no locking required
         if( !read_write_queue_.empty() && (read_only_queue_.empty() || *read_only_queue_.top() < *read_write_queue_.top()) )  {
            // read_write_queue_'s top function's priority greater than read_only_queue_'s top function's, or read_only_queue_ empty
            read_write_queue_.execute_highest();
         } else if( !read_only_queue_.empty() ) {
            read_only_queue_.execute_highest();
         }
         return !read_only_queue_.empty() || !read_write_queue_.empty();
      } else {
         // When in read window, multiple threads including main app thread are accessing two_queue_executor, locking required
         return read_only_queue_.execute_highest_locked(false);
      }
   }

   bool execute_highest_read_only() {
      return read_only_queue_.execute_highest_locked(true);
   }

   template <typename Function>
   boost::asio::executor_binder<Function, appbase::exec_pri_queue::executor>
   wrap(int priority, exec_queue q, Function&& func ) {
      if ( q == exec_queue::read_write )
         return read_write_queue_.wrap(priority, --order_, std::forward<Function>(func));
      else
         return read_only_queue_.wrap( priority, --order_, std::forward<Function>( func));
   }
     
   void clear() {
      read_only_queue_.clear();
      read_write_queue_.clear();
   }

   void set_to_read_window(uint32_t num_threads, std::function<bool()> should_exit) {
      exec_window_ = exec_window::read;
      read_only_queue_.enable_locking(num_threads, std::move(should_exit));
   }

   void set_to_write_window() {
      exec_window_ = exec_window::write;
      read_only_queue_.disable_locking();
   }

   bool is_read_window() const {
      return exec_window_ == exec_window::read;
   }

   bool is_write_window() const {
      return exec_window_ == exec_window::write;
   }

   auto& read_only_queue() { return read_only_queue_; }

   auto& read_write_queue() { return read_write_queue_; }

   // members are ordered taking into account that the last one is destructed first
private:
   boost::asio::io_service            io_serv_;
   appbase::exec_pri_queue            read_only_queue_;
   appbase::exec_pri_queue            read_write_queue_;
   std::atomic<std::size_t>           order_ { std::numeric_limits<size_t>::max() }; // to maintain FIFO ordering in both queues within priority
   exec_window                        exec_window_ { exec_window::write };
};

using application = application_t<two_queue_executor>;
}

#include <appbase/application_instance.hpp>
