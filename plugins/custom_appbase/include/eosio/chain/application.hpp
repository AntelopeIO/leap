#pragma once

#include <appbase/application_base.hpp>
#include <eosio/chain/exec_pri_queue.hpp>

/*
 * Custmomize appbase to support two-queue exector.
*/
namespace appbase { 

class two_queue_executor {
public:
    enum class exec_window {
       read_only,  // execute functions only from read_queue_
       read_write  // execute functions from both read_queue_ and write_queue
    };

   template <typename Func>
   auto post( int priority, appbase::exec_pri_queue& q, Func&& func ) {
      return boost::asio::post(io_serv_, q.wrap(priority, --order_, std::forward<Func>(func)));
   }

   template <typename Func>
   auto post( int priority, Func&& func ) {
      return boost::asio::post(io_serv_, read_queue_.wrap(priority, --order_, std::forward<Func>(func)));
   }

   auto& read_queue() { return read_queue_; }
   auto& write_queue() { return write_queue_; }

   boost::asio::io_service& get_io_service() { return io_serv_; }

   bool execute_highest() {
      if ( exec_window_ == exec_window::read_write ) {
         if( !write_queue_.empty() && ( read_queue_.empty() || *read_queue_.top() < *write_queue_.top()) )  {
            // write_queue_'s top function's priority greater than read_queue_'s top function's, or write_queue_ empty
            write_queue_.execute_highest();
         } else if( !read_queue_.empty() ) {
            read_queue_.execute_highest();
         }
         return !read_queue_.empty() || !write_queue_.empty();
      } else {
         return read_queue_.execute_highest();
      }
   }

   template <typename Function>
   boost::asio::executor_binder<Function, appbase::exec_pri_queue::executor>
   wrap(int priority, Function&& func) {
      return read_queue_.wrap(priority, --order_, std::forward<Function>(func));
   }
     
   void clear() {
      read_queue_.clear();
      write_queue_.clear();
   }

   void set_exec_window_to_read_only() {
      exec_window_ = exec_window::read_only;
   }

   bool is_exec_window_read_only() const {
      return exec_window_ == exec_window::read_only;
   }

   void set_exec_window_to_read_write() {
      exec_window_ = exec_window::read_write;
   }

   bool is_exec_window_read_write() const {
      return exec_window_ == exec_window::read_write;
   }

   // members are ordered taking into account that the last one is destructed first
private:
   boost::asio::io_service    io_serv_;
   appbase::exec_pri_queue    read_queue_;
   appbase::exec_pri_queue    write_queue_;
   std::atomic<std::size_t>   order_ { std::numeric_limits<size_t>::max() }; // to maintain FIFO ordering in both queues within priority
   exec_window                exec_window_ { exec_window::read_write };
};

using application = application_t<two_queue_executor>;
}

#include <appbase/application_instance.hpp>
