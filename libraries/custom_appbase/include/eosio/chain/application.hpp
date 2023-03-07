#pragma once

#include <appbase/application_base.hpp>
#include <appbase/execution_priority_queue.hpp>

/*
 * Custmomize appbase to support two-queue exector.
*/
namespace appbase { 

enum class exec_window {
   read,  // the window during which operations from read_only_trx_safe queue
          // can be executed in app thread in parallel with read-only transactions
          // in read-only transaction excuting threads.
   write, // the window during which operations from both general and
          // read_only_trx_safe queues can be executed in app thread,
          // while read-only transactions are not executed in read-only
          // transaction excuting threads.
};

enum class exec_queue {
   read_only_trx_safe, // the queue storing operations which are safe to execute
                       // on app thread in parallel with read-only transactions
                       // in read-only transaction excuting threads.
   general             // the queue storing operations which can be only executed
                       // on the app thread while read-only transactions are
                       // not being executed in read-only threads
};

class two_queue_executor {
public:

   template <typename Func>
   auto post( int priority, exec_queue q, Func&& func ) {
      if ( q == exec_queue::general )
         return boost::asio::post(io_serv_, general_queue_.wrap(priority, --order_, std::forward<Func>(func)));
      else
         return boost::asio::post(io_serv_, read_only_trx_safe_queue_.wrap(priority, --order_, std::forward<Func>(func)));
   }

   // Legacy and deprecated. To be removed after cleaning up its uses in base appbase
   template <typename Func>
   auto post( int priority, Func&& func ) {
      // safer to use general queue for unknown type of operation since operations
      // from general queue are not executed in parallel with read-only transactions
      return boost::asio::post(io_serv_, general_queue_.wrap(priority, --order_, std::forward<Func>(func)));
   }

   boost::asio::io_service& get_io_service() { return io_serv_; }

   bool execute_highest() {
      if ( exec_window_ == exec_window::write ) {
         if( !general_queue_.empty() && ( read_only_trx_safe_queue_.empty() || *read_only_trx_safe_queue_.top() < *general_queue_.top()) )  {
            // general_queue_'s top function's priority greater than read_only_trx_safe_queue_'s top function's, or general_queue_ empty
            general_queue_.execute_highest();
         } else if( !read_only_trx_safe_queue_.empty() ) {
            read_only_trx_safe_queue_.execute_highest();
         }
         return !read_only_trx_safe_queue_.empty() || !general_queue_.empty();
      } else {
         return read_only_trx_safe_queue_.execute_highest();
      }
   }

   template <typename Function>
   boost::asio::executor_binder<Function, appbase::execution_priority_queue::executor>
   wrap(int priority, exec_queue q, Function&& func ) {
      if ( q == exec_queue::general )
         return general_queue_.wrap(priority, --order_, std::forward<Function>(func));
      else
         return read_only_trx_safe_queue_.wrap(priority, --order_, std::forward<Function>(func));
   }
     
   void clear() {
      read_only_trx_safe_queue_.clear();
      general_queue_.clear();
   }

   void set_to_read_window() {
      exec_window_ = exec_window::read;
   }

   void set_to_write_window() {
      exec_window_ = exec_window::write;
   }

   bool is_read_window() const {
      return exec_window_ == exec_window::read;
   }

   bool is_write_window() const {
      return exec_window_ == exec_window::write;
   }

   auto& read_only_trx_safe_queue() { return read_only_trx_safe_queue_; }

   auto& general_queue() { return general_queue_; }

   // members are ordered taking into account that the last one is destructed first
private:
   boost::asio::io_service            io_serv_;
   appbase::execution_priority_queue  read_only_trx_safe_queue_;
   appbase::execution_priority_queue  general_queue_;
   std::atomic<std::size_t>           order_ { std::numeric_limits<size_t>::max() }; // to maintain FIFO ordering in both queues within priority
   exec_window                        exec_window_ { exec_window::write };
};

using application = application_t<two_queue_executor>;
}

#include <appbase/application_instance.hpp>
