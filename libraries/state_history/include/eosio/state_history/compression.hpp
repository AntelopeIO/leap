#pragma once

#include <eosio/chain/types.hpp>

namespace eosio {
namespace state_history {

using chain::bytes;

bytes zlib_compress_bytes(const bytes& in);
bytes zlib_decompress(std::string_view);

} // namespace state_history
} // namespace eosio
