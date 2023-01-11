#include <eosio/chain/thread_utils.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/exception/exception.hpp>

namespace eosio { namespace chain {

named_thread_pool::named_thread_pool( std::string name_prefix, size_t num_threads, bool delay_start )
: _name_prefix( std::move(name_prefix) )
, _num_threads( num_threads )
, _ioc( num_threads )
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
      _thread_pool.emplace_back( [&ioc = _ioc, &name_prefix = _name_prefix, i]() {
         std::string tn = name_prefix + "-" + std::to_string( i );
         fc::set_os_thread_name( tn );
         ioc.run();
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