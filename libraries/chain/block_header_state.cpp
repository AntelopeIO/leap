#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/exceptions.hpp>

namespace eosio { namespace chain {
   block_header_state_core::block_header_state_core(
      uint32_t last_final_block_height,
      std::optional<uint32_t> final_on_strong_qc_block_height,
      std::optional<uint32_t> last_qc_block_height )
   :last_final_block_height(last_final_block_height)
   ,final_on_strong_qc_block_height(final_on_strong_qc_block_height)
   ,last_qc_block_height(last_qc_block_height)
   {}

   block_header_state_core block_header_state_core::next( uint32_t last_qc_block_height,
                                                          bool     is_last_qc_strong) {
      // no state change if last_qc_block_height is the same
      if( last_qc_block_height == this->last_qc_block_height ) {
         return {*this};
      }

      EOS_ASSERT( last_qc_block_height > this->last_qc_block_height, block_validate_exception, "new last_qc_block_height must be greater than old last_qc_block_height" );

      auto old_last_qc_block_height = this->last_qc_block_height;
      auto old_final_on_strong_qc_block_height = this->final_on_strong_qc_block_height;

      block_header_state_core result{*this};

      if( is_last_qc_strong ) {
         // last QC is strong. We can progress forward.

         // block with old final_on_strong_qc_block_height becomes irreversible
         if( old_final_on_strong_qc_block_height.has_value() ) {
            result.last_final_block_height = *old_final_on_strong_qc_block_height;
         }

         // next block which can become irreversible is the block with
         // old last_qc_block_height
         if( old_last_qc_block_height.has_value() ) {
            result.final_on_strong_qc_block_height = *old_last_qc_block_height;
         }
      } else {
         // new final_on_strong_qc_block_height should not be present
         result.final_on_strong_qc_block_height.reset();

         // new last_final_block_height should be the same as the old last_final_block_height
      }

      // new last_qc_block_height is always the input last_qc_block_height.
      result.last_qc_block_height = last_qc_block_height;

      return result;
   }
} } /// namespace eosio::chain
