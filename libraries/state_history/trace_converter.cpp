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

void trace_converter::pack(boost::iostreams::filtering_ostreambuf& obuf, bool trace_debug_mode, const chain::signed_block_ptr& block) {
   std::vector<augmented_transaction_trace> traces;
   if (onblock_trace)
      traces.push_back(*onblock_trace);
   for (auto& r : block->transactions) {
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

   fc::datastream<boost::iostreams::filtering_ostreambuf&> ds{obuf};
   return fc::raw::pack(ds, make_history_context_wrapper(trace_debug_mode, traces));
}

} // namespace state_history
} // namespace eosio
