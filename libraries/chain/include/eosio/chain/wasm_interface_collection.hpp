#pragma once
#include <eosio/chain/wasm_interface.hpp>

namespace eosio::chain {

   /**
    * @class wasm_interface_collection manages the active wasm_interface to use for execution.
    */
   class wasm_interface_collection {
      public:
         wasm_interface_collection(wasm_interface::vm_type vm, wasm_interface::vm_oc_enable eosvmoc_tierup,
                                   const chainbase::database& d, const std::filesystem::path& data_dir,
                                   const eosvmoc::config& eosvmoc_config, bool profile)
            : main_thread_id(std::this_thread::get_id())
            , wasm_runtime(vm)
            , eosvmoc_tierup(eosvmoc_tierup)
            , wasmif(vm, eosvmoc_tierup, d, data_dir, eosvmoc_config, profile)
         {}

         wasm_interface& get_wasm_interface() {
            if (is_on_main_thread() || is_eos_vm_oc_enabled()) {
               return wasmif;
            }
            return *threaded_wasmifs[std::this_thread::get_id()];
         }


         // update current lib of all wasm interfaces
         void current_lib(const uint32_t lib) {
            // producer_plugin has already asserted irreversible_block signal is called in write window
            wasmif.current_lib(lib);
            for (auto& w: threaded_wasmifs) {
               w.second->current_lib(lib);
            }
         }

         // only called from non-main threads (read-only trx execution threads) when producer_plugin starts them
         void init_thread_local_data(const chainbase::database& d, const std::filesystem::path& data_dir,
                                     const eosvmoc::config& eosvmoc_config, bool profile) {
            EOS_ASSERT(!is_on_main_thread(), misc_exception, "init_thread_local_data called on the main thread");
            if (is_eos_vm_oc_enabled()) {
               // EOSVMOC needs further initialization of its thread local data
               wasmif.init_thread_local_data();
            } else {
               std::lock_guard g(threaded_wasmifs_mtx);
               // Non-EOSVMOC needs a wasmif per thread
               threaded_wasmifs[std::this_thread::get_id()] = std::make_unique<wasm_interface>(wasm_runtime, eosvmoc_tierup, d, data_dir, eosvmoc_config, profile);
            }
         }

         bool is_eos_vm_oc_enabled() const {
            return ((eosvmoc_tierup != wasm_interface::vm_oc_enable::oc_none) || wasm_runtime == wasm_interface::vm_type::eos_vm_oc);
         }

         void code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, uint32_t block_num) {
            // The caller of this function apply_eosio_setcode has already asserted that
            // the transaction is not a read-only trx, which implies we are
            // in write window. Safe to call threaded_wasmifs's code_block_num_last_used
            wasmif.code_block_num_last_used(code_hash, vm_type, vm_version, block_num);
            for (auto& w: threaded_wasmifs) {
               w.second->code_block_num_last_used(code_hash, vm_type, vm_version, block_num);
            }
         }

      private:
         bool is_on_main_thread() { return main_thread_id == std::this_thread::get_id(); };

      private:
         const std::thread::id main_thread_id;
         const wasm_interface::vm_type wasm_runtime;
         const wasm_interface::vm_oc_enable eosvmoc_tierup;

         wasm_interface wasmif;  // used by main thread and all threads for EOSVMOC
         std::mutex threaded_wasmifs_mtx;
         std::unordered_map<std::thread::id, std::unique_ptr<wasm_interface>> threaded_wasmifs; // one for each read-only thread, used by eos-vm and eos-vm-jit
   };

} // eosio::chain
