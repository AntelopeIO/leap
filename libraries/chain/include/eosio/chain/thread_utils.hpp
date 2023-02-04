#pragma once

#include <eosio/chain/name.hpp>
#include <fc/exception/exception.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/post.hpp>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace eosio { namespace chain {

   /**
    * Wrapper class for thread pool of boost asio io_context run.
    * Also names threads so that tools like htop can see thread name.
    */
   class named_thread_pool {
   public:
      using on_except_t = std::function<void(const fc::exception& e)>;

      /// @param name_prefix is name appended with -## of thread.
      ///                    A short name_prefix (6 chars or under) is recommended as console_appender uses 9 chars
      ///                    for the thread name.
      explicit named_thread_pool( eosio::chain::name name_prefix );

      /// calls stop()
      ~named_thread_pool();

      auto& get_executor() { return _ioc; }

      /// Spawn threads, can be re-started after stop().
      /// Assumes start()/stop() called from the same thread or externally protected.
      /// @param num_threads is number of threads spawned
      /// @param on_except is the function to call if io_context throws an exception, is called from thread pool thread.
      ///                  if an empty function then logs and rethrows exception on thread which will terminate.
      /// @throw assert_exception if already started and not stopped.
      void start( size_t num_threads, on_except_t on_except );

      /// destroy work guard, stop io_context, join thread_pool
      void stop();

   private:
      using ioc_work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

      eosio::chain::name             _name_prefix;
      boost::asio::io_context        _ioc;
      std::vector<std::thread>       _thread_pool;
      std::optional<ioc_work_t>      _ioc_work;
   };

   class named_thread {
   public:
      /// @param name_prefix is name appended with -1 for thread name.
      ///                    A short name_prefix (6 chars or under) is recommended as console_appender uses 9 chars
      ///                    for the thread name.
      explicit named_thread( eosio::chain::name name_prefix )
      : _pool( name_prefix ) {}

      /// stop before destroying strand
      ~named_thread() { stop(); }

      auto& get_executor() { return _strand; }

      /// Spawn thread, can be re-started after stop().
      /// Assumes start()/stop() called from the same thread or externally protected.
      /// @param on_except is the function to call if io_context throws an exception, is called from thread pool thread.
      ///                  if an empty function then logs and rethrows exception on thread which will terminate.
      /// @throw assert_exception if already started and not stopped.
      void start( named_thread_pool::on_except_t on_except ) { _pool.start( 1, std::move(on_except) ); }

      /// destroy work guard, stop io_context, join thread_pool
      void stop() { _pool.stop(); }

   private:
      named_thread_pool _pool;
      boost::asio::io_context::strand _strand{_pool.get_executor()};
   };


   // async on io_context and return future
   template<typename F>
   auto post_async_task( boost::asio::io_context& ioc, F&& f ) {
      auto task = std::make_shared<std::packaged_task<decltype( f() )()>>( std::forward<F>( f ) );
      boost::asio::post( ioc, [task]() { (*task)(); } );
      return task->get_future();
   }

} } // eosio::chain


