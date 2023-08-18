#include <eosio/chain/wasm_interface_collection.hpp>
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
#include <eosio/chain/webassembly/eos-vm-oc.hpp>
#else
#define _REGISTER_EOSVMOC_INTRINSIC(CLS, MOD, METHOD, WASM_SIG, NAME, SIG)
#endif

namespace eosio::chain {

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
struct eosvmoc_tier {
   // Called from main thread
   eosvmoc_tier(const std::filesystem::path& d, const eosvmoc::config& c, const chainbase::database& db)
      : cc(d, c, db) {
      // Construct exec and mem for the main thread
      exec = std::make_unique<eosvmoc::executor>(cc);
      mem  = std::make_unique<eosvmoc::memory>(wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size);
   }

   // Called from read-only threads
   void init_thread_local_data() {
      exec = std::make_unique<eosvmoc::executor>(cc);
      mem  = std::make_unique<eosvmoc::memory>(eosvmoc::memory::sliced_pages_for_ro_thread);
   }

   eosvmoc::code_cache_async cc;

   // Each thread requires its own exec and mem.
   thread_local static std::unique_ptr<eosvmoc::executor> exec;
   thread_local static std::unique_ptr<eosvmoc::memory>   mem;
};

thread_local std::unique_ptr<eosvmoc::executor> eosvmoc_tier::exec{};
thread_local std::unique_ptr<eosvmoc::memory>   eosvmoc_tier::mem{};
#endif

wasm_interface_collection::wasm_interface_collection(wasm_interface::vm_type vm, wasm_interface::vm_oc_enable eosvmoc_tierup,
                                                     const chainbase::database& d, const std::filesystem::path& data_dir,
                                                     const eosvmoc::config& eosvmoc_config, bool profile)
   : main_thread_id(std::this_thread::get_id())
   , wasm_runtime(vm)
   , eosvmoc_tierup(eosvmoc_tierup)
   , wasmif(vm, d, data_dir, eosvmoc_config, profile) {
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   if (eosvmoc_tierup != wasm_interface::vm_oc_enable::oc_none) {
      EOS_ASSERT(vm != wasm_interface::vm_type::eos_vm_oc, wasm_exception, "You can't use EOS VM OC as the base runtime when tier up is activated");
      eosvmoc = std::make_unique<eosvmoc_tier>(data_dir, eosvmoc_config, d);
   }
#endif
}

wasm_interface_collection::~wasm_interface_collection() = default;

void wasm_interface_collection::apply(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, apply_context& context) {
   if (substitute_apply && substitute_apply(code_hash, vm_type, vm_version, context))
      return;
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   if (eosvmoc && (eosvmoc_tierup == wasm_interface::vm_oc_enable::oc_all || context.should_use_eos_vm_oc())) {
      const chain::eosvmoc::code_descriptor* cd = nullptr;
      chain::eosvmoc::code_cache_base::get_cd_failure failure = chain::eosvmoc::code_cache_base::get_cd_failure::temporary;
      try {
         const bool high_priority = context.get_receiver().prefix() == chain::config::system_account_name;
         cd = eosvmoc->cc.get_descriptor_for_code(high_priority, code_hash, vm_version, context.control.is_write_window(), failure);
         if (test_disable_tierup)
            cd = nullptr;
      } catch (...) {
         // swallow errors here, if EOS VM OC has gone in to the weeds we shouldn't bail: continue to try and run baseline
         // In the future, consider moving bits of EOS VM that can fire exceptions and such out of this call path
         static bool once_is_enough;
         if (!once_is_enough)
            elog("EOS VM OC has encountered an unexpected failure");
         once_is_enough = true;
      }
      if (cd) {
         if (!context.is_applying_block()) // read_only_trx_test.py looks for this log statement
            tlog("${a} speculatively executing ${h} with eos vm oc", ("a", context.get_receiver())("h", code_hash));
         eosvmoc->exec->execute(*cd, *eosvmoc->mem, context);
         return;
      }
   }
#endif
   if (is_on_main_thread()) {
      wasmif.apply(code_hash, vm_type, vm_version, context);
      return;
   }
   threaded_wasmifs[std::this_thread::get_id()]->apply(code_hash, vm_type, vm_version, context);
}

// update current lib of all wasm interfaces
void wasm_interface_collection::current_lib(const uint32_t lib) {
   // producer_plugin has already asserted irreversible_block signal is called in write window
   std::function<void(const digest_type& code_hash, uint8_t vm_version)> cb{};
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   if (eosvmoc) {
      cb = [&](const digest_type& code_hash, uint8_t vm_version) {
         eosvmoc->cc.free_code(code_hash, vm_version);
      };
   }
#endif
   wasmif.current_lib(lib, cb);
   for (auto& w : threaded_wasmifs) {
      w.second->current_lib(lib, cb);
   }
}

// only called from non-main threads (read-only trx execution threads) when producer_plugin starts them
void wasm_interface_collection::init_thread_local_data(const chainbase::database& d, const std::filesystem::path& data_dir,
                                                       const eosvmoc::config& eosvmoc_config, bool profile) {
   EOS_ASSERT(!is_on_main_thread(), misc_exception, "init_thread_local_data called on the main thread");
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   if (is_eos_vm_oc_enabled()) {
      // EOSVMOC needs further initialization of its thread local data
      if (eosvmoc)
         eosvmoc->init_thread_local_data();
      wasmif.init_thread_local_data();
   }
#endif

   std::lock_guard g(threaded_wasmifs_mtx);
   // Non-EOSVMOC needs a wasmif per thread
   threaded_wasmifs[std::this_thread::get_id()] = std::make_unique<wasm_interface>(wasm_runtime, d, data_dir, eosvmoc_config, profile);
}

void wasm_interface_collection::code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, uint32_t block_num) {
   // The caller of this function apply_eosio_setcode has already asserted that
   // the transaction is not a read-only trx, which implies we are
   // in write window. Safe to call threaded_wasmifs's code_block_num_last_used
   wasmif.code_block_num_last_used(code_hash, vm_type, vm_version, block_num);
   for (auto& w : threaded_wasmifs) {
      w.second->code_block_num_last_used(code_hash, vm_type, vm_version, block_num);
   }
}

} // namespace eosio::chain
