#pragma once
#include <eosio/chain/wasm_interface.hpp>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace eosio::chain {

   /**
    * @class wasm_interface_collection manages the active wasm_interface to use for execution.
    */
   class wasm_interface_collection {
      public:
         inline static bool test_disable_tierup = false; // set by unittests to test tierup failing

         wasm_interface_collection(wasm_interface::vm_type vm, wasm_interface::vm_oc_enable eosvmoc_tierup,
                                   const chainbase::database& d, const std::filesystem::path& data_dir,
                                   const eosvmoc::config& eosvmoc_config, bool profile);

         ~wasm_interface_collection();

         void apply(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, apply_context& context);

         // used for tests, only valid on main thread
         bool is_code_cached(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) {
            EOS_ASSERT(is_on_main_thread(), wasm_execution_error, "is_code_cached called off the main thread");
            return wasmif.is_code_cached(code_hash, vm_type, vm_version);
         }

         // update current lib of all wasm interfaces
         void current_lib(const uint32_t lib);

         // only called from non-main threads (read-only trx execution threads) when producer_plugin starts them
         void init_thread_local_data(const chainbase::database& d, const std::filesystem::path& data_dir, const eosvmoc::config& eosvmoc_config, bool profile);

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         bool is_eos_vm_oc_enabled() const {
            return ((eosvmoc_tierup != wasm_interface::vm_oc_enable::oc_none) || wasm_runtime == wasm_interface::vm_type::eos_vm_oc);
         }
#endif

         void code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, uint32_t block_num);

         // If substitute_apply is set, then apply calls it before doing anything else. If substitute_apply returns true,
         // then apply returns immediately. Provided function must be multi-thread safe.
         std::function<bool(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, apply_context& context)> substitute_apply;

      private:
         bool is_on_main_thread() { return main_thread_id == std::this_thread::get_id(); };

      private:
         const std::thread::id main_thread_id;
         const wasm_interface::vm_type wasm_runtime;
         const wasm_interface::vm_oc_enable eosvmoc_tierup;

         wasm_interface wasmif;  // used by main thread
         std::mutex threaded_wasmifs_mtx;
         std::unordered_map<std::thread::id, std::unique_ptr<wasm_interface>> threaded_wasmifs; // one for each read-only thread, used by eos-vm and eos-vm-jit

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         std::unique_ptr<struct eosvmoc_tier> eosvmoc; // used by all threads
#endif
   };

} // eosio::chain
