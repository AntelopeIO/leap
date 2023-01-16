#pragma once
#include <debug_eos_vm/dwarf.hpp>
#include <eosio/vm/backend.hpp>

namespace debug_eos_vm
{
   struct debug_instr_map
   {
      using builder = debug_instr_map;

      const void* code_begin = nullptr;
      const void* wasm_begin = nullptr;
      size_t wasm_size = 0;
      size_t code_size = 0;

      std::vector<dwarf::jit_fn_loc> fn_locs;
      std::vector<dwarf::jit_instr_loc> instr_locs;
      const dwarf::jit_instr_loc* offset_to_addr = nullptr;
      std::size_t offset_to_addr_len = 0;

      uint32_t code_offset(const void* p)
      {
         return reinterpret_cast<const char*>(p) - reinterpret_cast<const char*>(code_begin);
      }

      uint32_t wasm_offset(const void* p)
      {
         return reinterpret_cast<const char*>(p) - reinterpret_cast<const char*>(wasm_begin);
      }

      void on_code_start(const void* code_addr, const void* wasm_addr)
      {
         code_begin = code_addr;
         wasm_begin = wasm_addr;
      }

      void on_function_start(const void* code_addr, const void* wasm_addr)
      {
         fn_locs.emplace_back();
         fn_locs.back().code_prologue = code_offset(code_addr);
         fn_locs.back().wasm_begin = wasm_offset(wasm_addr);
      }

      void on_function_body(const void* code_addr)
      {
         fn_locs.back().code_body = code_offset(code_addr);
      }

      void on_function_epilogue(const void* code_addr)
      {
         fn_locs.back().code_epilogue = code_offset(code_addr);
      }

      void on_function_end(const void* code_addr, const void* wasm_addr)
      {
         fn_locs.back().code_end = code_offset(code_addr);
         fn_locs.back().wasm_end = wasm_offset(wasm_addr);
      }

      void on_instr_start(const void* code_addr, const void* wasm_addr)
      {
         instr_locs.push_back({code_offset(code_addr), wasm_offset(wasm_addr)});
      }

      void on_code_end(const void* code_addr, const void* wasm_addr)
      {
         code_size = (const char*)code_addr - (const char*)code_begin;
         wasm_size = (const char*)wasm_addr - (const char*)wasm_begin;
      }

      void set(builder&& b)
      {
         *this = std::move(b);

         {
            uint32_t code = 0;
            uint32_t wasm = 0;
            for (auto& fn : fn_locs)
            {
               EOS_VM_ASSERT(code <= fn.code_prologue &&              //
                                 fn.code_prologue <= fn.code_body &&  //
                                 fn.code_body <= fn.code_epilogue &&  //
                                 fn.code_epilogue <= fn.code_end,
                             eosio::vm::profile_exception, "function parts are out of order");
               EOS_VM_ASSERT(wasm <= fn.wasm_begin && fn.wasm_begin <= fn.wasm_end,
                             eosio::vm::profile_exception, "function wasm is out of order");
               code = fn.code_end;
               wasm = fn.wasm_end;
            }
         }

         {
            uint32_t code = 0;
            uint32_t wasm = 0;
            for (auto& instr : instr_locs)
            {
               EOS_VM_ASSERT(code <= instr.code_offset, eosio::vm::profile_exception,
                             "jit instructions are out of order");
               EOS_VM_ASSERT(wasm <= instr.wasm_addr, eosio::vm::profile_exception,
                             "jit instructions are out of order");
               code = instr.code_offset;
               wasm = instr.wasm_addr;
            }
         }

         offset_to_addr = instr_locs.data();
         offset_to_addr_len = instr_locs.size();
      }

      void relocate(const void* new_base) { code_begin = new_base; }

      std::uint32_t translate(const void* pc) const
      {
         std::size_t diff = (reinterpret_cast<const char*>(pc) -
                             reinterpret_cast<const char*>(code_begin));  // negative values wrap
         if (diff >= code_size || diff < offset_to_addr[0].code_offset)
            return 0xFFFFFFFFu;
         std::uint32_t code_offset = diff;

         // Loop invariant: offset_to_addr[lower].code_offset <= code_offset < offset_to_addr[upper].code_offset
         std::size_t lower = 0, upper = offset_to_addr_len;
         while (upper - lower > 1)
         {
            std::size_t mid = lower + (upper - lower) / 2;
            if (offset_to_addr[mid].code_offset <= code_offset)
               lower = mid;
            else
               upper = mid;
         }

         return offset_to_addr[lower].wasm_addr;
      }
   };  // debug_instr_map

// Macro because member functions can't be partially specialized
#define DEBUG_PARSE_CODE_SECTION(Host, Options)                                                   \
   template <>                                                                                    \
   template <>                                                                                    \
   void eosio::vm::binary_parser<                                                                 \
       eosio::vm::machine_code_writer<eosio::vm::jit_execution_context<Host, true>>, Options,     \
       debug_eos_vm::debug_instr_map>::                                                           \
       parse_section<eosio::vm::section_id::code_section>(                                        \
           eosio::vm::wasm_code_ptr & code,                                                       \
           eosio::vm::guarded_vector<eosio::vm::function_body> & elems)                           \
   {                                                                                              \
      const void* code_start = code.raw() - code.offset();                                        \
      parse_section_impl(code, elems,                                                             \
                         eosio::vm::detail::get_max_function_section_elements(_options),          \
                         [&](eosio::vm::wasm_code_ptr& code, eosio::vm::function_body& fb,        \
                             std::size_t idx) { parse_function_body(code, fb, idx); });           \
      EOS_VM_ASSERT(elems.size() == _mod->functions.size(), eosio::vm::wasm_parse_exception,      \
                    "code section must have the same size as the function section");              \
      eosio::vm::machine_code_writer<eosio::vm::jit_execution_context<Host, true>> code_writer(   \
          _allocator, code.bounds() - code.offset(), *_mod);                                      \
      imap.on_code_start(code_writer.get_base_addr(), code_start);                                \
      for (size_t i = 0; i < _function_bodies.size(); i++)                                        \
      {                                                                                           \
         eosio::vm::function_body& fb = _mod->code[i];                                            \
         eosio::vm::func_type& ft = _mod->types.at(_mod->functions.at(i));                        \
         local_types_t local_types(ft, fb.locals);                                                \
         imap.on_function_start(code_writer.get_addr(), _function_bodies[i].first.raw());         \
         code_writer.emit_prologue(ft, fb.locals, i);                                             \
         imap.on_function_body(code_writer.get_addr());                                           \
         parse_function_body_code(_function_bodies[i].first, fb.size, _function_bodies[i].second, \
                                  code_writer, ft, local_types);                                  \
         imap.on_function_epilogue(code_writer.get_addr());                                       \
         code_writer.emit_epilogue(ft, fb.locals, i);                                             \
         imap.on_function_end(code_writer.get_addr(), _function_bodies[i].first.bnds);            \
         code_writer.finalize(fb);                                                                \
      }                                                                                           \
      imap.on_code_end(code_writer.get_addr(), code.raw());                                       \
   }

   template <typename Backend>
   std::shared_ptr<dwarf::debugger_registration> enable_debug(std::vector<uint8_t>& code,
                                                              Backend& backend,
                                                              dwarf::info& dwarf_info,
                                                              const char* entry)
   {
      auto& module = backend.get_module();
      auto func_index = module.get_exported_function(entry);
      if (func_index == std::numeric_limits<uint32_t>::max())
         throw std::runtime_error("can not find " + std::string(entry));
      auto& alloc = module.allocator;
      auto& dbg = backend.get_debug();
      return dwarf::register_with_debugger(
          dwarf_info, dbg.fn_locs, dbg.instr_locs, alloc.get_code_start(), alloc._code_size,
          (char*)alloc.get_code_start() +
              module.code[func_index - module.get_imported_functions_size()].jit_code_offset);
   }
}  // namespace debug_eos_vm
