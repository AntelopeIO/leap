#pragma once

#include <eosio/stream.hpp>
#include <memory>
#include <optional>

namespace dwarf
{
   // Location of jitted function
   struct jit_fn_loc
   {
      // offsets relative to beginning of generated code
      uint32_t code_prologue = 0;
      uint32_t code_body = 0;
      uint32_t code_epilogue = 0;
      uint32_t code_end = 0;

      // offsets relative to beginning of wasm file
      uint32_t wasm_begin = 0;
      uint32_t wasm_end = 0;
   };

   // Location of jitted instruction
   struct jit_instr_loc
   {
      uint32_t code_offset;  // Relative to beginning of generated code
      uint32_t wasm_addr;    // Relative to beginning of wasm file
   };

   // Location of line extracted from DWARF
   struct location
   {
      // Addresses relative to code section content (after section id and section length)
      uint32_t begin_address = 0;
      uint32_t end_address = 0;
      uint32_t file_index = 0;
      uint32_t line = 0;

      friend bool operator<(const location& a, const location& b)
      {
         return a.begin_address < b.begin_address;
      }
   };

   // Location of subprogram extracted from DWARF
   struct subprogram
   {
      // Addresses relative to code section content (after id and section length)
      uint32_t begin_address;
      uint32_t end_address;
      std::optional<std::string> linkage_name;
      std::optional<std::string> name;
      std::string demangled_name;
      std::optional<uint32_t> parent;
      std::vector<uint32_t> children;

      auto key() const { return std::pair{begin_address, ~end_address}; }

      friend bool operator<(const subprogram& a, const subprogram& b) { return a.key() < b.key(); }
   };

   struct abbrev_attr
   {
      uint32_t name = 0;
      uint32_t form = 0;
   };

   // Abbreviation extracted from DWARF
   struct abbrev_decl
   {
      uint32_t table_offset = 0;
      uint32_t code = 0;
      uint32_t tag = 0;
      bool has_children = false;
      std::vector<abbrev_attr> attrs;

      auto key() const { return std::pair{table_offset, code}; }
      friend bool operator<(const abbrev_decl& a, const abbrev_decl& b)
      {
         return a.key() < b.key();
      }
   };

   // Position of function within wasm file
   struct wasm_fn
   {
      // offsets relative to beginning of file
      uint32_t size_pos = 0;
      uint32_t locals_pos = 0;
      uint32_t end_pos = 0;
   };

   struct info
   {
      // Offset of code section content (after id and section length) within wasm file
      uint32_t wasm_code_offset = 0;

      std::vector<char> strings;
      std::vector<std::string> files;
      std::vector<location> locations;        // sorted
      std::vector<abbrev_decl> abbrev_decls;  // sorted
      std::vector<subprogram> subprograms;    // sorted
      std::vector<wasm_fn> wasm_fns;          // in wasm order

      const char* get_str(uint32_t offset) const;
      const location* get_location(uint32_t address) const;
      const abbrev_decl* get_abbrev_decl(uint32_t table_offset, uint32_t code) const;
      const subprogram* get_subprogram(uint32_t address) const;
   };

   eosio::input_stream wasm_exclude_custom(eosio::input_stream stream);
   info get_info_from_wasm(eosio::input_stream stream);

   struct debugger_registration;
   std::shared_ptr<debugger_registration> register_with_debugger(  //
       info& info,
       const std::vector<jit_fn_loc>& fn_locs,
       const std::vector<jit_instr_loc>& instr_locs,
       const void* code_start,
       size_t code_size,
       const void* entry);
}  // namespace dwarf
