#include <eosio/chain_plugin/trx_retry_db.hpp>

#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/controller.hpp>

#include <appbase/application.hpp>
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
   const transaction_metadata_ptr trx_meta;
   const uint16_t                 num_blocks = 0; // lib is lib_totem
   uint32_t                       block_num = 0;
   fc::variant                    trx_trace_v;
   fc::time_point                 last_try;

   const transaction_id_type& id()const { return trx_meta->id(); }
   fc::time_point_sec expiry()const { return trx_meta->packed_trx()->expiration(); }

   uint32_t ready_block_num()const {
      if( block_num == 0 ) return std::numeric_limits<uint32_t>::max() - 1; // group not seen in middle
      if( num_blocks == lib_totem ) return std::numeric_limits<uint32_t>::max();   // lib at the end
      return block_num + num_blocks;
   }

   bool waiting_for_lib()const {
      return num_blocks == lib_totem;
   }

   bool is_ready()const {
      return block_num != 0;
   }

   // for fc::tracked_storage, x3 for trx_meta as a very rough guess of trace variant size
   size_t memory_size()const { return trx_meta->get_estimated_size() * 3 + sizeof(*this); }

   tracked_transaction(const tracked_transaction&) = delete;
   tracked_transaction() = delete;
   tracked_transaction& operator=(const tracked_transaction&) = delete;
   tracked_transaction(tracked_transaction&&) = default;
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
                  member<tracked_transaction, uint32_t, &tracked_transaction::block_num>, std::greater<>
            >,
            ordered_non_unique<tag<by_last_try>,
                  member<tracked_transaction, fc::time_point, &tracked_transaction::last_try>
            >
      >
>;

} // anonymous namespace

namespace eosio::chain_apis {

struct trx_retry_db_impl {
   explicit trx_retry_db_impl(const chain::controller& controller)
   : _controller(controller)
   , _transaction_ack_channel(appbase::app().get_channel<chain::plugin_interface::compat::channels::transaction_ack>())
   {}

   /**
    * Hooked up to callback of chain_plugin send_transaction2 api if enabled
    * @param trx_meta
    */
   void track_transaction( transaction_metadata_ptr trx_meta, uint16_t num_blocks, bool lib ) {
      auto i = _tracked_trxs.index().get<by_trx_id>().find( trx_meta->id() );
      if( i == _tracked_trxs.index().end() ) {
         _tracked_trxs.insert( {std::move(trx_meta), lib ? lib_totem : num_blocks, 0, {}, fc::time_point::now()} );
      } else {
         // already tracking trx_meta
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
            tt.last_try = fc::time_point::maximum(); // do not retry if already received in a block
            try {
               // send_transaction trace output format.
               // Convert to variant with abi here and now because abi could change in very next transaction.
               // Alternatively, we could store off all the abis needed and do the conversion later, but as this is designed
               // to run on an API node, probably the best trade off to perform the abi serialization during block processing.
               tt.trx_trace_v = control.to_variant_with_abi( *trace, abi_serializer::create_yield_function( abi_max_time ) );
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

   void on_accepted_block(const chain::block_state_ptr& bsp ) {
      // good time to perform processing
      ack_ready_trxs_by_block_num( bsp->block_num );
      retry_trxs();
      clear_expired( bsp->block->timestamp );
   }

   void on_irreversible_block(const chain::block_state_ptr& bsp ) {
      ack_ready_trxs_by_lib( bsp->block_num );
   }

private:

   void rollback_to( uint32_t block_num ) {
      const auto& idx = _tracked_trxs.index().get<by_block_num>();
      // determine what to rollback
      std::vector<decltype(_tracked_trxs.index().project<0>(idx.begin()))> to_process;
      for( auto i = idx.begin(); i != idx.end(); ++i ) {
         // called on block_start, so any block_num greater or equal have been rolled back
         if( i->block_num < block_num ) break;

         to_process.emplace_back( _tracked_trxs.index().project<0>( i ) );
      }
      // perform rollback
      for( auto& i : to_process ) {
         _tracked_trxs.modify( i, [&]( tracked_transaction& tt ) {
            // if forked out, then need to retry. Estimate last_try via block_num it was in.
            tt.last_try = fc::time_point::now() - fc::microseconds( (i->block_num - tt.block_num) * chain::config::block_interval_us );
            tt.block_num = 0;
            tt.trx_trace_v.clear();
         } );
      }
   }

   void retry_trxs() {
      const auto& idx = _tracked_trxs.index().get<by_last_try>();
      auto now = fc::time_point::now();
      // determine what to retry
      std::vector<decltype(_tracked_trxs.index().project<0>(idx.begin()))> to_process;
      for( auto i = idx.begin(); i != idx.end(); ++i ) {
         if( i->is_ready() ) break;

         if( i->last_try + _retry_interval >= now ) {
            to_process.emplace_back( _tracked_trxs.index().project<0>( i ) );
         }
      }
      // retry
      for( auto& i: to_process ) {
         _transaction_ack_channel.publish(
               appbase::priority::low, std::pair<fc::exception_ptr, transaction_metadata_ptr>( nullptr, i->trx_meta ) );
         _tracked_trxs.modify( i, [&]( tracked_transaction& tt ) {
            tt.last_try = now;
         } );
      }
   }

   void ack_ready_trxs_by_block_num( uint32_t block_num ) {
      const auto& idx = _tracked_trxs.index().get<by_ready_block_num>();
      // determine what to ack
      std::vector<decltype(idx.begin())> to_process;
      for( auto i = idx.begin(); i != idx.end(); ) {
         if( !i->is_ready() ) break;
         // if we have reached requested block height then ack to user
         if( i->ready_block_num() >= block_num ) {
            to_process.emplace_back( i );
            // todo: i->next();
            _tracked_trxs.erase( i->id() );
         }
      }
      // ack
      for( auto& i: to_process ) {
         // todo: i->next();
         _tracked_trxs.erase( i->id() );
      }
   }

   void ack_ready_trxs_by_lib( uint32_t block_num ) {
      const auto& idx = _tracked_trxs.index().get<by_block_num>();
      // determine what to ack
      std::vector<decltype(idx.begin())> to_process;
      for( auto i = idx.begin(); i != idx.end(); ++i ) {
         if( i->block_num < block_num ) break;
         to_process.emplace_back( i );
      }
      // ack
      for( auto& i: to_process ) {
         // todo: i->next();
         _tracked_trxs.erase( i->id() );
      }
   }

   bool clear_expired(const block_timestamp_type& block_timestamp) {
      const fc::time_point block_time = block_timestamp;
      auto& idx = _tracked_trxs.index().get<by_expiry>();
      while( !idx.empty() ) {
         auto itr = idx.begin();
         if( itr->expiry() > block_time ) {
            break;
         }
// todo:
//         if( itr->next ) {
//            itr->next( std::static_pointer_cast<fc::exception>(
//                  std::make_shared<expired_tx_exception>(
//                        FC_LOG_MESSAGE( error, "expired transaction ${id}, expiration ${e}, block time ${bt}",
//                                        ("id", itr->id())("e", itr->trx_meta->packed_trx()->expiration())
//                                        ("bt", block_timestamp) ) ) ) );
//         }
         _tracked_trxs.erase( itr->id() );
      }
      return true;
   }


private:
   const chain::controller& _controller;               ///< the controller to read data from
   chain::plugin_interface::compat::channels::transaction_ack::channel_type& _transaction_ack_channel;
   const fc::microseconds _abi_serializer_max_time;  ///< the maximum time to allow abi_serialization to run
   fc::tracked_storage<tracked_transaction_index_t> _tracked_trxs;
   size_t _max_mem_usage_size = 0;
   fc::microseconds _retry_interval = fc::minutes( 1 );
};

trx_retry_db::trx_retry_db( const chain::controller& controller )
:_impl(std::make_unique<trx_retry_db_impl>(controller))
{
}

trx_retry_db::~trx_retry_db() = default;

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

void trx_retry_db::on_accepted_block(const chain::block_state_ptr& block ) {
   try {
      _impl->on_accepted_block(block);
   } FC_LOG_AND_DROP(("trx retry accepted_block ERROR"));
}

void trx_retry_db::on_irreversible_block(const chain::block_state_ptr& block ) {
   try {
      _impl->on_irreversible_block(block);
   } FC_LOG_AND_DROP(("trx retry irreversible_block ERROR"));
}

} // namespace eosio::chain_apis
