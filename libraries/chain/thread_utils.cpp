#include <eosio/chain/thread_utils.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/exception/exception.hpp>

namespace eosio { namespace chain {

named_thread_pool::named_thread_pool( std::string name_prefix, size_t num_threads, on_except_t on_except, bool delay_start )
: _name_prefix( std::move(name_prefix) )
, _num_threads( num_threads )
, _ioc( num_threads )
, _on_except( std::move(on_except) )
{
   if( !delay_start ) {
      start();
   }
}

named_thread_pool::~named_thread_pool() {
   stop();
}

void named_thread_pool::start() {
   FC_ASSERT( !_ioc_work, "Thread pool already started" );
   _ioc_work.emplace( boost::asio::make_work_guard( _ioc ) );
   _ioc.restart();
   for( size_t i = 0; i < _num_threads; ++i ) {
      _thread_pool.emplace_back( [&ioc = _ioc, &name_prefix = _name_prefix, on_except = _on_except, i]() {
         std::string tn = name_prefix + "-" + std::to_string( i );
         try {
            fc::set_os_thread_name( tn );
            ioc.run();
         } catch( const fc::exception& e ) {
            if( on_except ) {
               on_except( e );
            } else {
               elog( "Exiting thread ${t} on exception: ${e}", ("t", tn)("e", e.to_detail_string()) );
            }
         } catch( const std::exception& e ) {
            fc::std_exception_wrapper se( FC_LOG_MESSAGE( warn, "${what}: ", ("what", e.what()) ),
                                          std::current_exception(), BOOST_CORE_TYPEID( e ).name(), e.what() );
            if( on_except ) {
               on_except( se );
            } else {
               elog( "Exiting thread ${t} on exception: ${e}", ("t", tn)("e", se.to_detail_string()) );
            }
         } catch( ... ) {
            if( on_except ) {
               fc::unhandled_exception ue( FC_LOG_MESSAGE( warn, "unknown exception" ), std::current_exception() );
               on_except( ue );
            } else {
               elog( "Exiting thread ${t} on unknown exception", ("t", tn) );
            }
         }
      } );
   }
}

void named_thread_pool::stop() {
   _ioc_work.reset();
   _ioc.stop();
   for( auto& t : _thread_pool ) {
      t.join();
   }
   _thread_pool.clear();
}


} } // eosio::chain
