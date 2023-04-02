#include <eosio/chain/wasm_module_cache.hpp>
#include <eosio/chain/exceptions.hpp>
#include "WAST/WAST.h"
#include "IR/Validate.h"

namespace eosio { namespace chain {
   const IR::Module& wasm_module_cache::get_module( const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, std::vector<U8>& bytes )
   {
      std::lock_guard g(mtx);
      module_cache_index::iterator it = module_cache.find( boost::make_tuple(code_hash, vm_type, vm_version) );
      if(it == module_cache.end()) {
         IR::Module module;
         try {
            Serialization::MemoryInputStream stream((const U8*)bytes.data(), bytes.size());
            WASM::scoped_skip_checks no_check;
            WASM::serialize(stream, module);
            module.userSections.clear();
         } catch (const Serialization::FatalSerializationException& e) {
            EOS_ASSERT(false, wasm_serialization_error, e.message.c_str());
         } catch (const IR::ValidationException& e) {
            EOS_ASSERT(false, wasm_serialization_error, e.message.c_str());
         }

         it = module_cache.emplace( module_entry {
                                      .code_hash = code_hash,
                                      .vm_type = vm_type,
                                      .vm_version = vm_version,
                                      .module = module
                                   } ).first;
      }

      return it->module;
   }

   // apply_eosio_setcode asserted this was not called by non-read-only trxs.
   // This implies it is in write window and no read-only threads are running.
   // Safe to update module_cache without mutex.
   void wasm_module_cache::code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, uint32_t block_num) {
      auto it = module_cache.find(boost::make_tuple(code_hash,vm_type, vm_version));
      if(it != module_cache.end())
         module_cache.modify(it, [block_num](module_entry& e) {
            e.last_block_num_used = block_num;
         });
      }

   // producer_plugin has already asserted irreversible_block signal is called
   // in write window. No read-only threads are running. Safe to update
   // module_cache without mutex.
   void wasm_module_cache::current_lib(uint32_t lib) {
      //anything last used before or on the LIB can be evicted
      const auto first_it = module_cache.get<by_last_block_num>().begin();
      const auto last_it  = module_cache.get<by_last_block_num>().upper_bound(lib);
      module_cache.get<by_last_block_num>().erase(first_it, last_it);
   }
} } // eosio::chain
