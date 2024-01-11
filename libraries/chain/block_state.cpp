#include <eosio/chain/block_state.hpp>
#include <eosio/chain/exceptions.hpp>

namespace eosio::chain {

   namespace {
      constexpr auto additional_sigs_eid = additional_block_signatures_extension::extension_id();

      /**
       * Given a complete signed block, extract the validated additional signatures if present;
       *
       * @param b complete signed block
       * @param pfs protocol feature set for digest access
       * @param pfa activated protocol feature set to determine if extensions are allowed
       * @return the list of additional signatures
       * @throws if additional signatures are present before being supported by protocol feature activations
       */
      vector<signature_type> extract_additional_signatures( const signed_block_ptr& b,
                                                            const protocol_feature_set& pfs,
                                                            const protocol_feature_activation_set_ptr& pfa )
      {
         auto exts = b->validate_and_extract_extensions();

         if ( exts.count(additional_sigs_eid) > 0 ) {
            auto& additional_sigs = std::get<additional_block_signatures_extension>(exts.lower_bound(additional_sigs_eid)->second);

            return std::move(additional_sigs.signatures);
         }

         return {};
      }

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
      block_header_state inject_additional_signatures(block_header_state&& cur,
                                                      signed_block& b,
                                                      const protocol_feature_set& pfs,
                                                      Extras&& ... extras)
      {
         
         block_header_state result;
#if 0
         result = std::move(cur).finish_next(b, pfs, std::forward<Extras>(extras)...);
         auto pfa = cur.prev_activated_protocol_features;

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
#endif
         return result;
      }

   }
#if 0

   block_state::block_state(const block_header_state& prev,
                                  signed_block_ptr b,
                                  const protocol_feature_set& pfs,
                                  bool hotstuff_activated,
                                  const validator_t& validator,
                                  bool skip_validate_signee
                           )
   :block_header_state( prev.next( *b, extract_additional_signatures(b, pfs, prev.activated_protocol_features), pfs, hotstuff_activated, validator, skip_validate_signee ) )
   ,block( std::move(b) )
   {}

   block_state::block_state(pending_block_header_state&& cur,
                            signed_block_ptr&& b,
                            deque<transaction_metadata_ptr>&& trx_metas,
                            const protocol_feature_set& pfs,
                            const validator_t& validator,
                            const signer_callback_type& signer
      )
   :block_header_state( inject_additional_signatures( std::move(cur), *b, pfs, validator, signer ) )
   ,block( std::move(b) )
   ,_pub_keys_recovered( true ) // called by produce_block so signature recovery of trxs must have been done
   ,_cached_trxs( std::move(trx_metas) )
   {}
#endif

   std::optional<qc_data_t> block_state::get_best_qc() const {
      auto block_number = block_num();

      // if pending_qc does not have a valid QC, consider valid_qc only
      if( !pending_qc.is_valid() ) {
         if( valid_qc ) {
            return qc_data_t{ quorum_certificate{ block_number, valid_qc.value() },
                              qc_info_t{ block_number, valid_qc.value().is_strong() }};
         } else {
            return std::nullopt;;
         }
      }

      // extract valid QC from pending_qc
      valid_quorum_certificate valid_qc_from_pending(pending_qc);

      // if valid_qc does not have value, consider valid_qc_from_pending only
      if( !valid_qc ) {
         return qc_data_t{ quorum_certificate{ block_number, valid_qc_from_pending },
                           qc_info_t{ block_number, valid_qc_from_pending.is_strong() }};
      }

      // Both valid_qc and valid_qc_from_pending have value. Compare them and select a better one.
      // Strong beats weak. Break tie with highest accumulated weight.
      auto valid_qc_is_better = false;
      if( valid_qc.value().is_strong() && !valid_qc_from_pending.is_strong() ) {
         valid_qc_is_better = true;
      } else if( !valid_qc.value().is_strong() && valid_qc_from_pending.is_strong() ) {
         valid_qc_is_better = false;
      } else if( valid_qc.value().accumulated_weight() >= valid_qc_from_pending.accumulated_weight() ) {
         valid_qc_is_better = true;
      } else {
         valid_qc_is_better = false;
      }

      const auto& qc = valid_qc_is_better ? valid_qc.value() : valid_qc_from_pending;
      return qc_data_t{ quorum_certificate{ block_number, qc },
                        qc_info_t{ block_number, qc.is_strong() }};
   }
   
} /// eosio::chain
