#include "txfee_api_test.hpp"

[[eosio::action]] void txfee_api_test::setparams(uint64_t cpu_fee_scaler, uint64_t free_block_cpu_threshold, uint64_t net_fee_scaler, uint64_t free_block_net_threshold) {
   eosio::internal_use_do_not_use::set_fee_parameters(cpu_fee_scaler, free_block_cpu_threshold, net_fee_scaler, free_block_net_threshold);
}

[[eosio::action]] void txfee_api_test::configfees(eosio::name account, int64_t tx_fee_limit, int64_t account_fee_limit) {
   eosio::internal_use_do_not_use::config_fee_limits(account.value, tx_fee_limit, account_fee_limit);
}

[[eosio::action]] void txfee_api_test::setfees(eosio::name account, int64_t net_weight_limit, int64_t cpu_weight_limit) {
   eosio::internal_use_do_not_use::set_fee_limits(account.value, net_weight_limit, cpu_weight_limit);
}

[[eosio::action]] void txfee_api_test::getfees(eosio::name account, int64_t expected_net_pending_weight, int64_t expected_cpu_consumed_weight) {
   int64_t net_weight_consumption, cpu_weight_consumption;
   eosio::internal_use_do_not_use::get_fee_consumption(account.value, &net_weight_consumption, &cpu_weight_consumption);
   eosio::check( net_weight_consumption == expected_net_pending_weight, "Error does not match");
   eosio::check( cpu_weight_consumption == expected_cpu_consumed_weight, "Error does not match");
}
