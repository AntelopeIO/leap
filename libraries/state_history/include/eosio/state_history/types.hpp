#pragma once

#include <eosio/chain/trace.hpp>

namespace eosio {
namespace state_history {

using chain::block_id_type;
using chain::bytes;
using chain::extensions_type;
using chain::signature_type;
using chain::signed_transaction;
using chain::transaction_trace_ptr;

template <typename T>
struct big_vector_wrapper {
   T obj;
};

struct row_pair {
   row_pair() {}
   row_pair(const bool f, const bytes& s) : first(f), second(s){}
   bool first = false;
   bytes second;
};

struct partial_transaction {
   fc::time_point_sec          expiration             = {};
   uint16_t                    ref_block_num          = {};
   uint32_t                    ref_block_prefix       = {};
   fc::unsigned_int            max_net_usage_words    = {};
   uint8_t                     max_cpu_usage_ms       = {};
   fc::unsigned_int            delay_sec              = {};
   extensions_type             transaction_extensions = {};
   std::vector<signature_type> signatures             = {};
   std::vector<bytes>          context_free_data      = {};

   partial_transaction(const chain::packed_transaction_ptr& t)
       : expiration(t->get_transaction().expiration)
       , ref_block_num(t->get_transaction().ref_block_num)
       , ref_block_prefix(t->get_transaction().ref_block_prefix)
       , max_net_usage_words(t->get_transaction().max_net_usage_words)
       , max_cpu_usage_ms(t->get_transaction().max_cpu_usage_ms)
       , delay_sec(t->get_transaction().delay_sec)
       , transaction_extensions(t->get_transaction().transaction_extensions)
       , signatures(t->get_signed_transaction().signatures)
       , context_free_data(t->get_signed_transaction().context_free_data) {}
};

struct augmented_transaction_trace {
   transaction_trace_ptr                trace;
   std::shared_ptr<partial_transaction> partial;

   augmented_transaction_trace()                                   = default;
   augmented_transaction_trace(const augmented_transaction_trace&) = default;
   augmented_transaction_trace(augmented_transaction_trace&&)      = default;

   augmented_transaction_trace(const transaction_trace_ptr& trace)
       : trace{trace} {}

   augmented_transaction_trace(const transaction_trace_ptr& trace, const std::shared_ptr<partial_transaction>& partial)
       : trace{trace}
       , partial{partial} {}

   augmented_transaction_trace(const transaction_trace_ptr& trace, const chain::packed_transaction_ptr& t)
       : trace{trace}
       , partial{std::make_shared<partial_transaction>(t)} {}

   augmented_transaction_trace& operator=(const augmented_transaction_trace&) = default;
   augmented_transaction_trace& operator=(augmented_transaction_trace&&) = default;
};

struct table_delta {
   fc::unsigned_int                                                       struct_version = 0;
   std::string                                                            name{};
   state_history::big_vector_wrapper<std::vector<row_pair>>               rows{};
};

struct block_position {
   uint32_t      block_num = 0;
   block_id_type block_id  = {};
};

struct get_status_request_v0 {};

struct get_status_result_v0 {
   block_position head                    = {};
   block_position last_irreversible       = {};
   uint32_t       trace_begin_block       = 0;
   uint32_t       trace_end_block         = 0;
   uint32_t       chain_state_begin_block = 0;
   uint32_t       chain_state_end_block   = 0;
   fc::sha256     chain_id                = {};
};

struct get_blocks_request_v0 {
   uint32_t                    start_block_num        = 0;
   uint32_t                    end_block_num          = 0;
   uint32_t                    max_messages_in_flight = 0;
   std::vector<block_position> have_positions         = {};
   bool                        irreversible_only      = false;
   bool                        fetch_block            = false;
   bool                        fetch_traces           = false;
   bool                        fetch_deltas           = false;
};

struct get_blocks_ack_request_v0 {
   uint32_t num_messages = 0;
};

struct get_blocks_result_base {
   block_position                head;
   block_position                last_irreversible;
   std::optional<block_position> this_block;
   std::optional<block_position> prev_block;
   std::optional<bytes>          block;
};

struct get_blocks_result_v0 : get_blocks_result_base {
   std::optional<bytes>          traces;
   std::optional<bytes>          deltas;
};

using state_request = std::variant<get_status_request_v0, get_blocks_request_v0, get_blocks_ack_request_v0>;
using state_result  = std::variant<get_status_result_v0, get_blocks_result_v0>;

} // namespace state_history
} // namespace eosio

// clang-format off
FC_REFLECT(eosio::state_history::table_delta, (struct_version)(name)(rows));
FC_REFLECT(eosio::state_history::block_position, (block_num)(block_id));
FC_REFLECT_EMPTY(eosio::state_history::get_status_request_v0);
FC_REFLECT(eosio::state_history::get_status_result_v0, (head)(last_irreversible)(trace_begin_block)(trace_end_block)(chain_state_begin_block)(chain_state_end_block)(chain_id));
FC_REFLECT(eosio::state_history::get_blocks_request_v0, (start_block_num)(end_block_num)(max_messages_in_flight)(have_positions)(irreversible_only)(fetch_block)(fetch_traces)(fetch_deltas));
FC_REFLECT(eosio::state_history::get_blocks_ack_request_v0, (num_messages));
FC_REFLECT(eosio::state_history::get_blocks_result_base, (head)(last_irreversible)(this_block)(prev_block)(block));
FC_REFLECT_DERIVED(eosio::state_history::get_blocks_result_v0, (eosio::state_history::get_blocks_result_base), (traces)(deltas));
// clang-format on
