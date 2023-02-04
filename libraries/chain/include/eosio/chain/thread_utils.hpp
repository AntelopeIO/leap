#pragma once

#include <eosio/chain/name.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger_config.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace eosio { namespace chain {

   inline constexpr uint64_t make_name_v( std::string_view str ) {
      return string_to_uint64_t(str);
   }

   /**
    * Wrapper class for thread pool of boost asio io_context run.
    * Also names threads so that tools like htop can see thread name.
    * Example: named_thread_pool<eosio::chain::make_name_v("chain")> thread_pool;
    * @param NamePrefix  is eosio::name appended with -## of thread.
    *                    A short name_prefix (6 chars or under) is recommended as console_appender uses 9 chars
    *                    for the thread name.
    */
   template<uint64_t NamePrefix>
   class named_thread_pool {
   public:
      using on_except_t = std::function<void(const fc::exception& e)>;

      explicit named_thread_pool()
            : _name_prefix(NamePrefix)
            , _ioc()
      {}

      ~named_thread_pool(){
         stop();
      }

      boost::asio::io_context& get_executor() { return _ioc; }

      /// Spawn threads, can be re-started after stop().
      /// Assumes start()/stop() called from the same thread or externally protected.
      /// @param num_threads is number of threads spawned
      /// @param on_except is the function to call if io_context throws an exception, is called from thread pool thread.
      ///                  if an empty function then logs and rethrows exception on thread which will terminate.
      /// @throw assert_exception if already started and not stopped.
      void start( size_t num_threads, on_except_t on_except ) {
         FC_ASSERT( !_ioc_work, "Thread pool already started" );
         _ioc_work.emplace( boost::asio::make_work_guard( _ioc ) );
         _ioc.restart();
         _thread_pool.reserve( num_threads );
         for( size_t i = 0; i < num_threads; ++i ) {
            _thread_pool.emplace_back( std::thread( &named_thread_pool::run_thread, this, i, on_except )  );
         }
      }

      /// destroy work guard, stop io_context, join thread_pool
      void stop() {
         _ioc_work.reset();
         _ioc.stop();
         for( auto& t : _thread_pool ) {
            t.join();
         }
         _thread_pool.clear();
      }

   private:
      void run_thread( size_t i, const on_except_t& on_except ) {
         // copying the name_prefix and i into lambda makes it available to be seen on the stack in the debugger
         std::string tn = _name_prefix.to_string() + "-" + std::to_string( i );
         try {
            fc::set_os_thread_name( tn );
            _ioc.run();
         } catch( const fc::exception& e ) {
            if( on_except ) {
               on_except( e );
            } else {
               elog( "Exiting thread ${t} on exception: ${e}", ("t", tn)("e", e.to_detail_string()) );
               throw;
            }
         } catch( const std::exception& e ) {
            fc::std_exception_wrapper se( FC_LOG_MESSAGE( warn, "${what}: ", ("what", e.what()) ),
                                          std::current_exception(), BOOST_CORE_TYPEID( e ).name(), e.what() );
            if( on_except ) {
               on_except( se );
            } else {
               elog( "Exiting thread ${t} on exception: ${e}", ("t", tn)("e", se.to_detail_string()) );
               throw;
            }
         } catch( ... ) {
            if( on_except ) {
               fc::unhandled_exception ue( FC_LOG_MESSAGE( warn, "unknown exception" ), std::current_exception() );
               on_except( ue );
            } else {
               elog( "Exiting thread ${t} on unknown exception", ("t", tn) );
               throw;
            }
         }
      }

   private:
      using ioc_work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

      eosio::chain::name             _name_prefix;
      boost::asio::io_context        _ioc;
      std::vector<std::thread>       _thread_pool;
      std::optional<ioc_work_t>      _ioc_work;
   };


   // async on io_context and return future
   template<typename F>
   auto post_async_task( boost::asio::io_context& ioc, F&& f ) {
      auto task = std::make_shared<std::packaged_task<decltype( f() )()>>( std::forward<F>( f ) );
      boost::asio::post( ioc, [task]() { (*task)(); } );
      return task->get_future();
   }

} } // eosio::chain


