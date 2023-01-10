#pragma once

#include <eosio/state_history/types.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>

namespace eosio {
namespace state_history {

void pack_deltas(boost::iostreams::filtering_ostreambuf& ds, const chainbase::database& db, bool full_snapshot);


} // namespace state_history
} // namespace eosio
