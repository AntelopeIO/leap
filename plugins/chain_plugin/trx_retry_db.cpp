#include <eosio/chain_plugin/trx_retry_db.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include <eosio/chain/types.hpp>
#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/controller.hpp>

#include <eosio/chain/application.hpp>
#include <eosio/chain/plugin_interface.hpp>

#include <fc/container/tracked_storage.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>


using namespace eosio;
using namespace eosio::chain;
using namespace eosio::chain::literals;
using namespace boost::multi_index;

namespace {

constexpr uint16_t lib_totem = std::numeric_limits<uint16_t>::max();

struct tracked_transaction {
   const packed_transaction_ptr                              ptrx;
   const uint16_t                                            num_blocks = 0; // lib is lib_totem
   uint32_t                                                  block_num = 0;
   fc::variant                                               trx_trace_v;
   fc::time_point                                            last_try;
   next_function<std::unique_ptr<fc::variant>>               next;

   const transaction_id_type& id()const { return ptrx->id(); }
   fc::time_point_sec expiry()const { return ptrx->expiration(); }

   uint32_t ready_block_num()const {
      if( block_num == 0 ) return std::numeric_limits<uint32_t>::max() - 1; // group not seen in middle
      if( num_blocks == lib_totem ) return std::numeric_limits<uint32_t>::max();   // lib at the end
      return block_num + num_blocks;
   }

   fc::time_point last_try_time()const {
      if( block_num != 0 ) return fc::time_point::maximum();
      return last_try;
   }

   bool is_ready()const {
      return block_num != 0;
   }

   size_t memory_size()const { return ptrx->get_estimated_size() + trx_trace_v.estimated_size() + sizeof(*this); }
};

struct by_trx_id;
struct by_expiry;
struct by_ready_block_num;
struct by_block_num;
struct by_last_try;

using tracked_transaction_index_t = multi_index_container<tracked_transaction,
      indexed_by<
            hashed_unique<tag<by_trx_id>,
                  const_mem_fun<tracked_transaction, const transaction_id_type&, &tracked_transaction::id>, std::hash<transaction_id_type>
            >,
            ordered_non_unique<tag<by_expiry>,
                  const_mem_fun<tracked_transaction, fc::time_point_sec, &tracked_transaction::expiry>
            >,
            ordered_non_unique<tag<by_ready_block_num>,
                  const_mem_fun<tracked_transaction, uint32_t, &tracked_transaction::ready_block_num>
            >,
            ordered_non_unique<tag<by_block_num>,
                  member<tracked_transaction, uint32_t, &tracked_transaction::block_num>
            >,
            ordered_non_unique<tag<by_last_try>,
                  const_mem_fun<tracked_transaction, fc::time_point, &tracked_transaction::last_try_time>
            >
      >
>;

} // anonymous namespace

namespace eosio::chain_apis {

struct trx_retry_db_impl {
   explicit trx_retry_db_impl(const chain::controller& controller, size_t max_mem_usage_size,
                              fc::microseconds retry_interval, fc::microseconds max_expiration_time,
                              fc::microseconds abi_serializer_max_time)
   : _controller(controller)
   , _transaction_ack_channel(appbase::app().get_channel<chain::plugin_interface::compat::channels::transaction_ack>())
   , _abi_serializer_max_time(abi_serializer_max_time)
   , _max_mem_usage_size(max_mem_usage_size)
   , _retry_interval(retry_interval)
   , _max_expiration_time(max_expiration_time)
   {}

   const fc::microseconds& get_max_expiration()const {
      return _max_expiration_time;
   }

   size_t size()const {
      return _tracked_trxs.index().size();
   }

   void track_transaction( packed_transaction_ptr ptrx, std::optional<uint16_t> num_blocks, next_function<std::unique_ptr<fc::variant>> next ) {
      EOS_ASSERT( _tracked_trxs.memory_size() < _max_mem_usage_size, tx_resource_exhaustion,
                  "Transaction exceeded  transaction-retry-max-storage-size-gb limit: ${m} bytes", ("m", _tracked_trxs.memory_size()) );
      auto i = _tracked_trxs.index().get<by_trx_id>().find( ptrx->id() );
      if( i == _tracked_trxs.index().end() ) {
         _tracked_trxs.insert( {std::move(ptrx),
                                !num_blocks.has_value() ? lib_totem : *num_blocks,
                                0,
                                {},
                                fc::time_point::now(),
                                std::move(next)} );
      } else {
         // already tracking transaction
      }
   }

   void on_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx ) {
      if( !trace->receipt ) return;
      // include only executed incoming transactions.
      // soft_fail not included as only interested in incoming
      if(trace->receipt->status != chain::transaction_receipt_header::executed) {
         return;
      }
      // only incoming
      if( trace->scheduled ) return;
      // Only want transactions in a block, if no producer id then not in a block
      if( !trace->producer_block_id ) return;
      // Don't care about implicit
      if( chain::is_onblock( *trace ) ) return;

      // Is this a transaction we are tracking
      auto& idx = _tracked_trxs.index().get<by_trx_id>();
      auto itr = idx.find(trace->id);
      if( itr != idx.end() ) {
         _tracked_trxs.modify( itr, [&trace, &control=_controller, &abi_max_time=_abi_serializer_max_time]( tracked_transaction& tt ) {
            tt.block_num = trace->block_num;
            try {
               // send_transaction trace output format.
               // Convert to variant with abi here and now because abi could change in very next transaction.
               // Alternatively, we could store off all the abis needed and do the conversion later, but as this is designed
               // to run on an API node, probably the best trade off to perform the abi serialization during block processing.
               auto resolver = get_serializers_cache(control, trace, abi_max_time);
               tt.trx_trace_v.clear();
               abi_serializer::to_variant(*trace, tt.trx_trace_v, resolver, abi_max_time);
            } catch( chain::abi_exception& ) {
               tt.trx_trace_v = *trace;
            }
         } );
      }
   }

   void on_block_start( uint32_t block_num ) {
      // on forks rollback any accepted block transactions
      rollback_to( block_num );
   }

   void on_accepted_block( uint32_t block_num ) {
      // good time to perform processing
      ack_ready_trxs_by_block_num( block_num );
      retry_trxs();
   }

   void on_irreversible_block( const chain::signed_block_ptr& block ) {
      ack_ready_trxs_by_lib( block->block_num() );
      clear_expired( block->timestamp );
   }

private:

   void rollback_to( uint32_t block_num ) {
      const auto& idx = _tracked_trxs.index().get<by_block_num>();
      // determine what to rollback
      deque<decltype(_tracked_trxs.index().project<0>(idx.begin()))> to_process;
      for( auto i = idx.rbegin(); i != idx.rend(); ++i ) {
         // called on block_start, so any block_num greater or equal have been rolled back
         if( i->block_num < block_num ) break;

         auto ii = i.base(); // switch to forward iterator, then -- to get back to item
         to_process.emplace_back( _tracked_trxs.index().project<0>( --ii ) );
      }
      // perform rollback
      auto now = fc::time_point::now();
      for( auto& i : to_process ) {
         _tracked_trxs.modify( i, [&]( tracked_transaction& tt ) {
            // if forked out, then need to retry, which will happen according to last_try.
            // if last_try would cause it to immediately resend, then push it out 10 seconds to allow time for
            // fork-switch to complete.
            if( tt.last_try + _retry_interval <= now ) {
               tt.last_try += fc::seconds( 10 );
            }
            tt.block_num = 0;
            tt.trx_trace_v.clear();
         } );
      }
   }

   void retry_trxs() {
      const auto& idx = _tracked_trxs.index().get<by_last_try>();
      auto now = fc::time_point::now();
      // determine what to retry
      deque<decltype(_tracked_trxs.index().project<0>(idx.begin()))> to_process;
      for( auto i = idx.begin(); i != idx.end(); ++i ) {
         if( i->is_ready() ) break;

         if( i->last_try + _retry_interval <= now ) {
            to_process.emplace_back( _tracked_trxs.index().project<0>( i ) );
         }
      }
      // retry
      for( auto& i: to_process ) {
         _transaction_ack_channel.publish(
               appbase::priority::low, std::pair<fc::exception_ptr, packed_transaction_ptr>( nullptr, i->ptrx ) );
         dlog( "retry send trx ${id}", ("id", i->ptrx->id()) );
         _tracked_trxs.modify( i, [&]( tracked_transaction& tt ) {
            tt.last_try = now;
         } );
      }
   }

   void ack_ready_trxs_by_block_num( uint32_t block_num ) {
      const auto& idx = _tracked_trxs.index().get<by_ready_block_num>();
      // if we have reached requested block height then ack to user
      deque<decltype(_tracked_trxs.index().project<0>(idx.begin()))> to_process;
      auto end = idx.upper_bound(block_num);
      for( auto i = idx.begin(); i != end; ++i ) {
         to_process.emplace_back( _tracked_trxs.index().project<0>( i ) );
      }
      // ack
      for( auto& i: to_process ) {
         _tracked_trxs.modify( i, [&]( tracked_transaction& tt ) {
            tt.next( std::make_unique<fc::variant>( std::move( tt.trx_trace_v ) ) );
            tt.trx_trace_v.clear();
         } );
         _tracked_trxs.erase( i );
      }
   }

   void ack_ready_trxs_by_lib( uint32_t lib_block_num ) {
      const auto& idx = _tracked_trxs.index().get<by_block_num>();
      // determine what to ack
      deque<decltype(_tracked_trxs.index().project<0>(idx.begin()))> to_process;
      auto end = idx.upper_bound(lib_block_num); // process until lib_block_num
      for( auto i = idx.lower_bound(1); i != end; ++i ) { // skip over not ready, block_num == 0
         to_process.emplace_back( _tracked_trxs.index().project<0>( i ) );
      }
      // ack
      for( auto& i: to_process ) {
         _tracked_trxs.modify( i, [&]( tracked_transaction& tt ) {
            tt.next( std::make_unique<fc::variant>( std::move( tt.trx_trace_v ) ) );
            tt.trx_trace_v.clear();
         } );
         _tracked_trxs.erase( i );
      }
   }

   void clear_expired(const block_timestamp_type& block_timestamp) {
      const fc::time_point block_time = block_timestamp;
      auto& idx = _tracked_trxs.index().get<by_expiry>();
      while( !idx.empty() ) {
         auto itr = idx.begin();
         if( itr->expiry().to_time_point() > block_time ) {
            break;
         }
         itr->next( std::static_pointer_cast<fc::exception>(
               std::make_shared<expired_tx_exception>(
                     FC_LOG_MESSAGE( error, "expired retry transaction ${id}, expiration ${e}, block time ${bt}",
                                     ("id", itr->id())("e", itr->ptrx->expiration())
                                     ("bt", block_timestamp) ) ) ) );
         _tracked_trxs.erase( _tracked_trxs.index().project<0>( itr ) );
      }
   }

private:
   const chain::controller& _controller; ///< the controller to read data from
   chain::plugin_interface::compat::channels::transaction_ack::channel_type& _transaction_ack_channel;
   const fc::microseconds _abi_serializer_max_time; ///< the maximum time to allow abi_serialization to run
   const size_t _max_mem_usage_size; ///< maximum size allowed for _tracked_trxs
   const fc::microseconds _retry_interval; ///< how often to resend not seen transactions
   const fc::microseconds _max_expiration_time; ///< limit to expiration on transactions that are tracked
   fc::tracked_storage<tracked_transaction_index_t> _tracked_trxs;
};

trx_retry_db::trx_retry_db( const chain::controller& controller, size_t max_mem_usage_size,
                            fc::microseconds retry_interval, fc::microseconds max_expiration_time,
                            fc::microseconds abi_serializer_max_time )
:_impl(std::make_unique<trx_retry_db_impl>(controller, max_mem_usage_size, retry_interval, max_expiration_time, abi_serializer_max_time))
{
}

trx_retry_db::~trx_retry_db() = default;

void trx_retry_db::track_transaction( chain::packed_transaction_ptr ptrx, std::optional<uint16_t> num_blocks, next_function<std::unique_ptr<fc::variant>> next ) {
   _impl->track_transaction( std::move( ptrx ), num_blocks, next );
}

fc::time_point_sec trx_retry_db::get_max_expiration_time()const {
   // conversion from time_point to time_point_sec rounds down, round up to nearest second to avoid appearing expired
   return fc::time_point_sec{fc::time_point::now() + _impl->get_max_expiration() + fc::microseconds(999'999)};
}

size_t trx_retry_db::size()const {
   return _impl->size();
}

void trx_retry_db::on_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx ) {
   try {
      _impl->on_applied_transaction(trace, ptrx);
   } FC_LOG_AND_DROP(("trx retry on_applied_transaction ERROR"));
}

void trx_retry_db::on_block_start( uint32_t block_num ) {
   try {
      _impl->on_block_start(block_num);
   } FC_LOG_AND_DROP(("trx retry block_start ERROR"));
}

void trx_retry_db::on_accepted_block( uint32_t block_num ) {
   try {
      _impl->on_accepted_block(block_num);
   } FC_LOG_AND_DROP(("trx retry accepted_block ERROR"));
}

void trx_retry_db::on_irreversible_block(const chain::signed_block_ptr& block) {
   try {
      _impl->on_irreversible_block(block);
   } FC_LOG_AND_DROP(("trx retry irreversible_block ERROR"));
}

} // namespace eosio::chain_apis
