#pragma once
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/whitelisted_intrinsics.hpp>
#include <eosio/chain/exceptions.hpp>
#include <functional>

namespace eosio { namespace chain {

   class apply_context;
   class wasm_runtime_interface;
   class controller;
   namespace eosvmoc { struct config; }

   struct wasm_exit {
      int32_t code = 0;
   };

   /**
    * @class wasm_interface
    *
    */
   class wasm_interface {
      public:
         enum class vm_type {
            eos_vm,
            eos_vm_jit,
            eos_vm_oc
         };

         //return string description of vm_type
         static std::string vm_type_string(vm_type vmtype) {
             switch (vmtype) {
             case vm_type::eos_vm:
                return "eos-vm";
             case vm_type::eos_vm_oc:
                return "eos-vm-oc";
             default:
                 return "eos-vm-jit";
             }
         }

         enum class vm_oc_enable {
            oc_auto,
            oc_all,
            oc_none
         };

         inline static bool test_disable_tierup = false; // set by unittests to test tierup failing

         wasm_interface(vm_type vm, vm_oc_enable eosvmoc_tierup, const chainbase::database& d, const std::filesystem::path data_dir, const eosvmoc::config& eosvmoc_config, bool profile);
         ~wasm_interface();

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         // initialize exec per thread
         void init_thread_local_data();

         // returns true if EOS VM OC is enabled
         bool is_eos_vm_oc_enabled() const;
#endif

         //call before dtor to skip what can be minutes of dtor overhead with some runtimes; can cause leaks
         void indicate_shutting_down();

         //validates code -- does a WASM validation pass and checks the wasm against EOSIO specific constraints
         static void validate(const controller& control, const bytes& code);

         //indicate that a particular code probably won't be used after given block_num
         void code_block_num_last_used(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, const uint32_t& block_num);

         //indicate the current LIB. evicts old cache entries
         void current_lib(const uint32_t lib);

         //Calls apply or error on a given code
         void apply(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, apply_context& context);

         //Returns true if the code is cached
         bool is_code_cached(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) const;

         // If substitute_apply is set, then apply calls it before doing anything else. If substitute_apply returns true,
         // then apply returns immediately. Provided function must be multi-thread safe.
         std::function<bool(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, apply_context& context)> substitute_apply;

      private:
         vm_oc_enable eosvmoc_tierup;
         unique_ptr<struct wasm_interface_impl> my;
   };

} } // eosio::chain

namespace eosio{ namespace chain {
   std::istream& operator>>(std::istream& in, wasm_interface::vm_type& runtime);
   inline std::ostream& operator<<(std::ostream& os, wasm_interface::vm_oc_enable t) {
      if (t == wasm_interface::vm_oc_enable::oc_auto) {
         os << "auto";
      } else if (t == wasm_interface::vm_oc_enable::oc_all) {
         os << "all";
      } else if (t == wasm_interface::vm_oc_enable::oc_none) {
         os << "none";
      }
      return os;
   }
}}

FC_REFLECT_ENUM( eosio::chain::wasm_interface::vm_type, (eos_vm)(eos_vm_jit)(eos_vm_oc) )
