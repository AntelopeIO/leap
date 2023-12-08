#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/exceptions.hpp>
#include <limits>

namespace eosio::chain {

namespace detail {

uint32_t get_next_next_round_block_num(block_timestamp_type t, uint32_t block_num) {
   auto index = t.slot % config::producer_repetitions; // current index in current round
   //                 (increment to the end of this round  ) + next round
   return block_num + (config::producer_repetitions - index) + config::producer_repetitions;
}

} // namespace detail

block_header_state_core block_header_state_core::next(uint32_t last_qc_block_height, bool is_last_qc_strong) const {
   // no state change if last_qc_block_height is the same
   if (last_qc_block_height == this->last_qc_block_height) {
      return {*this};
   }

   EOS_ASSERT(last_qc_block_height > this->last_qc_block_height, block_validate_exception,
              "new last_qc_block_height must be greater than old last_qc_block_height");

   auto old_last_qc_block_height            = this->last_qc_block_height;
   auto old_final_on_strong_qc_block_height = this->final_on_strong_qc_block_height;

   block_header_state_core result{*this};

   if (is_last_qc_strong) {
      // last QC is strong. We can progress forward.

      // block with old final_on_strong_qc_block_height becomes irreversible
      if (old_final_on_strong_qc_block_height.has_value()) {
         result.last_final_block_height = *old_final_on_strong_qc_block_height;
      }

      // next block which can become irreversible is the block with
      // old last_qc_block_height
      if (old_last_qc_block_height.has_value()) {
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


block_header_state block_header_state::next(const assembled_block_input& data) const {
   block_header_state result;

#if 0
   if (when != block_timestamp_type()) {
      EOS_ASSERT(when > header.timestamp, block_validate_exception, "next block must be in the future");
   } else {
      (when = header.timestamp).slot++;
   }
   result.block_num                        = block_num + 1;
   result.previous                         = id;
   result.timestamp                        = when;
   result.active_schedule_version          = active_schedule.version;
   result.prev_activated_protocol_features = activated_protocol_features;

   auto proauth = get_scheduled_producer(when);

   result.valid_block_signing_authority             = proauth.authority;
   result.producer                                  = proauth.producer_name;
   result.last_proposed_finalizer_policy_generation = last_proposed_finalizer_policy_generation;

   result.blockroot_merkle = blockroot_merkle;
   result.blockroot_merkle.append(id);

   result.prev_pending_schedule = pending_schedule;

   if (hotstuff_activated) {
      result.confirmed                           = hs_block_confirmed;
      result.dpos_proposed_irreversible_blocknum = 0;
      // fork_database will prefer hotstuff blocks over dpos blocks
      result.dpos_irreversible_blocknum = hs_dpos_irreversible_blocknum;
      // Change to active on the next().next() producer block_num
      // TODO: use calculated hotstuff lib instead of block_num
      if (pending_schedule.schedule.producers.size() &&
          block_num >= detail::get_next_next_round_block_num(when, pending_schedule.schedule_lib_num)) {
         result.active_schedule      = pending_schedule.schedule;
         result.was_pending_promoted = true;
      } else {
         result.active_schedule = active_schedule;
      }

   } else {
      auto itr = producer_to_last_produced.find(proauth.producer_name);
      if (itr != producer_to_last_produced.end()) {
         EOS_ASSERT(itr->second < (block_num + 1) - num_prev_blocks_to_confirm, producer_double_confirm,
                    "producer ${prod} double-confirming known range",
                    ("prod", proauth.producer_name)("num", block_num + 1)("confirmed", num_prev_blocks_to_confirm)(
                       "last_produced", itr->second));
      }

      result.confirmed = num_prev_blocks_to_confirm;

      /// grow the confirmed count
      static_assert(std::numeric_limits<uint8_t>::max() >= (config::max_producers * 2 / 3) + 1,
                    "8bit confirmations may not be able to hold all of the needed confirmations");

      // This uses the previous block active_schedule because thats the "schedule" that signs and therefore confirms
      // _this_ block
      auto     num_active_producers = active_schedule.producers.size();
      uint32_t required_confs       = (uint32_t)(num_active_producers * 2 / 3) + 1;

      if (confirm_count.size() < config::maximum_tracked_dpos_confirmations) {
         result.confirm_count.reserve(confirm_count.size() + 1);
         result.confirm_count = confirm_count;
         result.confirm_count.resize(confirm_count.size() + 1);
         result.confirm_count.back() = (uint8_t)required_confs;
      } else {
         result.confirm_count.resize(confirm_count.size());
         memcpy(&result.confirm_count[0], &confirm_count[1], confirm_count.size() - 1);
         result.confirm_count.back() = (uint8_t)required_confs;
      }

      auto new_dpos_proposed_irreversible_blocknum = dpos_proposed_irreversible_blocknum;

      int32_t  i                 = (int32_t)(result.confirm_count.size() - 1);
      uint32_t blocks_to_confirm = num_prev_blocks_to_confirm + 1; /// confirm the head block too
      while (i >= 0 && blocks_to_confirm) {
         --result.confirm_count[i];
         // idump((confirm_count[i]));
         if (result.confirm_count[i] == 0) {
            uint32_t block_num_for_i = result.block_num - (uint32_t)(result.confirm_count.size() - 1 - i);
            new_dpos_proposed_irreversible_blocknum = block_num_for_i;
            // idump((dpos2_lib)(block_num)(dpos_irreversible_blocknum));

            if (i == static_cast<int32_t>(result.confirm_count.size() - 1)) {
               result.confirm_count.resize(0);
            } else {
               memmove(&result.confirm_count[0], &result.confirm_count[i + 1], result.confirm_count.size() - i - 1);
               result.confirm_count.resize(result.confirm_count.size() - i - 1);
            }

            break;
         }
         --i;
         --blocks_to_confirm;
      }

      result.dpos_proposed_irreversible_blocknum = new_dpos_proposed_irreversible_blocknum;
      result.dpos_irreversible_blocknum          = calc_dpos_last_irreversible(proauth.producer_name);

      if (pending_schedule.schedule.producers.size() &&
          result.dpos_irreversible_blocknum >= pending_schedule.schedule_lib_num) {
         result.active_schedule = pending_schedule.schedule;

         flat_map<account_name, uint32_t> new_producer_to_last_produced;

         for (const auto& pro : result.active_schedule.producers) {
            if (pro.producer_name == proauth.producer_name) {
               new_producer_to_last_produced[pro.producer_name] = result.block_num;
            } else {
               auto existing = producer_to_last_produced.find(pro.producer_name);
               if (existing != producer_to_last_produced.end()) {
                  new_producer_to_last_produced[pro.producer_name] = existing->second;
               } else {
                  new_producer_to_last_produced[pro.producer_name] = result.dpos_irreversible_blocknum;
               }
            }
         }
         new_producer_to_last_produced[proauth.producer_name] = result.block_num;

         result.producer_to_last_produced = std::move(new_producer_to_last_produced);

         flat_map<account_name, uint32_t> new_producer_to_last_implied_irb;

         for (const auto& pro : result.active_schedule.producers) {
            if (pro.producer_name == proauth.producer_name) {
               new_producer_to_last_implied_irb[pro.producer_name] = dpos_proposed_irreversible_blocknum;
            } else {
               auto existing = producer_to_last_implied_irb.find(pro.producer_name);
               if (existing != producer_to_last_implied_irb.end()) {
                  new_producer_to_last_implied_irb[pro.producer_name] = existing->second;
               } else {
                  new_producer_to_last_implied_irb[pro.producer_name] = result.dpos_irreversible_blocknum;
               }
            }
         }

         result.producer_to_last_implied_irb = std::move(new_producer_to_last_implied_irb);

         result.was_pending_promoted = true;
      } else {
         result.active_schedule                                     = active_schedule;
         result.producer_to_last_produced                           = producer_to_last_produced;
         result.producer_to_last_produced[proauth.producer_name]    = result.block_num;
         result.producer_to_last_implied_irb                        = producer_to_last_implied_irb;
         result.producer_to_last_implied_irb[proauth.producer_name] = dpos_proposed_irreversible_blocknum;
      }
   } // !hotstuff_activated
#endif
   
   return result;
}


} // namespace eosio::chain