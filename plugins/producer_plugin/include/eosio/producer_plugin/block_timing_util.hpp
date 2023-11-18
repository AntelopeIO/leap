#pragma once
#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/producer_schedule.hpp>

namespace eosio {

enum class pending_block_mode { producing, speculating };

namespace block_timing_util {

   // Store watermarks
   // Watermarks are recorded times that the specified producer has produced.
   // Used by calculate_producer_wake_up_time to skip over already produced blocks avoiding duplicate production.
   class producer_watermarks {
   public:
      void consider_new_watermark(chain::account_name producer, uint32_t block_num, chain::block_timestamp_type timestamp) {
         auto itr = _producer_watermarks.find(producer);
         if (itr != _producer_watermarks.end()) {
            itr->second.first  = std::max(itr->second.first, block_num);
            itr->second.second = std::max(itr->second.second, timestamp);
         } else {
            _producer_watermarks.emplace(producer, std::make_pair(block_num, timestamp));
         }
      }

      using producer_watermark = std::pair<uint32_t, chain::block_timestamp_type>;
      std::optional<producer_watermark> get_watermark(chain::account_name producer) const {
         auto itr = _producer_watermarks.find(producer);

         if (itr == _producer_watermarks.end())
            return {};

         return itr->second;
      }
   private:
      std::map<chain::account_name, producer_watermark> _producer_watermarks;
   };

   // Calculate when a producer can start producing a given block represented by its block_time
   //
   // In the past, a producer would always start a block `config::block_interval_us` ahead of its block time. However,
   // it causes the last block in a block production round being released too late for the next producer to have
   // received it and start producing on schedule. To mitigate the problem, we leave no time gap in block producing. For
   // example, given block_interval=500 ms and cpu effort=400 ms, assuming our round starts at time point 0; in the
   // past, the block start time points would be at time point -500, 0, 500, 1000, 1500, 2000 ....  With this new
   // approach, the block time points would become -500, -100, 300, 700, 1100 ...
   inline fc::time_point production_round_block_start_time(fc::microseconds cpu_effort, chain::block_timestamp_type block_time) {
      uint32_t block_slot = block_time.slot;
      uint32_t production_round_start_block_slot =
            (block_slot / chain::config::producer_repetitions) * chain::config::producer_repetitions;
      uint32_t production_round_index = block_slot % chain::config::producer_repetitions;
      return chain::block_timestamp_type(production_round_start_block_slot - 1).to_time_point() +
             fc::microseconds(cpu_effort.count() * production_round_index);
   }

   inline fc::time_point calculate_producing_block_deadline(fc::microseconds cpu_effort, chain::block_timestamp_type block_time) {
      return production_round_block_start_time(cpu_effort, block_time) + cpu_effort;
   }

   namespace detail {
      inline uint32_t calculate_next_block_slot(const chain::account_name& producer_name, uint32_t current_block_slot, uint32_t block_num,
                                                size_t producer_index, size_t active_schedule_size, const producer_watermarks& prod_watermarks) {
         uint32_t minimum_offset = 1; // must at least be the "next" block

         // account for a watermark in the future which is disqualifying this producer for now
         // this is conservative assuming no blocks are dropped.  If blocks are dropped the watermark will
         // disqualify this producer for longer but it is assumed they will wake up, determine that they
         // are disqualified for longer due to skipped blocks and re-calculate their next block with better
         // information then
         auto current_watermark = prod_watermarks.get_watermark(producer_name);
         if (current_watermark) {
            const auto watermark = *current_watermark;
            if (watermark.first > block_num) {
               // if I have a watermark block number then I need to wait until after that watermark
               minimum_offset = watermark.first - block_num + 1;
            }
            if (watermark.second.slot > current_block_slot) {
               // if I have a watermark block timestamp then I need to wait until after that watermark timestamp
               minimum_offset = std::max(minimum_offset, watermark.second.slot - current_block_slot + 1);
            }
         }

         // this producers next opportunity to produce is the next time its slot arrives after or at the calculated minimum
         uint32_t minimum_slot = current_block_slot + minimum_offset;
         size_t   minimum_slot_producer_index =
            (minimum_slot % (active_schedule_size * chain::config::producer_repetitions)) / chain::config::producer_repetitions;
         if (producer_index == minimum_slot_producer_index) {
            // this is the producer for the minimum slot, go with that
            return minimum_slot;
         } else {
            // calculate how many rounds are between the minimum producer and the producer in question
            size_t producer_distance = producer_index - minimum_slot_producer_index;
            // check for unsigned underflow
            if (producer_distance > producer_index) {
               producer_distance += active_schedule_size;
            }

            // align the minimum slot to the first of its set of reps
            uint32_t first_minimum_producer_slot = minimum_slot - (minimum_slot % chain::config::producer_repetitions);

            // offset the aligned minimum to the *earliest* next set of slots for this producer
            uint32_t next_block_slot = first_minimum_producer_slot + (producer_distance * chain::config::producer_repetitions);
            return next_block_slot;
         }
      }
   }

   // Return the *next* block start time according to its block time slot.
   // Returns empty optional if no producers are in the active_schedule.
   // block_num is only used for watermark minimum offset.
   inline std::optional<fc::time_point> calculate_producer_wake_up_time(fc::microseconds cpu_effort, uint32_t block_num,
                                                                        const chain::block_timestamp_type& ref_block_time,
                                                                        const std::set<chain::account_name>& producers,
                                                                        const std::vector<chain::producer_authority>& active_schedule,
                                                                        const producer_watermarks& prod_watermarks) {
      auto ref_block_slot = ref_block_time.slot;
      // if we have any producers then we should at least set a timer for our next available slot
      uint32_t wake_up_slot = UINT32_MAX;
      for (const auto& p : producers) {
         // determine if this producer is in the active schedule and if so, where
         auto itr = std::find_if(active_schedule.begin(), active_schedule.end(), [&](const auto& asp) { return asp.producer_name == p; });
         if (itr == active_schedule.end()) {
            continue;
         }
         size_t producer_index = itr - active_schedule.begin();

         auto next_producer_block_slot = detail::calculate_next_block_slot(p, ref_block_slot, block_num, producer_index, active_schedule.size(), prod_watermarks);
         wake_up_slot                  = std::min(next_producer_block_slot, wake_up_slot);
      }
      if (wake_up_slot == UINT32_MAX) {
         return {};
      }

      return production_round_block_start_time(cpu_effort, chain::block_timestamp_type(wake_up_slot));
   }

} // namespace block_timing_util
} // namespace eosio