#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/trace.hpp>
#include <optional>

namespace eosio::chain {

class block_interface {
public:
   block_interface() = default;

   /// connect to chain controller irreversible_block signal
   virtual void signal_irreversible_block( const chain::block_state_ptr& bsp ) = 0;

   /// connect to chain controller block_start signal
   virtual void signal_block_start( uint32_t block_num ) {
      _block_num = block_num;
   }

   /// connect to chain controller accepted_block signal
   virtual void signal_accepted_block( const chain::block_state_ptr& ) {
      _block_num.reset();
   }

   std::optional<uint32_t> block_num() const {
      return _block_num;
   }
private:
   std::optional<uint32_t> _block_num;
};

using block_interface_ptr = std::shared_ptr<block_interface>;

} // eosio::chain