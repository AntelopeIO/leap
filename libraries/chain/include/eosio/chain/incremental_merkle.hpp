#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/merkle.hpp>
#include <fc/io/raw.hpp>
#include <bit>

#include <eosio/chain/incremental_merkle_legacy.hpp>  // temporary - remove when incremental_merkle implemented here


namespace eosio::chain {

namespace detail {

} /// detail

typedef incremental_merkle_impl<digest_type> incremental_merkle_tree;

} /// eosio::chain

FC_REFLECT( eosio::chain::incremental_merkle_tree, (_active_nodes)(_node_count) );
