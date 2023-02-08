#pragma once

#include <eosio/state_history/types.hpp>

namespace eosio {
namespace state_history {

std::vector<table_delta> create_deltas(const chainbase::database& db, bool full_snapshot);

std::vector<table_delta_with_context> create_contract_row_deltas_with_context(const chainbase::database& db,
                                                                              bool                       full_snapshot);

} // namespace state_history
} // namespace eosio
