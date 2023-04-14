#pragma once
#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/config.hpp>

namespace eosio {

enum class pending_block_mode { producing, speculating };

namespace block_timing_util {

   // Calculate when a producer can start producing a given block represented by its block_time
   //
   // In the past, a producer would always start a block `config::block_interval_us` ahead of its block time. However,
   // it causes the last block in a block production round being released too late for the next producer to have
   // received it and start producing on schedule. To mitigate the problem, we leave no time gap in block producing. For
   // example, given block_interval=500 ms and cpu effort=400 ms, assuming the our round start at time point 0; in the
   // past, the block start time points would be at time point -500, 0, 500, 1000, 1500, 2000 ....  With this new
   // approach, the block time points would become -500, -100, 300, 700, 1200 ...
   fc::time_point production_round_block_start_time(uint32_t cpu_effort_us, chain::block_timestamp_type block_time) {
      uint32_t block_slot = block_time.slot;
      uint32_t production_round_start_block_slot =
            (block_slot / chain::config::producer_repetitions) * chain::config::producer_repetitions;
      uint32_t production_round_index = block_slot % chain::config::producer_repetitions;
      return chain::block_timestamp_type(production_round_start_block_slot - 1).to_time_point() +
             fc::microseconds(cpu_effort_us * production_round_index);
   }

   fc::time_point calculate_block_deadline(uint32_t cpu_effort_us, pending_block_mode mode, chain::block_timestamp_type block_time) {
      const auto hard_deadline =
                  block_time.to_time_point() - fc::microseconds(chain::config::block_interval_us - cpu_effort_us);
      if (mode == pending_block_mode::producing) {
         auto estimated_deadline = production_round_block_start_time(cpu_effort_us, block_time) + fc::microseconds(cpu_effort_us);
         auto now                = fc::time_point::now();
         if (estimated_deadline > now) {
            return estimated_deadline;
         } else {
            // This could only happen when the producer stop producing and then comes back alive in the middle of its own
            // production round. In this case, we just use the hard deadline.
            return std::min(hard_deadline, now + fc::microseconds(cpu_effort_us));
         }
      } else {
         return hard_deadline;
      }
   }
};
} // namespace eosio