#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/trace_converter.hpp>

namespace eosio {
namespace state_history {

void trace_converter::add_transaction(const transaction_trace_ptr& trace, const chain::packed_transaction_ptr& transaction) {
   if (trace->receipt) {
      if (chain::is_onblock(*trace))
         onblock_trace.emplace(trace, transaction);
      else if (trace->failed_dtrx_trace)
         cached_traces[trace->failed_dtrx_trace->id] = augmented_transaction_trace{trace, transaction};
      else
         cached_traces[trace->id] = augmented_transaction_trace{trace, transaction};
   }
}

bytes trace_converter::pack(const chainbase::database& db, bool trace_debug_mode, const block_state_ptr& block_state) {
   std::vector<augmented_transaction_trace> traces;
   if (onblock_trace)
      traces.push_back(*onblock_trace);
   for (auto& r : block_state->block->transactions) {
      transaction_id_type id;
      if (std::holds_alternative<transaction_id_type>(r.trx))
         id = std::get<transaction_id_type>(r.trx);
      else
         id = std::get<chain::packed_transaction>(r.trx).id();
      auto it = cached_traces.find(id);
      EOS_ASSERT(it != cached_traces.end() && it->second.trace->receipt, chain::plugin_exception,
                 "missing trace for transaction ${id}", ("id", id));
      traces.push_back(it->second);
   }
   cached_traces.clear();
   onblock_trace.reset();

   return fc::raw::pack(make_history_context_wrapper(db, trace_debug_mode, traces));
}

} // namespace state_history
} // namespace eosio
