#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/snapshot_detail.hpp>


namespace eosio::chain {

   namespace {
      constexpr auto additional_sigs_eid = additional_block_signatures_extension::extension_id();

      /**
       * Given a pending block header state, wrap the promotion to a block header state such that additional signatures
       * can be allowed based on activations *prior* to the promoted block and properly injected into the signed block
       * that is previously constructed and mutated by the promotion
       *
       * This cleans up lifetime issues involved with accessing activated protocol features and moving from the
       * pending block header state
       *
       * @param cur the pending block header state to promote
       * @param b the signed block that will receive signatures during this process
       * @param pfs protocol feature set for digest access
       * @param extras all the remaining parameters that pass through
       * @return the block header state
       * @throws if the block was signed with multiple signatures before the extension is allowed
       */

      template<typename ...Extras>
      block_header_state_legacy inject_additional_signatures( pending_block_header_state_legacy&& cur,
                                                              signed_block& b,
                                                              const protocol_feature_set& pfs,
                                                              Extras&& ... extras )
      {
         auto pfa = cur.prev_activated_protocol_features;
         block_header_state_legacy result = std::move(cur).finish_next(b, pfs, std::forward<Extras>(extras)...);

         if (!result.additional_signatures.empty()) {
            bool wtmsig_enabled = detail::is_builtin_activated(pfa, pfs, builtin_protocol_feature_t::wtmsig_block_signatures);

            EOS_ASSERT(wtmsig_enabled, block_validate_exception,
                       "Block has multiple signatures before activation of WTMsig Block Signatures");

            // as an optimization we don't copy this out into the legitimate extension structure as it serializes
            // the same way as the vector of signatures
            static_assert(fc::reflector<additional_block_signatures_extension>::total_member_count == 1);
            static_assert(std::is_same_v<decltype(additional_block_signatures_extension::signatures), std::vector<signature_type>>);

            emplace_extension(b.block_extensions, additional_sigs_eid, fc::raw::pack( result.additional_signatures ));
         }

         return result;
      }

   }

   block_state_legacy::block_state_legacy( const block_header_state_legacy& prev,
                                           signed_block_ptr b,
                                           const protocol_feature_set& pfs,
                                           const validator_t& validator,
                                           bool skip_validate_signee
                           )
      :block_header_state_legacy( prev.next( *b, detail::extract_additional_signatures(b), pfs, validator, skip_validate_signee ) )
      ,block( std::move(b) )
   {}

   block_state_legacy::block_state_legacy( pending_block_header_state_legacy&& cur,
                                           signed_block_ptr&& b,
                                           deque<transaction_metadata_ptr>&& trx_metas,
                                           const std::optional<digests_t>& action_receipt_digests_savanna,
                                           const protocol_feature_set& pfs,
                                           const validator_t& validator,
                                           const signer_callback_type& signer
                           )
   :block_header_state_legacy( inject_additional_signatures( std::move(cur), *b, pfs, validator, signer ) )
   ,block( std::move(b) )
   ,_pub_keys_recovered( true ) // called by produce_block so signature recovery of trxs must have been done
   ,_cached_trxs( std::move(trx_metas) )
   ,action_mroot_savanna( action_receipt_digests_savanna ? std::optional<digest_type>(calculate_merkle(*action_receipt_digests_savanna)) : std::nullopt )
   {}

   block_state_legacy::block_state_legacy(snapshot_detail::snapshot_block_state_legacy_v7&& sbs)
      : block_header_state_legacy(std::move(static_cast<snapshot_detail::snapshot_block_header_state_legacy_v3&>(sbs)))
        // , valid(std::move(sbs.valid) // [snapshot todo]
   {
   }

} /// eosio::chain
