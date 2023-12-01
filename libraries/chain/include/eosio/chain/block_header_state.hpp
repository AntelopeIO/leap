#pragma once

#include <cstdint>
#include <optional>

namespace eosio { namespace chain {

/**
 *  @struct block_header_state_core
 *
 *  A data structure holding hotstuff core information
 */
struct block_header_state_core {
   // the block height of the last irreversible (final) block.
   uint32_t last_final_block_height = 0;

   // the block height of the block that would become irreversible (final) if the
   // associated block header was to achieve a strong QC.
   std::optional<uint32_t> final_on_strong_qc_block_height;

   // the block height of the block that is referenced as the last QC block
   std::optional<uint32_t> last_qc_block_height;

   block_header_state_core() = default;

   explicit block_header_state_core( uint32_t last_final_block_height,
                                     std::optional<uint32_t> final_on_strong_qc_block_height,
                                     std::optional<uint32_t> last_qc_block_height );

   block_header_state_core next( uint32_t last_qc_block_height,
                                 bool is_last_qc_strong);
};
} } /// namespace eosio::chain
