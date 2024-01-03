#pragma   once

#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/hotstuff/proposer_policy.hpp>

namespace eosio::chain {

struct instant_finality_extension : fc::reflect_init {
   static constexpr uint16_t extension_id() { return 2; }
   static constexpr bool     enforce_unique() { return true; }

   instant_finality_extension() = default;
   instant_finality_extension( uint32_t last_qc_block_num, bool is_last_qc_strong, std::optional<finalizer_policy> new_finalizer_policy, std::optional<proposer_policy> new_proposer_policy)
   :last_qc_block_num( last_qc_block_num ), is_last_qc_strong( is_last_qc_strong ), new_finalizer_policy( std::move(new_finalizer_policy) ), new_proposer_policy( std::move(new_proposer_policy) )
   {}

   void reflector_init();

   uint32_t                         last_qc_block_num {0};  // The block height of the most recent ancestor block that has a QC justification
   bool                             is_last_qc_strong {false}; // Whether the QC for the block referenced by last_qc_block_height is strong or weak.
   std::optional<finalizer_policy>  new_finalizer_policy;
   std::optional<proposer_policy>   new_proposer_policy;
};

} /// eosio::chain

FC_REFLECT( eosio::chain::instant_finality_extension, (last_qc_block_num)(is_last_qc_strong)(new_finalizer_policy)(new_proposer_policy) )
