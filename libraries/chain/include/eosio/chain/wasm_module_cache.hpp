#pragma once

#include <eosio/chain/types.hpp>  // for digest_type
#include "IR/Module.h"
#include <eosio/chain/multi_index_includes.hpp>

namespace eosio { namespace chain {
   struct wasm_module_cache {
      const IR::Module& get_module( const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, std::vector<U8>& bytes );
      void code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, uint32_t block_num);
      void current_lib(uint32_t lib);

      struct module_entry {
         digest_type   code_hash;
         uint8_t       vm_type = 0;
         uint8_t       vm_version = 0;
         uint32_t      first_block_num_used;
         uint32_t      last_block_num_used;
         IR::Module    module;
      };
      struct by_hash;
      struct by_first_block_num;
      struct by_last_block_num;

   private:
      typedef boost::multi_index_container<
         module_entry,
         indexed_by<
            ordered_unique<tag<by_hash>,
               composite_key<module_entry,
                  member<module_entry, digest_type, &module_entry::code_hash>,
                  member<module_entry, uint8_t,     &module_entry::vm_type>,
                  member<module_entry, uint8_t,     &module_entry::vm_version>
               >
            >,
            ordered_non_unique<tag<by_first_block_num>, member<module_entry, uint32_t, &module_entry::first_block_num_used>>,
            ordered_non_unique<tag<by_last_block_num>, member<module_entry, uint32_t, &module_entry::last_block_num_used>>
         >
      > module_cache_index;

      std::mutex mtx;
      module_cache_index module_cache;
   };

} } // eosio::chain
