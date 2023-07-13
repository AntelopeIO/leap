#include <eosio/chain/wasm_interface_collection.hpp>

namespace eosio { namespace chain {

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   thread_local std::unique_ptr<eosvmoc::executor> wasm_interface_collection::eosvmoc_tier::exec {};
   thread_local eosvmoc::memory wasm_interface_collection::eosvmoc_tier::mem{ wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size };
#endif

} } /// eosio::chain
