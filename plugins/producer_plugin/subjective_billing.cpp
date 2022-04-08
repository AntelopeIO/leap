#include <eosio/producer_plugin/subjective_billing.hpp>

namespace eosio {

uint32_t subjective_billing::expired_accumulator_average_window = config::account_cpu_usage_average_window_ms / subjective_billing::subjective_time_interval_ms;

} //eosio
