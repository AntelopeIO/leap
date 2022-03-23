#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/trace.hpp>
#include <memory>

namespace eosio::chain {

class trx_interface {
public:
   trx_interface() = default;

   /// connect to chain controller applied_transaction signal
   virtual void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::signed_transaction& strx ) = 0;
};

using trx_interface_ptr = std::shared_ptr<trx_interface>;

class no_op_processor : public trx_interface {
public:
   no_op_processor() = default;

   void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::signed_transaction& strx ) override {}
};
} // eosio::chain