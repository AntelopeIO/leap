#pragma once

#include <debug_eos_vm/debug_eos_vm.hpp>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/webassembly/interface.hpp>

namespace debug_contract
{
   template <typename Backend>
   struct debugging_module
   {
      std::unique_ptr<Backend> module;
      std::shared_ptr<dwarf::debugger_registration> reg;
   };

   template <typename Backend>
   struct substitution_cache
   {
      std::map<fc::sha256, fc::sha256> substitutions;
      std::map<fc::sha256, std::vector<uint8_t>> codes;
      std::map<fc::sha256, debugging_module<Backend>> cached_modules;

      bool substitute_apply(const eosio::chain::digest_type& code_hash,
                            uint8_t vm_type,
                            uint8_t vm_version,
                            eosio::chain::apply_context& context)
      {
         if (vm_type || vm_version)
            return false;
         if (auto it = substitutions.find(code_hash); it != substitutions.end())
         {
            auto& module = *get_module(it->second).module;
            module.set_wasm_allocator(&context.control.get_wasm_allocator());
            eosio::chain::webassembly::interface iface(context);
            module.initialize(&iface);
            module.call(iface, "env", "apply", context.get_receiver().to_uint64_t(),
                        context.get_action().account.to_uint64_t(),
                        context.get_action().name.to_uint64_t());
            return true;
         }
         return false;
      }

      debugging_module<Backend>& get_module(const eosio::chain::digest_type& code_hash)
      {
         if (auto it = cached_modules.find(code_hash); it != cached_modules.end())
            return it->second;

         if (auto it = codes.find(code_hash); it != codes.end())
         {
            auto dwarf_info =
                dwarf::get_info_from_wasm({(const char*)it->second.data(), it->second.size()});
            auto size =
                dwarf::wasm_exclude_custom({(const char*)it->second.data(), it->second.size()})
                    .remaining();
            try
            {
               eosio::vm::wasm_code_ptr code(it->second.data(), size);
               auto bkend = std::make_unique<Backend>(code, size, nullptr);
               eosio::chain::eos_vm_host_functions_t::resolve(bkend->get_module());
               auto reg = debug_eos_vm::enable_debug(it->second, *bkend, dwarf_info, "apply");
               return cached_modules[code_hash] =
                          debugging_module<Backend>{std::move(bkend), std::move(reg)};
            }
            catch (eosio::vm::exception& e)
            {
               FC_THROW_EXCEPTION(eosio::chain::wasm_execution_error,
                                  "Error building eos-vm interp: ${e}", ("e", e.what()));
            }
         }
         throw std::runtime_error{"missing code for substituted module"};
      }  // get_module
   };    // substitution_cache

}  // namespace debug_contract
