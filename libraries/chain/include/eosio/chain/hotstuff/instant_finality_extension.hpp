#pragma   once

#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/hotstuff/proposer_policy.hpp>

namespace eosio::chain {

struct qc_claim_t {
   uint32_t last_qc_block_num; // The block height of the most recent ancestor block that has a QC justification
   bool     is_last_qc_strong; // Whether the QC for the block referenced by last_qc_block_height is strong or weak.
};

struct instant_finality_extension : fc::reflect_init {
   static constexpr uint16_t extension_id()   { return 2; }
   static constexpr bool     enforce_unique() { return true; }

   instant_finality_extension() = default;
   instant_finality_extension(qc_claim_t qc_claim,
                              std::optional<finalizer_policy> new_finalizer_policy,
                              std::shared_ptr<proposer_policy> new_proposer_policy) :
      qc_claim(qc_claim),
      new_finalizer_policy(std::move(new_finalizer_policy)),
      new_proposer_policy(std::move(new_proposer_policy))
   {}

   void reflector_init();

   qc_claim_t                         qc_claim;
   std::optional<finalizer_policy>    new_finalizer_policy;
   std::shared_ptr<proposer_policy>   new_proposer_policy;
};

} /// eosio::chain

FC_REFLECT( eosio::chain::qc_claim_t, (last_qc_block_num)(is_last_qc_strong) )
FC_REFLECT( eosio::chain::instant_finality_extension, (qc_claim)(new_finalizer_policy)(new_proposer_policy) )
