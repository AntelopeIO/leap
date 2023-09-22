#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <algorithm>

namespace eosio { namespace chain { namespace resource_limits {

using resource_index_set = index_set<
   resource_limits_index,
   resource_usage_index,
   resource_limits_state_index,
   resource_limits_config_index,
   fee_params_index,
   fee_limits_index
>;

static_assert( config::rate_limiting_precision > 0, "config::rate_limiting_precision must be positive" );

static uint64_t update_elastic_limit(uint64_t current_limit, uint64_t average_usage, const elastic_limit_parameters& params) {
   uint64_t result = current_limit;
   if (average_usage > params.target ) {
      result = result * params.contract_rate;
   } else {
      result = result * params.expand_rate;
   }
   return std::min(std::max(result, params.max), params.max * params.max_multiplier);
}

void elastic_limit_parameters::validate()const {
   // At the very least ensure parameters are not set to values that will cause divide by zero errors later on.
   // Stricter checks for sensible values can be added later.
   EOS_ASSERT( periods > 0, resource_limit_exception, "elastic limit parameter 'periods' cannot be zero" );
   EOS_ASSERT( contract_rate.denominator > 0, resource_limit_exception, "elastic limit parameter 'contract_rate' is not a well-defined ratio" );
   EOS_ASSERT( expand_rate.denominator > 0, resource_limit_exception, "elastic limit parameter 'expand_rate' is not a well-defined ratio" );
}


void resource_limits_state_object::update_virtual_cpu_limit( const resource_limits_config_object& cfg ) {
   //idump((average_block_cpu_usage.average()));
   virtual_cpu_limit = update_elastic_limit(virtual_cpu_limit, average_block_cpu_usage.average(), cfg.cpu_limit_parameters);
   //idump((virtual_cpu_limit));
}

void resource_limits_state_object::update_virtual_net_limit( const resource_limits_config_object& cfg ) {
   virtual_net_limit = update_elastic_limit(virtual_net_limit, average_block_net_usage.average(), cfg.net_limit_parameters);
}

void resource_limits_manager::add_indices() {
   resource_index_set::add_indices(_db);
}

void resource_limits_manager::initialize_database() {
   const auto& config = _db.create<resource_limits_config_object>([](resource_limits_config_object& config){
      // see default settings in the declaration
   });

   const auto& state = _db.create<resource_limits_state_object>([&config](resource_limits_state_object& state){
      // see default settings in the declaration

      // start the chain off in a way that it is "congested" aka slow-start
      state.virtual_cpu_limit = config.cpu_limit_parameters.max;
      state.virtual_net_limit = config.net_limit_parameters.max;
   });

   // At startup, no transaction specific logging is possible
   if (auto dm_logger = _control.get_deep_mind_logger(false)) {
      dm_logger->on_init_resource_limits(config, state);
   }
}

void resource_limits_manager::add_fee_params_db() {
   const auto& fee_params = _db.create<fee_params_object>([](fee_params_object& fee_params){
      // see default settings in the declaration
   });
   if (auto dm_logger = _control.get_deep_mind_logger(false)) {
      dm_logger->on_init_fee_params(fee_params);
   }
}

void resource_limits_manager::add_to_snapshot( const snapshot_writer_ptr& snapshot ) const {
   resource_index_set::walk_indices([this, &snapshot]( auto utils ){
      snapshot->write_section<typename decltype(utils)::index_t::value_type>([this]( auto& section ){
         decltype(utils)::walk(_db, [this, &section]( const auto &row ) {
            section.add_row(row, _db);
         });
      });
   });
}

void resource_limits_manager::read_from_snapshot( const snapshot_reader_ptr& snapshot ) {

   chain_snapshot_header header;
   snapshot->read_section<chain_snapshot_header>([this, &header]( auto &section ){
      section.read_row(header, _db);
      header.validate();
   });

   resource_index_set::walk_indices([this, &snapshot, &header]( auto utils ){
      using value_t = typename decltype(utils)::index_t::value_type;
      if (std::is_same<value_t, fee_params_object>::value || std::is_same<value_t, fee_limits_object>::value) {
         if (header.version >= 8) {
            snapshot->read_section<value_t>([this]( auto& section ) {
               bool more = !section.empty();
               while(more) {
                  decltype(utils)::create(_db, [this, &section, &more]( auto &row ) {
                     more = section.read_row(row, _db);
                  });
               }
            });
         }
         return;
      }

      snapshot->read_section<value_t>([this]( auto& section ) {
         bool more = !section.empty();
         while(more) {
            decltype(utils)::create(_db, [this, &section, &more]( auto &row ) {
               more = section.read_row(row, _db);
            });
         }
      });
   });
}

void resource_limits_manager::initialize_account(const account_name& account, bool is_trx_transient) {
   const auto& limits = _db.create<resource_limits_object>([&]( resource_limits_object& bl ) {
      bl.owner = account;
   });

   const auto& usage = _db.create<resource_usage_object>([&]( resource_usage_object& bu ) {
      bu.owner = account;
   });
   if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
      dm_logger->on_newaccount_resource_limits(limits, usage);
   }

   if(_control.is_builtin_activated( builtin_protocol_feature_t::transaction_fee )){
      const auto& fee_limits = _db.create<fee_limits_object>([&]( fee_limits_object& fl ) {
         fl.owner = account;
      });
      if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
         dm_logger->on_init_account_fee_limits(fee_limits);
      }
   }
}

void resource_limits_manager::set_block_parameters(const elastic_limit_parameters& cpu_limit_parameters, const elastic_limit_parameters& net_limit_parameters ) {
   cpu_limit_parameters.validate();
   net_limit_parameters.validate();
   const auto& config = _db.get<resource_limits_config_object>();
   if( config.cpu_limit_parameters == cpu_limit_parameters && config.net_limit_parameters == net_limit_parameters )
      return;
   _db.modify(config, [&](resource_limits_config_object& c){
      c.cpu_limit_parameters = cpu_limit_parameters;
      c.net_limit_parameters = net_limit_parameters;

      // set_block_parameters is called by controller::finalize_block,
      // where transaction specific logging is not possible
      if (auto dm_logger = _control.get_deep_mind_logger(false)) {
         dm_logger->on_update_resource_limits_config(c);
      }
   });
}

void resource_limits_manager::set_fee_parameters(uint64_t cpu_fee_scaler, uint64_t free_block_cpu_threshold, uint64_t net_fee_scaler, uint64_t free_block_net_threshold) {
   const auto& config = _db.get<resource_limits_config_object>();
   EOS_ASSERT( free_block_cpu_threshold < config.cpu_limit_parameters.max, resource_limit_exception, "free_block_cpu_threshold must be lower maximum cpu_limit_parameters" );
   EOS_ASSERT( free_block_net_threshold < config.net_limit_parameters.max, resource_limit_exception, "free_block_net_threshold must be lower maximum net_limit_parameters" );

   const auto& fee_params = _db.get<fee_params_object>();
   if( fee_params.cpu_fee_scaler == cpu_fee_scaler && fee_params.free_block_cpu_threshold == free_block_cpu_threshold && fee_params.net_fee_scaler == net_fee_scaler && fee_params.free_block_net_threshold == free_block_net_threshold )
   {
      return;
   }
   _db.modify(fee_params, [&](fee_params_object& c){
      c.cpu_fee_scaler = cpu_fee_scaler;
      c.free_block_cpu_threshold = free_block_cpu_threshold;
      c.net_fee_scaler = net_fee_scaler;
      c.free_block_net_threshold = free_block_net_threshold;

      if (auto dm_logger = _control.get_deep_mind_logger(false)) {
         dm_logger->on_update_fee_params(c);
      }
   });
}

void resource_limits_manager::update_account_usage(const flat_set<account_name>& accounts, uint32_t time_slot ) {
   const auto& config = _db.get<resource_limits_config_object>();
   for( const auto& a : accounts ) {
      const auto& usage = _db.get<resource_usage_object,by_owner>( a );
      _db.modify( usage, [&]( auto& bu ){
          bu.net_usage.add( 0, time_slot, config.account_net_usage_average_window );
          bu.cpu_usage.add( 0, time_slot, config.account_cpu_usage_average_window );
      });
   }
}

void resource_limits_manager::add_transaction_usage(const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, uint32_t time_slot, bool is_trx_transient ) {
   const auto& state = _db.get<resource_limits_state_object>();
   const auto& config = _db.get<resource_limits_config_object>();

   for( const auto& a : accounts ) {

      const auto& usage = _db.get<resource_usage_object,by_owner>( a );
      int64_t unused;
      int64_t net_weight;
      int64_t cpu_weight;
      get_account_limits( a, unused, net_weight, cpu_weight );

      _db.modify( usage, [&]( auto& bu ){
          bu.net_usage.add( net_usage, time_slot, config.account_net_usage_average_window );
          bu.cpu_usage.add( cpu_usage, time_slot, config.account_cpu_usage_average_window );

         if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
            dm_logger->on_update_account_usage(bu);
         }
      });

      if( cpu_weight >= 0 && state.total_cpu_weight > 0 ) {
         uint128_t window_size = config.account_cpu_usage_average_window;
         auto virtual_network_capacity_in_window = (uint128_t)state.virtual_cpu_limit * window_size;
         auto cpu_used_in_window                 = ((uint128_t)usage.cpu_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)cpu_weight;
         uint128_t all_user_weight = state.total_cpu_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;

         EOS_ASSERT( cpu_used_in_window <= max_user_use_in_window,
                     tx_cpu_usage_exceeded,
                     "authorizing account '${n}' has insufficient objective cpu resources for this transaction,"
                     " used in window ${cpu_used_in_window}us, allowed in window ${max_user_use_in_window}us",
                     ("n", a)
                     ("cpu_used_in_window",cpu_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );
      }

      if( net_weight >= 0 && state.total_net_weight > 0) {

         uint128_t window_size = config.account_net_usage_average_window;
         auto virtual_network_capacity_in_window = (uint128_t)state.virtual_net_limit * window_size;
         auto net_used_in_window                 = ((uint128_t)usage.net_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)net_weight;
         uint128_t all_user_weight = state.total_net_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;

         EOS_ASSERT( net_used_in_window <= max_user_use_in_window,
                     tx_net_usage_exceeded,
                     "authorizing account '${n}' has insufficient net resources for this transaction,"
                     " used in window ${net_used_in_window}, allowed in window ${max_user_use_in_window}",
                     ("n", a)
                     ("net_used_in_window",net_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );

      }
   }

   // account for this transaction in the block and do not exceed those limits either
   _db.modify(state, [&](resource_limits_state_object& rls){
      rls.pending_cpu_usage += cpu_usage;
      rls.pending_net_usage += net_usage;
   });

   EOS_ASSERT( state.pending_cpu_usage <= config.cpu_limit_parameters.max, block_resource_exhausted, "Block has insufficient cpu resources" );
   EOS_ASSERT( state.pending_net_usage <= config.net_limit_parameters.max, block_resource_exhausted, "Block has insufficient net resources" );
}

void resource_limits_manager::add_transaction_usage_and_fees(const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, int64_t cpu_fee, int64_t net_fee, uint32_t time_slot, bool is_trx_transient ) {
   const auto& state = _db.get<resource_limits_state_object>();
   const auto& config = _db.get<resource_limits_config_object>();
   for( const auto& a : accounts ) {
      auto& fee_limits = _db.get<fee_limits_object, by_owner>( a );
      const auto& usage = _db.get<resource_usage_object,by_owner>( a );
      int64_t unused;
      int64_t net_weight;
      int64_t cpu_weight;
      get_account_limits( a, unused, net_weight, cpu_weight );

      if ( net_fee ==  -1 || cpu_fee == -1 ) {
         _db.modify( usage, [&]( auto& bu ){
            if ( net_fee == -1 ) {
               bu.net_usage.add( net_usage, time_slot, config.account_net_usage_average_window );
            }

            if ( cpu_fee == -1 ) {
               bu.cpu_usage.add( cpu_usage, time_slot, config.account_cpu_usage_average_window );
            }

            if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
               dm_logger->on_update_account_usage(bu);
            }
         });
      }

      if( cpu_weight >= 0 && state.total_cpu_weight > 0 && cpu_fee == -1) {
         uint128_t window_size = config.account_cpu_usage_average_window;
         auto virtual_network_capacity_in_window = (uint128_t)state.virtual_cpu_limit * window_size;
         auto cpu_used_in_window                 = ((uint128_t)usage.cpu_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)cpu_weight;
         uint128_t all_user_weight = state.total_cpu_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;
         EOS_ASSERT( cpu_used_in_window <= max_user_use_in_window,
                     tx_cpu_usage_exceeded,
                     "authorizing account '${n}' has insufficient cpu resources for this transaction",
                     ("n", name(a))
                     ("cpu_used_in_window",cpu_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );

      } else if (cpu_fee >= 0){
         auto available_cpu_fee = fee_limits.cpu_weight_limit - fee_limits.cpu_weight_consumption;
         EOS_ASSERT( available_cpu_fee >= cpu_fee,
                     tx_cpu_fee_exceeded,
                     "authorizing account '${n}' has insufficient staked cpu fee for this transaction; needs ${needs} has ${available}",
                     ("n",name(a))
                     ("needs",cpu_fee)
                     ("available",available_cpu_fee) );
      }

      if( net_weight >= 0 && state.total_net_weight > 0 && net_fee == -1) {

         uint128_t window_size = config.account_net_usage_average_window;
         auto virtual_network_capacity_in_window = (uint128_t)state.virtual_net_limit * window_size;
         auto net_used_in_window                 = ((uint128_t)usage.net_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)net_weight;
         uint128_t all_user_weight = state.total_net_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;
         EOS_ASSERT( net_used_in_window <= max_user_use_in_window,
                     tx_net_usage_exceeded,
                     "authorizing account '${n}' has insufficient net resources for this transaction",
                     ("n", name(a))
                     ("net_used_in_window",net_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );

      } else if (net_fee >= 0){
         auto available_net_fee = fee_limits.net_weight_limit - fee_limits.net_weight_consumption;
         EOS_ASSERT( available_net_fee >= net_fee,
                     tx_net_fee_exceeded,
                     "authorizing account '${n}' has insufficient staked net fee for this transaction; needs ${needs} has ${available}",
                     ("n", name(a))
                     ("needs",net_fee)
                     ("available",available_net_fee) );
      }

      if ( net_fee >= 0 || cpu_fee >= 0 ) {
         auto tx_fee = std::max(net_fee, static_cast<int64_t>(0)) + std::max(cpu_fee, static_cast<int64_t>(0));
         if(fee_limits.tx_fee_limit > 0){
            EOS_ASSERT( tx_fee <= fee_limits.tx_fee_limit, max_tx_fee_exceeded, "the transaction has consumed fee exceeded the maximum limit fee per transaction; consumed: ${consumed}, limit: ${limit}",
               ("consumed",tx_fee)
               ("limit",fee_limits.tx_fee_limit) );
         }

         if(fee_limits.account_fee_limit > 0){
            auto total_fee_consumed = tx_fee + fee_limits.net_weight_consumption + fee_limits.cpu_weight_consumption;
            EOS_ASSERT( total_fee_consumed <= fee_limits.account_fee_limit, max_account_fee_exceeded, "the account has consumed fee exceeded the maximum configured fee; consumed: ${consumed}, limit: ${limit}",
               ("consumed",total_fee_consumed)
               ("limit",fee_limits.account_fee_limit) );
         }

         _db.modify(fee_limits, [&](fee_limits_object& fl){
            if (cpu_fee >= 0) {
               fl.cpu_weight_consumption += cpu_fee;
            }
            if (net_fee >= 0) {
               fl.net_weight_consumption += net_fee;
            }
            if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
               dm_logger->on_update_account_fee_limits(fl);
            }
         });
      }
   }

   // account for this transaction in the block and do not exceed those limits either
   _db.modify(state, [&](resource_limits_state_object& rls){
      rls.pending_cpu_usage += cpu_usage;
      rls.pending_net_usage += net_usage;
   });

   EOS_ASSERT( state.pending_cpu_usage <= config.cpu_limit_parameters.max, block_resource_exhausted, "Block has insufficient cpu resources" );
   EOS_ASSERT( state.pending_net_usage <= config.net_limit_parameters.max, block_resource_exhausted, "Block has insufficient net resources" );
}

void resource_limits_manager::add_pending_ram_usage( const account_name account, int64_t ram_delta, bool is_trx_transient ) {
   if (ram_delta == 0) {
      return;
   }

   const auto& usage  = _db.get<resource_usage_object,by_owner>( account );

   EOS_ASSERT( ram_delta <= 0 || UINT64_MAX - usage.ram_usage >= (uint64_t)ram_delta, transaction_exception,
              "Ram usage delta would overflow UINT64_MAX");
   EOS_ASSERT(ram_delta >= 0 || usage.ram_usage >= (uint64_t)(-ram_delta), transaction_exception,
              "Ram usage delta would underflow UINT64_MAX");

   _db.modify( usage, [&]( auto& u ) {
      u.ram_usage += ram_delta;

      if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
         dm_logger->on_ram_event(account, u.ram_usage, ram_delta);
      }
   });
}

void resource_limits_manager::verify_account_ram_usage( const account_name account )const {
   int64_t ram_bytes; int64_t net_weight; int64_t cpu_weight;
   get_account_limits( account, ram_bytes, net_weight, cpu_weight );
   const auto& usage  = _db.get<resource_usage_object,by_owner>( account );

   if( ram_bytes >= 0 ) {
      EOS_ASSERT( usage.ram_usage <= static_cast<uint64_t>(ram_bytes), ram_usage_exceeded,
                  "account ${account} has insufficient ram; needs ${needs} bytes has ${available} bytes",
                  ("account", account)("needs",usage.ram_usage)("available",ram_bytes)              );
   }
}

int64_t resource_limits_manager::get_account_ram_usage( const account_name& name )const {
   return _db.get<resource_usage_object,by_owner>( name ).ram_usage;
}


bool resource_limits_manager::set_account_limits( const account_name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight, bool is_trx_transient) {
   //const auto& usage = _db.get<resource_usage_object,by_owner>( account );
   /*
    * Since we need to delay these until the next resource limiting boundary, these are created in a "pending"
    * state or adjusted in an existing "pending" state.  The chain controller will collapse "pending" state into
    * the actual state at the next appropriate boundary.
    */
   auto find_or_create_pending_limits = [&]() -> const resource_limits_object& {
      const auto* pending_limits = _db.find<resource_limits_object, by_owner>( boost::make_tuple(true, account) );
      if (pending_limits == nullptr) {
         const auto& limits = _db.get<resource_limits_object, by_owner>( boost::make_tuple(false, account));
         return _db.create<resource_limits_object>([&](resource_limits_object& pending_limits){
            pending_limits.owner = limits.owner;
            pending_limits.ram_bytes = limits.ram_bytes;
            pending_limits.net_weight = limits.net_weight;
            pending_limits.cpu_weight = limits.cpu_weight;
            pending_limits.pending = true;
         });
      } else {
         return *pending_limits;
      }
   };

   // update the users weights directly
   auto& limits = find_or_create_pending_limits();

   bool decreased_limit = false;

   if( ram_bytes >= 0 ) {

      decreased_limit = ( (limits.ram_bytes < 0) || (ram_bytes < limits.ram_bytes) );

      /*
      if( limits.ram_bytes < 0 ) {
         EOS_ASSERT(ram_bytes >= usage.ram_usage, wasm_execution_error, "converting unlimited account would result in overcommitment [commit=${c}, desired limit=${l}]", ("c", usage.ram_usage)("l", ram_bytes));
      } else {
         EOS_ASSERT(ram_bytes >= usage.ram_usage, wasm_execution_error, "attempting to release committed ram resources [commit=${c}, desired limit=${l}]", ("c", usage.ram_usage)("l", ram_bytes));
      }
      */
   }

   _db.modify( limits, [&]( resource_limits_object& pending_limits ){
      pending_limits.ram_bytes = ram_bytes;
      pending_limits.net_weight = net_weight;
      pending_limits.cpu_weight = cpu_weight;

      if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
         dm_logger->on_set_account_limits(pending_limits);
      }
   });

   return decreased_limit;
}

void resource_limits_manager::get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight ) const {
   const auto* pending_buo = _db.find<resource_limits_object,by_owner>( boost::make_tuple(true, account) );
   if (pending_buo) {
      ram_bytes  = pending_buo->ram_bytes;
      net_weight = pending_buo->net_weight;
      cpu_weight = pending_buo->cpu_weight;
   } else {
      const auto& buo = _db.get<resource_limits_object,by_owner>( boost::make_tuple( false, account ) );
      ram_bytes  = buo.ram_bytes;
      net_weight = buo.net_weight;
      cpu_weight = buo.cpu_weight;
   }
}

void resource_limits_manager::config_account_fee_limits(const account_name& account, int64_t tx_fee_limit, int64_t account_fee_limit, bool is_trx_transient) {
   EOS_ASSERT( tx_fee_limit >= -1, resource_limit_exception,
              "max consumed fee must be positive or -1 (no limit)");
   EOS_ASSERT( account_fee_limit >= -1, resource_limit_exception,
              "max consumed fee per transaction must be positive or -1 (no limmit)");
   const auto& fee_limits = _db.find<fee_limits_object,by_owner>( account );
   if (fee_limits == nullptr) {
      _db.create<fee_limits_object>([&]( fee_limits_object& fl ) {
         fl.owner = account;
         fl.tx_fee_limit = tx_fee_limit;
         fl.account_fee_limit = account_fee_limit;
         if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
            dm_logger->on_update_account_fee_limits(fl);
         }
      });
   }else{
      _db.modify(_db.get<fee_limits_object,by_owner>( account ), [&](auto& fl){
         fl.tx_fee_limit = tx_fee_limit;
         fl.account_fee_limit = account_fee_limit;
         if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
            dm_logger->on_update_account_fee_limits(fl);
         }
      });
   }
}

void resource_limits_manager::set_account_fee_limits( const account_name& account,int64_t net_weight_limit, int64_t cpu_weight_limit, bool is_trx_transient) {
   const auto& fee_limits = _db.find<fee_limits_object,by_owner>( account );
   if (fee_limits == nullptr) {
      _db.create<fee_limits_object>([&]( fee_limits_object& fl ) {
         fl.owner = account;
         fl.net_weight_limit = net_weight_limit;
         fl.cpu_weight_limit = cpu_weight_limit;
         fl.net_weight_consumption = 0;
         fl.cpu_weight_consumption = 0;
         // other values see default settings in the declaration
         if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
            dm_logger->on_update_account_fee_limits(fl);
         }
      });
   }else{
      _db.modify(_db.get<fee_limits_object,by_owner>( account ), [&](auto& fl){
         fl.net_weight_limit = net_weight_limit;
         fl.cpu_weight_limit = cpu_weight_limit;
         fl.net_weight_consumption = 0;
         fl.cpu_weight_consumption = 0;
         if (auto dm_logger = _control.get_deep_mind_logger(is_trx_transient)) {
               dm_logger->on_update_account_fee_limits(fl);
         }
      });
   }
}

bool resource_limits_manager::is_unlimited_cpu( const account_name& account ) const {
   const auto* buo = _db.find<resource_limits_object,by_owner>( boost::make_tuple(false, account) );
   if (buo) {
      return buo->cpu_weight == -1;
   }
   return false;
}

bool resource_limits_manager::is_account_enable_charging_fee(const flat_set<account_name>& accounts) const{
   auto is_enable_charging_fee = false;
   for( const auto& account : accounts ) {
      const auto& fee_limits = _db.find<fee_limits_object,by_owner>( account );
      if (fee_limits == nullptr) {
         return false;
      }else if(fee_limits->account_fee_limit != 0 && fee_limits->tx_fee_limit != 0){
         is_enable_charging_fee = true;
      }else{
         return false;
      }
   }
   return is_enable_charging_fee;
}

void resource_limits_manager::process_account_limit_updates() {
   auto& multi_index = _db.get_mutable_index<resource_limits_index>();
   auto& by_owner_index = multi_index.indices().get<by_owner>();

   // convenience local lambda to reduce clutter
   auto update_state_and_value = [](uint64_t &total, int64_t &value, int64_t pending_value, const char* debug_which) -> void {
      if (value > 0) {
         EOS_ASSERT(total >= static_cast<uint64_t>(value), rate_limiting_state_inconsistent, "underflow when reverting old value to ${which}", ("which", debug_which));
         total -= value;
      }

      if (pending_value > 0) {
         EOS_ASSERT(UINT64_MAX - total >= static_cast<uint64_t>(pending_value), rate_limiting_state_inconsistent, "overflow when applying new value to ${which}", ("which", debug_which));
         total += pending_value;
      }

      value = pending_value;
   };

   const auto& state = _db.get<resource_limits_state_object>();
   _db.modify(state, [&](resource_limits_state_object& rso){
      while(!by_owner_index.empty()) {
         const auto& itr = by_owner_index.lower_bound(boost::make_tuple(true));
         if (itr == by_owner_index.end() || itr->pending!= true) {
            break;
         }

         const auto& actual_entry = _db.get<resource_limits_object, by_owner>(boost::make_tuple(false, itr->owner));
         _db.modify(actual_entry, [&](resource_limits_object& rlo){
            update_state_and_value(rso.total_ram_bytes,  rlo.ram_bytes,  itr->ram_bytes, "ram_bytes");
            update_state_and_value(rso.total_cpu_weight, rlo.cpu_weight, itr->cpu_weight, "cpu_weight");
            update_state_and_value(rso.total_net_weight, rlo.net_weight, itr->net_weight, "net_weight");
         });

         multi_index.remove(*itr);
      }

      // process_account_limit_updates is called by controller::finalize_block,
      // where transaction specific logging is not possible
      if (auto dm_logger = _control.get_deep_mind_logger(false)) {
         dm_logger->on_update_resource_limits_state(state);
      }
   });
}

void resource_limits_manager::process_block_usage(uint32_t block_num) {
   const auto& s = _db.get<resource_limits_state_object>();
   const auto& config = _db.get<resource_limits_config_object>();
   _db.modify(s, [&](resource_limits_state_object& state){
      // apply pending usage, update virtual limits and reset the pending

      state.average_block_cpu_usage.add(state.pending_cpu_usage, block_num, config.cpu_limit_parameters.periods);
      state.update_virtual_cpu_limit(config);
      state.pending_cpu_usage = 0;

      state.average_block_net_usage.add(state.pending_net_usage, block_num, config.net_limit_parameters.periods);
      state.update_virtual_net_limit(config);
      state.pending_net_usage = 0;

      // process_block_usage is called by controller::finalize_block,
      // where transaction specific logging is not possible
      if (auto dm_logger = _control.get_deep_mind_logger(false)) {
         dm_logger->on_update_resource_limits_state(state);
      }
   });

}

uint64_t resource_limits_manager::get_total_cpu_weight() const {
   const auto& state = _db.get<resource_limits_state_object>();
   return state.total_cpu_weight;
}

uint64_t resource_limits_manager::get_total_net_weight() const {
   const auto& state = _db.get<resource_limits_state_object>();
   return state.total_net_weight;
}

uint64_t resource_limits_manager::get_virtual_block_cpu_limit() const {
   const auto& state = _db.get<resource_limits_state_object>();
   return state.virtual_cpu_limit;
}

uint64_t resource_limits_manager::get_virtual_block_net_limit() const {
   const auto& state = _db.get<resource_limits_state_object>();
   return state.virtual_net_limit;
}

uint64_t resource_limits_manager::get_block_cpu_limit() const {
   const auto& state = _db.get<resource_limits_state_object>();
   const auto& config = _db.get<resource_limits_config_object>();
   return config.cpu_limit_parameters.max - state.pending_cpu_usage;
}

uint64_t resource_limits_manager::get_block_net_limit() const {
   const auto& state = _db.get<resource_limits_state_object>();
   const auto& config = _db.get<resource_limits_config_object>();
   return config.net_limit_parameters.max - state.pending_net_usage;
}

std::pair<int64_t, bool> resource_limits_manager::get_account_cpu_limit( const account_name& name, uint32_t greylist_limit ) const {
   auto [arl, greylisted] = get_account_cpu_limit_ex(name, greylist_limit);
   return {arl.available, greylisted};
}

std::pair<account_resource_limit, bool>
resource_limits_manager::get_account_cpu_limit_ex( const account_name& name, uint32_t greylist_limit, const std::optional<block_timestamp_type>& current_time) const {

   const auto& state = _db.get<resource_limits_state_object>();
   const auto& usage = _db.get<resource_usage_object, by_owner>(name);
   const auto& config = _db.get<resource_limits_config_object>();

   int64_t cpu_weight, x, y;
   get_account_limits( name, x, y, cpu_weight );

   if( cpu_weight < 0 || state.total_cpu_weight == 0 ) {
      return {{ -1, -1, -1, block_timestamp_type(usage.cpu_usage.last_ordinal), -1 }, false};
   }

   account_resource_limit arl;

   uint128_t window_size = config.account_cpu_usage_average_window;

   bool greylisted = false;
   uint128_t virtual_cpu_capacity_in_window = window_size;
   if( greylist_limit < config::maximum_elastic_resource_multiplier ) {
      uint64_t greylisted_virtual_cpu_limit = config.cpu_limit_parameters.max * greylist_limit;
      if( greylisted_virtual_cpu_limit < state.virtual_cpu_limit ) {
         virtual_cpu_capacity_in_window *= greylisted_virtual_cpu_limit;
         greylisted = true;
      } else {
         virtual_cpu_capacity_in_window *= state.virtual_cpu_limit;
      }
   } else {
      virtual_cpu_capacity_in_window *= state.virtual_cpu_limit;
   }

   uint128_t user_weight     = (uint128_t)cpu_weight;
   uint128_t all_user_weight = (uint128_t)state.total_cpu_weight;

   auto max_user_use_in_window = (virtual_cpu_capacity_in_window * user_weight) / all_user_weight;
   auto cpu_used_in_window  = impl::integer_divide_ceil((uint128_t)usage.cpu_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision);

   if( max_user_use_in_window <= cpu_used_in_window )
      arl.available = 0;
   else
      arl.available = impl::downgrade_cast<int64_t>(max_user_use_in_window - cpu_used_in_window);

   arl.used = impl::downgrade_cast<int64_t>(cpu_used_in_window);
   arl.max = impl::downgrade_cast<int64_t>(max_user_use_in_window);
   arl.last_usage_update_time = block_timestamp_type(usage.cpu_usage.last_ordinal);
   arl.current_used = arl.used;
   if ( current_time ) {
      if (current_time->slot > usage.cpu_usage.last_ordinal) {
         auto history_usage = usage.cpu_usage;
         history_usage.add(0, current_time->slot, window_size);
         arl.current_used = impl::downgrade_cast<int64_t>(impl::integer_divide_ceil((uint128_t)history_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision));
      }
   }
   return {arl, greylisted};
}

std::pair<int64_t, bool> resource_limits_manager::get_account_net_limit( const account_name& name, uint32_t greylist_limit ) const {
   auto [arl, greylisted] = get_account_net_limit_ex(name, greylist_limit);
   return {arl.available, greylisted};
}

std::pair<account_resource_limit, bool>
resource_limits_manager::get_account_net_limit_ex( const account_name& name, uint32_t greylist_limit, const std::optional<block_timestamp_type>& current_time) const {
   const auto& config = _db.get<resource_limits_config_object>();
   const auto& state  = _db.get<resource_limits_state_object>();
   const auto& usage  = _db.get<resource_usage_object, by_owner>(name);

   int64_t net_weight, x, y;
   get_account_limits( name, x, net_weight, y );

   if( net_weight < 0 || state.total_net_weight == 0) {
      return {{ -1, -1, -1, block_timestamp_type(usage.net_usage.last_ordinal), -1 }, false};
   }

   account_resource_limit arl;

   uint128_t window_size = config.account_net_usage_average_window;

   bool greylisted = false;
   uint128_t virtual_network_capacity_in_window = window_size;
   if( greylist_limit < config::maximum_elastic_resource_multiplier ) {
      uint64_t greylisted_virtual_net_limit = config.net_limit_parameters.max * greylist_limit;
      if( greylisted_virtual_net_limit < state.virtual_net_limit ) {
         virtual_network_capacity_in_window *= greylisted_virtual_net_limit;
         greylisted = true;
      } else {
         virtual_network_capacity_in_window *= state.virtual_net_limit;
      }
   } else {
      virtual_network_capacity_in_window *= state.virtual_net_limit;
   }

   uint128_t user_weight     = (uint128_t)net_weight;
   uint128_t all_user_weight = (uint128_t)state.total_net_weight;

   auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;
   auto net_used_in_window  = impl::integer_divide_ceil((uint128_t)usage.net_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision);

   if( max_user_use_in_window <= net_used_in_window )
      arl.available = 0;
   else
      arl.available = impl::downgrade_cast<int64_t>(max_user_use_in_window - net_used_in_window);

   arl.used = impl::downgrade_cast<int64_t>(net_used_in_window);
   arl.max = impl::downgrade_cast<int64_t>(max_user_use_in_window);
   arl.last_usage_update_time = block_timestamp_type(usage.net_usage.last_ordinal);
   arl.current_used = arl.used;
   if ( current_time ) {
      if (current_time->slot > usage.net_usage.last_ordinal) {
         auto history_usage = usage.net_usage;
         history_usage.add(0, current_time->slot, window_size);
         arl.current_used = impl::downgrade_cast<int64_t>(impl::integer_divide_ceil((uint128_t)history_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision));
      }
   }
   return {arl, greylisted};
}

int64_t resource_limits_manager::calculate_resource_fee(uint64_t resource_usage, uint64_t ema_block_resource, uint64_t free_block_resource_threshold, uint64_t max_block_resource, uint64_t resource_fee_scaler) const {
   EOS_ASSERT( ema_block_resource < max_block_resource, resource_limit_exception, "elastic moving average resource parameter must be smaller than max block resource parameter" );

   // Formula: resource_fee = resource_fee_scaler * (1 / (max_block_resource - ema_block_resource) - 1 / (max_block_resource - free_block_resource_threshold)) * resource_usage

   if (free_block_resource_threshold >= ema_block_resource ) {
      return 0;
   }
   auto num = (int128_t)resource_usage * resource_fee_scaler * (ema_block_resource - free_block_resource_threshold);
   auto den = (int128_t)(max_block_resource - free_block_resource_threshold) * (max_block_resource - ema_block_resource);
   return impl::downgrade_cast<int64_t>(num / den);
}

void resource_limits_manager::get_account_fee_consumption( const account_name& account, int64_t& net_weight_consumption, int64_t& cpu_weight_consumption) const {
   const auto& fee_limits = _db.find<fee_limits_object,by_owner>( account );
   if (fee_limits == nullptr) {
      net_weight_consumption = 0;
      cpu_weight_consumption = 0;
   }else{
      net_weight_consumption = fee_limits->net_weight_consumption;
      cpu_weight_consumption = fee_limits->cpu_weight_consumption;
   }
}

std::pair<int64_t, int64_t> resource_limits_manager::get_account_available_fees( const account_name& account) const {
   const auto& fee_limits = _db.find<fee_limits_object,by_owner>( account );
   if (fee_limits == nullptr) {
      return{0, 0};
   }else{
      int64_t available_net_fee = fee_limits->net_weight_limit - fee_limits->net_weight_consumption;
      int64_t available_cpu_fee = fee_limits->cpu_weight_limit - fee_limits->cpu_weight_consumption;
      return{available_net_fee, available_cpu_fee};
   }
}

std::pair<int64_t, int64_t> resource_limits_manager::get_config_fee_limits( const account_name& account) const {
   const auto& fee_limits = _db.find<fee_limits_object,by_owner>( account );
   if (fee_limits == nullptr) {
      return{0, 0};
   }else{
      return{fee_limits->tx_fee_limit, fee_limits->account_fee_limit};
   }
}

int64_t resource_limits_manager::get_cpu_usage_fee_to_bill( int64_t cpu_usage ) const {
   const auto& state = _db.get<resource_limits_state_object>();
   const auto& config = _db.get<resource_limits_config_object>();
   const auto& fee_params = _db.get<fee_params_object>();
   return calculate_resource_fee(
      cpu_usage, 
      state.average_block_cpu_usage.average(),
      fee_params.free_block_cpu_threshold,
      config.cpu_limit_parameters.max,
      fee_params.cpu_fee_scaler
   );
}

int64_t resource_limits_manager::get_net_usage_fee_to_bill( int64_t net_usage ) const {
   const auto& state = _db.get<resource_limits_state_object>();
   const auto& config = _db.get<resource_limits_config_object>();
   const auto& fee_params = _db.get<fee_params_object>();
   return calculate_resource_fee(
      net_usage, 
      state.average_block_net_usage.average(),
      fee_params.free_block_net_threshold,
      config.net_limit_parameters.max,
      fee_params.net_fee_scaler
   );
}

} } } /// eosio::chain::resource_limits
