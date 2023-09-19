#pragma once

#include <eosio/eosio.hpp>
#include <tuple>

namespace eosio {
   namespace internal_use_do_not_use {
      extern "C" {
         __attribute__((eosio_wasm_import))
         void set_fee_parameters(uint64_t cpu_fee_scaler, uint64_t free_block_cpu_threshold, uint64_t net_fee_scaler, uint64_t free_block_net_threshold);

         __attribute__((eosio_wasm_import))
         void config_fee_limits(uint64_t account, int64_t tx_fee_limit, int64_t account_fee_limit);

         __attribute__((eosio_wasm_import))
         void set_fee_limits( uint64_t account, int64_t net_weight_limit, int64_t cpu_weight_limit);

         __attribute__((eosio_wasm_import))
         void get_fee_consumption( uint64_t account, int64_t* net_weight_consumption, int64_t* cpu_consumed_weight);
      }
   }
}

class [[eosio::contract]] txfee_api_test : public eosio::contract {
public:
   using eosio::contract::contract;

   [[eosio::action]] void setparams(uint64_t cpu_fee_scaler, uint64_t free_block_cpu_threshold, uint64_t net_fee_scaler, uint64_t free_block_net_threshold);

   [[eosio::action]] void configfees(eosio::name account, int64_t tx_fee_limit, int64_t account_fee_limit);

   [[eosio::action]] void setfees(eosio::name account, int64_t net_weight_limit, int64_t cpu_weight_limit);

   [[eosio::action]]
   void getfees(eosio::name account, int64_t expected_net_pending_weight, int64_t expected_cpu_consumed_weight);
};
