#pragma once

#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/exceptions.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

namespace fc {
  inline std::size_t hash_value( const fc::sha256& v ) {
     return v._hash[3];
  }
}

namespace eosio { namespace chain {

using namespace boost::multi_index;

enum class trx_enum_type {
   unknown = 0,
   forked = 1,
   aborted = 2,
   incoming_api = 3,
   incoming_p2p = 4 // incoming_end() needs to be updated if this changes
};

using next_func_t = next_function<transaction_trace_ptr>;

struct unapplied_transaction {
   const transaction_metadata_ptr trx_meta;
   trx_enum_type                  trx_type = trx_enum_type::unknown;
   bool                           return_failure_trace = false;
   next_func_t                    next;

   const transaction_id_type& id()const { return trx_meta->id(); }
   fc::time_point_sec expiration()const { return trx_meta->packed_trx()->expiration(); }
};

/**
 * Track unapplied transactions for incoming, forked blocks, and aborted blocks.
 */
class unapplied_transaction_queue {
private:
   struct by_trx_id;
   struct by_type;
   struct by_expiry;

   typedef multi_index_container< unapplied_transaction,
      indexed_by<
         hashed_unique< tag<by_trx_id>,
               const_mem_fun<unapplied_transaction, const transaction_id_type&, &unapplied_transaction::id>
         >,
         ordered_non_unique< tag<by_type>, member<unapplied_transaction, trx_enum_type, &unapplied_transaction::trx_type> >,
         ordered_non_unique< tag<by_expiry>, const_mem_fun<unapplied_transaction, fc::time_point_sec, &unapplied_transaction::expiration> >
      >
   > unapplied_trx_queue_type;

   unapplied_trx_queue_type queue;
   uint64_t max_transaction_queue_size = 1024*1024*1024; // enforced for incoming
   uint64_t size_in_bytes = 0;
   size_t incoming_count = 0;

public:

   void set_max_transaction_queue_size( uint64_t v ) { max_transaction_queue_size = v; }

   bool empty() const {
      return queue.empty();
   }

   size_t size() const {
      return queue.size();
   }

   void clear() {
      queue.clear();
   }

   size_t incoming_size()const {
      return incoming_count;
   }

   transaction_metadata_ptr get_trx( const transaction_id_type& id ) const {
      auto itr = queue.get<by_trx_id>().find( id );
      if( itr == queue.get<by_trx_id>().end() ) return {};
      return itr->trx_meta;
   }

   template <typename Yield, typename Callback>
   bool clear_expired( const time_point& pending_block_time, Yield&& yield, Callback&& callback ) {
      auto& persisted_by_expiry = queue.get<by_expiry>();
      while( !persisted_by_expiry.empty() ) {
         const auto& itr = persisted_by_expiry.begin();
         if( itr->expiration().to_time_point() > pending_block_time ) {
            break;
         }
         if( yield() ) {
            return false;
         }
         callback( itr->trx_meta->packed_trx(), itr->trx_type );
         if( itr->next ) {
            itr->next( std::static_pointer_cast<fc::exception>(
                  std::make_shared<expired_tx_exception>(
                        FC_LOG_MESSAGE( error, "expired transaction ${id}, expiration ${e}, block time ${bt}",
                                        ("id", itr->id())("e", itr->trx_meta->packed_trx()->expiration())
                                        ("bt", pending_block_time) ) ) ) );
         }
         removed( itr );
         persisted_by_expiry.erase( itr );
      }
      return true;
   }

   void clear_applied( const block_state_ptr& bs ) {
      if( empty() ) return;
      auto& idx = queue.get<by_trx_id>();
      for( const auto& receipt : bs->block->transactions ) {
         if( std::holds_alternative<packed_transaction>(receipt.trx) ) {
            const auto& pt = std::get<packed_transaction>(receipt.trx);
            auto itr = idx.find( pt.id() );
            if( itr != idx.end() ) {
               if( itr->next ) {
                  itr->next( std::static_pointer_cast<fc::exception>( std::make_shared<tx_duplicate>(
                                FC_LOG_MESSAGE( info, "duplicate transaction ${id}", ("id", itr->trx_meta->id())))));
               }
               removed( itr );
               idx.erase( itr );
            }
         }
      }
   }

   void add_forked( const branch_type& forked_branch ) {
      // forked_branch is in reverse order
      for( auto ritr = forked_branch.rbegin(), rend = forked_branch.rend(); ritr != rend; ++ritr ) {
         const block_state_ptr& bsptr = *ritr;
         for( auto itr = bsptr->trxs_metas().begin(), end = bsptr->trxs_metas().end(); itr != end; ++itr ) {
            const auto& trx = *itr;
            auto insert_itr = queue.insert( { trx, trx_enum_type::forked } );
            if( insert_itr.second ) added( insert_itr.first );
         }
      }
   }

   void add_aborted( deque<transaction_metadata_ptr> aborted_trxs ) {
      for( auto& trx : aborted_trxs ) {
         auto insert_itr = queue.insert( { std::move( trx ), trx_enum_type::aborted } );
         if( insert_itr.second ) added( insert_itr.first );
      }
   }

   void add_incoming( const transaction_metadata_ptr& trx, bool api_trx, bool return_failure_trace, next_func_t next ) {
      auto itr = queue.get<by_trx_id>().find( trx->id() );
      if( itr == queue.get<by_trx_id>().end() ) {
         auto insert_itr = queue.insert(
               { trx, api_trx ? trx_enum_type::incoming_api : trx_enum_type::incoming_p2p, return_failure_trace, std::move( next ) } );
         if( insert_itr.second ) added( insert_itr.first );
      } else {
         if( itr->trx_meta == trx ) return; // same trx meta pointer
         if( next ) {
            next( std::static_pointer_cast<fc::exception>( std::make_shared<tx_duplicate>(
                  FC_LOG_MESSAGE( info, "duplicate transaction ${id}", ("id", trx->id()) ) ) ) );
         }
      }
   }

   using iterator = unapplied_trx_queue_type::index<by_type>::type::iterator;

   iterator begin() { return queue.get<by_type>().begin(); }
   iterator end() { return queue.get<by_type>().end(); }

   // forked, aborted
   iterator unapplied_begin() { return queue.get<by_type>().begin(); }
   iterator unapplied_end() { return queue.get<by_type>().upper_bound( trx_enum_type::aborted ); }

   iterator incoming_begin() { return queue.get<by_type>().lower_bound( trx_enum_type::incoming_api ); }
   iterator incoming_end() { return queue.get<by_type>().end(); } // if changed to upper_bound, verify usage performance

   iterator lower_bound( const transaction_id_type& id ) {
      auto itr = queue.get<by_trx_id>().find( id );
      if( itr == queue.get<by_trx_id>().end() ) return queue.get<by_type>().end();
      return queue.project<by_type>(itr);
   }

   /// caller's responsibility to call next() if applicable
   iterator erase( iterator itr ) {
      removed( itr );
      return queue.get<by_type>().erase( itr );
   }

private:
   template<typename Itr>
   void added( Itr itr ) {
      auto size = calc_size( itr->trx_meta );
      if( itr->trx_type == trx_enum_type::incoming_p2p || itr->trx_type == trx_enum_type::incoming_api ) {
         ++incoming_count;
         EOS_ASSERT( size_in_bytes + size < max_transaction_queue_size, tx_resource_exhaustion,
                     "Transaction ${id}, size ${s} bytes would exceed configured "
                     "incoming-transaction-queue-size-mb ${qs}, current queue size ${cs} bytes",
                     ("id", itr->trx_meta->id())("s", size)("qs", max_transaction_queue_size/(1024*1024))
                     ("cs", size_in_bytes) );
      }
      size_in_bytes += size;
   }

   template<typename Itr>
   void removed( Itr itr ) {
      if( itr->trx_type == trx_enum_type::incoming_p2p || itr->trx_type == trx_enum_type::incoming_api ) {
         --incoming_count;
      }
      size_in_bytes -= calc_size( itr->trx_meta );
   }

   static uint64_t calc_size( const transaction_metadata_ptr& trx ) {
      // packed_trx caches unpacked transaction so double
      return (trx->packed_trx()->get_unprunable_size() + trx->packed_trx()->get_prunable_size()) * 2 + sizeof( *trx );
   }

};

} } //eosio::chain
