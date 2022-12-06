#pragma once
#include <eosio/chain/abi_def.hpp>
#include <eosio/chain/protocol_feature_activation.hpp>
#include <eosio/trace_api/trace.hpp>
#include <fc/variant.hpp>

namespace eosio {
namespace trace_api {

using data_log_entry = std::variant<block_trace_v0, block_trace_v1, block_trace_v2>;

}
} // namespace eosio
