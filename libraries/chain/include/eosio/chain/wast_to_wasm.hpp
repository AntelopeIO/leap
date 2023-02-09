#pragma once
#include <vector>
#include <string>
#include <stdint.h>

namespace eosio { namespace chain {

std::vector<uint8_t> wast_to_wasm( const std::string& wast );

} } /// eosio::chain
