#pragma once

#include <eosio/chain/types.hpp>

namespace eosio {
namespace state_history {

using chain::bytes;

bytes zlib_compress_bytes(const bytes& in);
bytes zlib_decompress(const char* data, uint64_t data_size);

} // namespace state_history
} // namespace eosio
