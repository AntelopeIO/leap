#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>
#include <eosio/chain/config.hpp>

#include <fc/time.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>

namespace eosio::chain {

class subjective_billing {
private:

   struct trx_cache_entry {
      chain::transaction_id_type trx_id;
      chain::account_name        account;
      int64_t                    subjective_cpu_bill = 0;
      fc::time_point             expiry;
   };
   struct by_id;
   struct by_expiry;

   using trx_cache_index = bmi::multi_index_container<
         trx_cache_entry,
         indexed_by<
               bmi::hashed_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER( trx_cache_entry, chain::transaction_id_type, trx_id ) >,
               ordered_non_unique<tag<by_expiry>, BOOST_MULTI_INDEX_MEMBER( trx_cache_entry, fc::time_point, expiry ) >
         >
   >;

   using decaying_accumulator = chain::resource_limits::impl::exponential_decay_accumulator<>;

   struct subjective_billing_info {
      uint64_t              pending_cpu_us = 0;    // tracked cpu us for transactions that may still succeed in a block
      decaying_accumulator  expired_accumulator;   // accumulator used to account for transactions that have expired

      bool empty(uint32_t time_ordinal, uint32_t expired_accumulator_average_window) const {
         return pending_cpu_us == 0 && expired_accumulator.value_at(time_ordinal, expired_accumulator_average_window) == 0;
      }
   };

   using account_subjective_bill_cache = std::map<chain::account_name, subjective_billing_info>;

   bool                                      _disabled = false;
   trx_cache_index                           _trx_cache_index;
   account_subjective_bill_cache             _account_subjective_bill_cache;
   std::set<chain::account_name>             _disabled_accounts;
   uint32_t                                  _expired_accumulator_average_window = chain::config::account_cpu_usage_average_window_ms / subjective_time_interval_ms;

private:
   static uint32_t time_ordinal_for( const fc::time_point& t ) {
      auto ordinal = t.time_since_epoch().count() / (1000U * (uint64_t)subjective_time_interval_ms);
      EOS_ASSERT(ordinal <= std::numeric_limits<uint32_t>::max(), chain::tx_resource_exhaustion, "overflow of quantized time in subjective billing");
      return ordinal;
   }

   void remove_subjective_billing( const trx_cache_entry& entry, uint32_t time_ordinal ) {
      auto aitr = _account_subjective_bill_cache.find( entry.account );
      if( aitr != _account_subjective_bill_cache.end() ) {
         aitr->second.pending_cpu_us -= entry.subjective_cpu_bill;
         EOS_ASSERT( aitr->second.pending_cpu_us >= 0, chain::tx_resource_exhaustion,
                     "Logic error in subjective account billing ${a}", ("a", entry.account) );
         if( aitr->second.empty(time_ordinal, _expired_accumulator_average_window) ) _account_subjective_bill_cache.erase( aitr );
      }
   }

   void transition_to_expired( const trx_cache_entry& entry, uint32_t time_ordinal ) {
      auto aitr = _account_subjective_bill_cache.find( entry.account );
      if( aitr != _account_subjective_bill_cache.end() ) {
         aitr->second.pending_cpu_us -= entry.subjective_cpu_bill;
         aitr->second.expired_accumulator.add(entry.subjective_cpu_bill, time_ordinal, _expired_accumulator_average_window);
      }
   }

   void remove_subjective_billing( const chain::signed_block_ptr& block, uint32_t time_ordinal ) {
      if( !_trx_cache_index.empty() ) {
         for( const auto& receipt : block->transactions ) {
            if( std::holds_alternative<chain::packed_transaction>(receipt.trx) ) {
               const auto& pt = std::get<chain::packed_transaction>(receipt.trx);
               remove_subjective_billing( pt.id(), time_ordinal );
            }
         }
      }
   }

public: // public for tests
   static constexpr uint32_t subjective_time_interval_ms = 5'000;
   size_t get_account_cache_size() const {return _account_subjective_bill_cache.size();}
   void remove_subjective_billing( const chain::transaction_id_type& trx_id, uint32_t time_ordinal ) {
      auto& idx = _trx_cache_index.get<by_id>();
      auto itr = idx.find( trx_id );
      if( itr != idx.end() ) {
         remove_subjective_billing( *itr, time_ordinal );
         idx.erase( itr );
      }
   }

public:
   void disable() { _disabled = true; }
   void disable_account( chain::account_name a ) { _disabled_accounts.emplace( a ); }
   bool is_account_disabled(const chain::account_name& a ) const { return _disabled || _disabled_accounts.count( a ); }

   void subjective_bill( const chain::transaction_id_type& id, fc::time_point_sec expire,
                         const chain::account_name& first_auth, const fc::microseconds& elapsed )
   {
      if( !_disabled && !_disabled_accounts.count( first_auth ) ) {
         int64_t bill = std::max<int64_t>( 0, elapsed.count() );
         auto p = _trx_cache_index.emplace(
               trx_cache_entry{id,
                               first_auth,
                               bill,
                               expire.to_time_point()} );
         if( p.second ) {
            _account_subjective_bill_cache[first_auth].pending_cpu_us += bill;
         }
      }
   }

   void subjective_bill_failure( const chain::account_name& first_auth, const fc::microseconds& elapsed, const fc::time_point& now )
   {
      if( !_disabled && !_disabled_accounts.count( first_auth ) ) {
         int64_t bill = std::max<int64_t>( 0, elapsed.count() );
         const auto time_ordinal = time_ordinal_for(now);
         _account_subjective_bill_cache[first_auth].expired_accumulator.add(bill, time_ordinal, _expired_accumulator_average_window);
      }
   }

   int64_t get_subjective_bill( const chain::account_name& first_auth, const fc::time_point& now ) const {
      if( _disabled || _disabled_accounts.count( first_auth ) ) return 0;
      const auto time_ordinal = time_ordinal_for(now);
      const subjective_billing_info* sub_bill_info = nullptr;
      auto aitr = _account_subjective_bill_cache.find( first_auth );
      if( aitr != _account_subjective_bill_cache.end() ) {
         sub_bill_info = &aitr->second;
      }

      if (sub_bill_info) {
         int64_t sub_bill = sub_bill_info->pending_cpu_us + sub_bill_info->expired_accumulator.value_at(time_ordinal, _expired_accumulator_average_window );
         return sub_bill;
      } else {
         return 0;
      }
   }

   void on_block( fc::logger& log, const chain::signed_block_ptr& block, const fc::time_point& now ) {
      if( block == nullptr || _disabled ) return;
      const auto time_ordinal = time_ordinal_for(now);
      const auto orig_count = _account_subjective_bill_cache.size();
      remove_subjective_billing( block, time_ordinal );
      if (orig_count > 0) {
         fc_dlog( log, "Subjective billed accounts ${n} removed ${r}",
                  ("n", orig_count)("r", orig_count - _account_subjective_bill_cache.size()) );
      }
   }

   template <typename Yield>
   bool remove_expired( fc::logger& log, const fc::time_point& pending_block_time, const fc::time_point& now, Yield&& yield ) {
      bool exhausted = false;
      auto& idx = _trx_cache_index.get<by_expiry>();
      if( !idx.empty() ) {
         const auto time_ordinal = time_ordinal_for(now);
         const auto orig_count = _trx_cache_index.size();
         uint32_t num_expired = 0;

         while( !idx.empty() ) {
            if( yield() ) {
               exhausted = true;
               break;
            }
            auto b = idx.begin();
            if( b->expiry > pending_block_time ) break;
            transition_to_expired( *b, time_ordinal );
            idx.erase( b );
            num_expired++;
         }

         fc_dlog( log, "Processed ${n} subjective billed transactions, Expired ${expired}",
                  ("n", orig_count)( "expired", num_expired ) );
      }
      return !exhausted;
   }

   uint32_t get_expired_accumulator_average_window() const {
      return _expired_accumulator_average_window;
   }

   void set_expired_accumulator_average_window( fc::microseconds subjective_account_decay_time ) {
      _expired_accumulator_average_window =
        subjective_account_decay_time.count() / 1000 / subjective_time_interval_ms;
   }
};

} //eosio::chain
