#pragma once
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/block_timestamp.hpp>
#include <chainbase/chainbase.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <set>

namespace eosio { namespace chain {

   class deep_mind_handler;
   class fee_params_object;

   namespace resource_limits {
   namespace impl {
      template<typename T>
      struct ratio {
         static_assert(std::is_integral<T>::value, "ratios must have integral types");
         T numerator;
         T denominator;

         friend inline bool operator ==( const ratio& lhs, const ratio& rhs ) {
            return std::tie(lhs.numerator, lhs.denominator) == std::tie(rhs.numerator, rhs.denominator);
         }

         friend inline bool operator !=( const ratio& lhs, const ratio& rhs ) {
            return !(lhs == rhs);
         }
      };
   }

   using ratio = impl::ratio<uint64_t>;

   struct elastic_limit_parameters {
      uint64_t target;           // the desired usage
      uint64_t max;              // the maximum usage
      uint32_t periods;          // the number of aggregation periods that contribute to the average usage

      uint32_t max_multiplier;   // the multiplier by which virtual space can oversell usage when uncongested
      ratio    contract_rate;    // the rate at which a congested resource contracts its limit
      ratio    expand_rate;       // the rate at which an uncongested resource expands its limits

      void validate()const; // throws if the parameters do not satisfy basic sanity checks

      friend inline bool operator ==( const elastic_limit_parameters& lhs, const elastic_limit_parameters& rhs ) {
         return std::tie(lhs.target, lhs.max, lhs.periods, lhs.max_multiplier, lhs.contract_rate, lhs.expand_rate)
                  == std::tie(rhs.target, rhs.max, rhs.periods, rhs.max_multiplier, rhs.contract_rate, rhs.expand_rate);
      }

      friend inline bool operator !=( const elastic_limit_parameters& lhs, const elastic_limit_parameters& rhs ) {
         return !(lhs == rhs);
      }
   };

   struct account_resource_limit {
      int64_t used = 0; ///< quantity used in current window
      int64_t available = 0; ///< quantity available in current window (based upon fractional reserve)
      int64_t max = 0; ///< max per window under current congestion
      block_timestamp_type last_usage_update_time; ///< last usage timestamp
      int64_t current_used = 0;  ///< current usage according to the given timestamp
   };

   class resource_limits_manager {
      public:

         explicit resource_limits_manager(controller& c, chainbase::database& db)
         :_control(c),_db(db)
         {
         }

         void add_indices();
         void initialize_database();
         void add_fee_params_db();
         void add_to_snapshot( const snapshot_writer_ptr& snapshot ) const;
         void read_from_snapshot( const snapshot_reader_ptr& snapshot );

         void initialize_account( const account_name& account, bool is_trx_transient );
         void set_block_parameters( const elastic_limit_parameters& cpu_limit_parameters, const elastic_limit_parameters& net_limit_parameters );
         void set_fee_parameters(uint64_t cpu_fee_scaler, uint64_t free_block_cpu_threshold, uint64_t net_fee_scaler, uint64_t free_block_net_threshold);

         void update_account_usage( const flat_set<account_name>& accounts, uint32_t ordinal );
         void add_transaction_usage( const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, uint32_t ordinal, bool is_trx_transient = false );
         void add_transaction_usage_and_fees( const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, int64_t cpu_fee, int64_t net_fee, uint32_t ordinal, bool is_trx_transient = false );

         void add_pending_ram_usage( const account_name account, int64_t ram_delta, bool is_trx_transient = false );
         void verify_account_ram_usage( const account_name accunt )const;

         /// set_account_limits returns true if new ram_bytes limit is more restrictive than the previously set one
         bool set_account_limits( const account_name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight, bool is_trx_transient);
         void get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight) const;
         void config_account_fee_limits(const account_name& account, int64_t tx_fee_limit, int64_t account_fee_limit, bool is_trx_transient);
         void set_account_fee_limits( const account_name& account,int64_t net_weight_limit, int64_t cpu_weight_limit, bool is_trx_transient);

         bool is_unlimited_cpu( const account_name& account ) const;
         bool is_account_enable_charging_fee(const flat_set<account_name>& accounts) const;

         void process_account_limit_updates();
         void process_block_usage( uint32_t block_num );

         // accessors
         uint64_t get_total_cpu_weight() const;
         uint64_t get_total_net_weight() const;

         uint64_t get_virtual_block_cpu_limit() const;
         uint64_t get_virtual_block_net_limit() const;

         uint64_t get_block_cpu_limit() const;
         uint64_t get_block_net_limit() const;

         std::pair<int64_t, bool> get_account_cpu_limit( const account_name& name, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier ) const;
         std::pair<int64_t, bool> get_account_net_limit( const account_name& name, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier ) const;

         std::pair<account_resource_limit, bool>
         get_account_cpu_limit_ex( const account_name& name, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier, const std::optional<block_timestamp_type>& current_time={} ) const;
         std::pair<account_resource_limit, bool>
         get_account_net_limit_ex( const account_name& name, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier, const std::optional<block_timestamp_type>& current_time={} ) const;

         int64_t get_account_ram_usage( const account_name& name ) const;
         
         int64_t calculate_resource_fee(uint64_t resource_usage, uint64_t ema_block_resource, uint64_t free_block_resource_threshold, uint64_t max_block_resource, uint64_t resource_fee_scaler) const;
         void get_account_fee_consumption( const account_name& account, int64_t& net_weight_consumption, int64_t& cpu_weight_consumption) const;
         std::pair<int64_t, int64_t> get_account_available_fees( const account_name& account) const;
         std::pair<int64_t, int64_t> get_config_fee_limits( const account_name& account) const;
         int64_t get_cpu_usage_fee_to_bill( int64_t cpu_usage ) const;
         int64_t get_net_usage_fee_to_bill( int64_t net_usage ) const;

      private:
         const controller&    _control;
         chainbase::database&         _db;
         std::function<deep_mind_handler*(bool is_trx_transient)> _get_deep_mind_logger;
   };
} } } /// eosio::chain

FC_REFLECT( eosio::chain::resource_limits::account_resource_limit, (used)(available)(max)(last_usage_update_time)(current_used) )
FC_REFLECT( eosio::chain::resource_limits::ratio, (numerator)(denominator))
FC_REFLECT( eosio::chain::resource_limits::elastic_limit_parameters, (target)(max)(periods)(max_multiplier)(contract_rate)(expand_rate))
