#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace eosio { namespace chain {

   /**
    * Wrapper class for boost asio thread pool and io_context run.
    * Also names threads so that tools like htop can see thread name.
    */
   class named_thread_pool {
   public:
      // name_prefix is name appended with -## of thread.
      // short name_prefix (6 chars or under) is recommended as console_appender uses 9 chars for thread name
      /// @param delay_start do not spawn threads in constructor, wait for start() call
      named_thread_pool( std::string name_prefix, size_t num_threads, bool delay_start = false );

      // calls stop()
      ~named_thread_pool();

      boost::asio::io_context& get_executor() { return _ioc; }

      /// Spawn threads, can be re-started after stop().
      /// Assumes start()/stop() called from the same thread or externally protected.
      /// @throw assert_exception if already started and not stopped.
      void start();

      /// destroy work guard, stop io_context, join thread_pool
      void stop();

   private:
      using ioc_work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

      std::string                    _name_prefix;
      size_t                         _num_threads;
      std::vector<std::thread>       _thread_pool;
      boost::asio::io_context        _ioc;
      std::optional<ioc_work_t>      _ioc_work;
   };


   // async on thread_pool and return future
   template<typename F>
   auto async_thread_pool( boost::asio::io_context& thread_pool, F&& f ) {
      auto task = std::make_shared<std::packaged_task<decltype( f() )()>>( std::forward<F>( f ) );
      boost::asio::post( thread_pool, [task]() { (*task)(); } );
      return task->get_future();
   }

} } // eosio::chain


