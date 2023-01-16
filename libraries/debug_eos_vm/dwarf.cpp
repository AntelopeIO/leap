// Notes:
// * Only supports DWARF version 4
// * Only supports DWARF produced by clang 11 or 12 in 32-bit WASM mode

#include <debug_eos_vm/dwarf.hpp>

#include <eosio/finally.hpp>
#include <eosio/from_bin.hpp>
#include <eosio/to_bin.hpp>
#include <eosio/vm/constants.hpp>
#include <eosio/vm/sections.hpp>

#include <cxxabi.h>
#include <elf.h>
#include <stdio.h>

static constexpr bool show_parsed_lines = false;
static constexpr bool show_parsed_abbrev = false;
static constexpr bool show_parsed_dies = false;
static constexpr bool show_wasm_fn_info = false;
static constexpr bool show_wasm_loc_summary = false;
static constexpr bool show_wasm_subp_summary = false;
static constexpr bool show_fn_locs = false;
static constexpr bool show_instr_locs = false;
static constexpr bool show_generated_lines = false;
static constexpr bool show_generated_dies = false;
static constexpr uint64_t print_addr_adj = 0;

namespace
{
   template <class... Ts>
   struct overloaded : Ts...
   {
      using Ts::operator()...;
   };
   template <class... Ts>
   overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace

#define ENUM_DECL(prefix, type, name, value) inline constexpr type prefix##name = value;
#define ENUM_DECODE(prefix, _, name, value) \
   case value:                              \
      return prefix #name;

namespace dwarf
{
   inline constexpr uint8_t lns_version = 4;
   inline constexpr uint8_t compile_unit_version = 4;

   inline constexpr uint8_t dw_lns_copy = 0x01;
   inline constexpr uint8_t dw_lns_advance_pc = 0x02;
   inline constexpr uint8_t dw_lns_advance_line = 0x03;
   inline constexpr uint8_t dw_lns_set_file = 0x04;
   inline constexpr uint8_t dw_lns_set_column = 0x05;
   inline constexpr uint8_t dw_lns_negate_stmt = 0x06;
   inline constexpr uint8_t dw_lns_set_basic_block = 0x07;
   inline constexpr uint8_t dw_lns_const_add_pc = 0x08;
   inline constexpr uint8_t dw_lns_fixed_advance_pc = 0x09;
   inline constexpr uint8_t dw_lns_set_prologue_end = 0x0a;
   inline constexpr uint8_t dw_lns_set_epilogue_begin = 0x0b;
   inline constexpr uint8_t dw_lns_set_isa = 0x0c;

   inline constexpr uint8_t dw_lne_end_sequence = 0x01;
   inline constexpr uint8_t dw_lne_set_address = 0x02;
   inline constexpr uint8_t dw_lne_define_file = 0x03;
   inline constexpr uint8_t dw_lne_set_discriminator = 0x04;
   inline constexpr uint8_t dw_lne_lo_user = 0x80;
   inline constexpr uint8_t dw_lne_hi_user = 0xff;

   inline constexpr uint16_t dw_lang_c_plus_plus = 0x0004;

   inline constexpr uint8_t dw_inl_not_inlined = 0x00;
   inline constexpr uint8_t dw_inl_inlined = 0x01;
   inline constexpr uint8_t dw_inl_declared_not_inlined = 0x02;
   inline constexpr uint8_t dw_inl_declared_inlined = 0x03;

// clang-format off
#define DW_ATS(a, b, x)                \
   x(a, b, sibling, 0x01)              \
   x(a, b, location, 0x02)             \
   x(a, b, name, 0x03)                 \
   x(a, b, ordering, 0x09)             \
   x(a, b, byte_size, 0x0b)            \
   x(a, b, bit_offset, 0x0c)           \
   x(a, b, bit_size, 0x0d)             \
   x(a, b, stmt_list, 0x10)            \
   x(a, b, low_pc, 0x11)               \
   x(a, b, high_pc, 0x12)              \
   x(a, b, language, 0x13)             \
   x(a, b, discr, 0x15)                \
   x(a, b, discr_value, 0x16)          \
   x(a, b, visibility, 0x17)           \
   x(a, b, import, 0x18)               \
   x(a, b, string_length, 0x19)        \
   x(a, b, common_reference, 0x1a)     \
   x(a, b, comp_dir, 0x1b)             \
   x(a, b, const_value, 0x1c)          \
   x(a, b, containing_type, 0x1d)      \
   x(a, b, default_value, 0x1e)        \
   x(a, b, inline, 0x20)               \
   x(a, b, is_optional, 0x21)          \
   x(a, b, lower_bound, 0x22)          \
   x(a, b, producer, 0x25)             \
   x(a, b, prototyped, 0x27)           \
   x(a, b, return_addr, 0x2a)          \
   x(a, b, start_scope, 0x2c)          \
   x(a, b, bit_stride, 0x2e)           \
   x(a, b, upper_bound, 0x2f)          \
   x(a, b, abstract_origin, 0x31)      \
   x(a, b, accessibility, 0x32)        \
   x(a, b, address_class, 0x33)        \
   x(a, b, artificial, 0x34)           \
   x(a, b, base_types, 0x35)           \
   x(a, b, calling_convention, 0x36)   \
   x(a, b, count, 0x37)                \
   x(a, b, data_member_location, 0x38) \
   x(a, b, decl_column, 0x39)          \
   x(a, b, decl_file, 0x3a)            \
   x(a, b, decl_line, 0x3b)            \
   x(a, b, declaration, 0x3c)          \
   x(a, b, discr_list, 0x3d)           \
   x(a, b, encoding, 0x3e)             \
   x(a, b, external, 0x3f)             \
   x(a, b, frame_base, 0x40)           \
   x(a, b, friend, 0x41)               \
   x(a, b, identifier_case, 0x42)      \
   x(a, b, macro_info, 0x43)           \
   x(a, b, namelist_item, 0x44)        \
   x(a, b, priority, 0x45)             \
   x(a, b, segment, 0x46)              \
   x(a, b, specification, 0x47)        \
   x(a, b, static_link, 0x48)          \
   x(a, b, type, 0x49)                 \
   x(a, b, use_location, 0x4a)         \
   x(a, b, variable_parameter, 0x4b)   \
   x(a, b, virtuality, 0x4c)           \
   x(a, b, vtable_elem_location, 0x4d) \
   x(a, b, allocated, 0x4e)            \
   x(a, b, associated, 0x4f)           \
   x(a, b, data_location, 0x50)        \
   x(a, b, byte_stride, 0x51)          \
   x(a, b, entry_pc, 0x52)             \
   x(a, b, use_UTF8, 0x53)             \
   x(a, b, extension, 0x54)            \
   x(a, b, ranges, 0x55)               \
   x(a, b, trampoline, 0x56)           \
   x(a, b, call_column, 0x57)          \
   x(a, b, call_file, 0x58)            \
   x(a, b, call_line, 0x59)            \
   x(a, b, description, 0x5a)          \
   x(a, b, binary_scale, 0x5b)         \
   x(a, b, decimal_scale, 0x5c)        \
   x(a, b, small, 0x5d)                \
   x(a, b, decimal_sign, 0x5e)         \
   x(a, b, digit_count, 0x5f)          \
   x(a, b, picture_string, 0x60)       \
   x(a, b, mutable, 0x61)              \
   x(a, b, threads_scaled, 0x62)       \
   x(a, b, explicit, 0x63)             \
   x(a, b, object_pointer, 0x64)       \
   x(a, b, endianity, 0x65)            \
   x(a, b, elemental, 0x66)            \
   x(a, b, pure, 0x67)                 \
   x(a, b, recursive, 0x68)            \
   x(a, b, signature, 0x69)            \
   x(a, b, main_subprogram, 0x6a)      \
   x(a, b, data_bit_offset, 0x6b)      \
   x(a, b, const_expr, 0x6c)           \
   x(a, b, enum_class, 0x6d)           \
   x(a, b, linkage_name, 0x6e)         \
   x(a, b, lo_user, 0x2000)            \
   x(a, b, hi_user, 0x3fff)
   // clang-format on

   DW_ATS(dw_at_, uint16_t, ENUM_DECL)
   std::string dw_at_to_str(uint16_t value)
   {
      switch (value)
      {
         DW_ATS("DW_AT_", _, ENUM_DECODE)
         default:
            return "DW_AT_" + std::to_string(value);
      }
   }

// clang-format off
#define DW_FORMS(a, b, x)           \
   x(a, b, addr, 0x01)              \
   x(a, b, block2, 0x03)            \
   x(a, b, block4, 0x04)            \
   x(a, b, data2, 0x05)             \
   x(a, b, data4, 0x06)             \
   x(a, b, data8, 0x07)             \
   x(a, b, string, 0x08)            \
   x(a, b, block, 0x09)             \
   x(a, b, block1, 0x0a)            \
   x(a, b, data1, 0x0b)             \
   x(a, b, flag, 0x0c)              \
   x(a, b, sdata, 0x0d)             \
   x(a, b, strp, 0x0e)              \
   x(a, b, udata, 0x0f)             \
   x(a, b, ref_addr, 0x10)          \
   x(a, b, ref1, 0x11)              \
   x(a, b, ref2, 0x12)              \
   x(a, b, ref4, 0x13)              \
   x(a, b, ref8, 0x14)              \
   x(a, b, ref_udata, 0x15)         \
   x(a, b, indirect, 0x16)          \
   x(a, b, sec_offset, 0x17)        \
   x(a, b, exprloc, 0x18)           \
   x(a, b, flag_present, 0x19)      \
   x(a, b, ref_sig8, 0x20)
   // clang-format on

   DW_FORMS(dw_form_, uint8_t, ENUM_DECL)
   std::string dw_form_to_str(uint8_t value)
   {
      switch (value)
      {
         DW_FORMS("DW_FORM_", _, ENUM_DECODE)
         default:
            return "DW_FORM_" + std::to_string(value);
      }
   }

// clang-format off
#define DW_TAGS(a, b, x)                     \
   x(a, b, array_type, 0x01)                 \
   x(a, b, class_type, 0x02)                 \
   x(a, b, entry_point, 0x03)                \
   x(a, b, enumeration_type, 0x04)           \
   x(a, b, formal_parameter, 0x05)           \
   x(a, b, imported_declaration, 0x08)       \
   x(a, b, label, 0x0a)                      \
   x(a, b, lexical_block, 0x0b)              \
   x(a, b, member, 0x0d)                     \
   x(a, b, pointer_type, 0x0f)               \
   x(a, b, reference_type, 0x10)             \
   x(a, b, compile_unit, 0x11)               \
   x(a, b, string_type, 0x12)                \
   x(a, b, structure_type, 0x13)             \
   x(a, b, subroutine_type, 0x15)            \
   x(a, b, typedef, 0x16)                    \
   x(a, b, union_type, 0x17)                 \
   x(a, b, unspecified_parameters, 0x18)     \
   x(a, b, variant, 0x19)                    \
   x(a, b, common_block, 0x1a)               \
   x(a, b, common_inclusion, 0x1b)           \
   x(a, b, inheritance, 0x1c)                \
   x(a, b, inlined_subroutine, 0x1d)         \
   x(a, b, module, 0x1e)                     \
   x(a, b, ptr_to_member_type, 0x1f)         \
   x(a, b, set_type, 0x20)                   \
   x(a, b, subrange_type, 0x21)              \
   x(a, b, with_stmt, 0x22)                  \
   x(a, b, access_declaration, 0x23)         \
   x(a, b, base_type, 0x24)                  \
   x(a, b, catch_block, 0x25)                \
   x(a, b, const_type, 0x26)                 \
   x(a, b, constant, 0x27)                   \
   x(a, b, enumerator, 0x28)                 \
   x(a, b, file_type, 0x29)                  \
   x(a, b, friend, 0x2a)                     \
   x(a, b, namelist, 0x2b)                   \
   x(a, b, namelist_item, 0x2c)              \
   x(a, b, packed_type, 0x2d)                \
   x(a, b, subprogram, 0x2e)                 \
   x(a, b, template_type_parameter, 0x2f)    \
   x(a, b, template_value_parameter, 0x30)   \
   x(a, b, thrown_type, 0x31)                \
   x(a, b, try_block, 0x32)                  \
   x(a, b, variant_part, 0x33)               \
   x(a, b, variable, 0x34)                   \
   x(a, b, volatile_type, 0x35)              \
   x(a, b, dwarf_procedure, 0x36)            \
   x(a, b, restrict_type, 0x37)              \
   x(a, b, interface_type, 0x38)             \
   x(a, b, namespace, 0x39)                  \
   x(a, b, imported_module, 0x3a)            \
   x(a, b, unspecified_type, 0x3b)           \
   x(a, b, partial_unit, 0x3c)               \
   x(a, b, imported_unit, 0x3d)              \
   x(a, b, condition, 0x3f)                  \
   x(a, b, shared_type, 0x40)                \
   x(a, b, type_unit, 0x41)                  \
   x(a, b, rvalue_reference_type, 0x42)      \
   x(a, b, template_alias, 0x43)             \
   x(a, b, lo_user, 0x4080)                  \
   x(a, b, hi_user, 0xfff  )
   // clang-format on

   DW_TAGS(dw_tag_, uint16_t, ENUM_DECL)
   std::string dw_tag_to_str(uint16_t value)
   {
      switch (value)
      {
         DW_TAGS("DW_TAG_", _, ENUM_DECODE)
         default:
            return "DW_TAG_" + std::to_string(value);
      }
   }

   std::string_view get_string(eosio::input_stream& s)
   {
      auto begin = s.pos;
      while (true)
      {
         if (s.pos == s.end)
            throw std::runtime_error("error reading string in dwarf info");
         auto ch = *s.pos++;
         if (!ch)
            break;
      }
      return {begin, size_t(s.pos - begin - 1)};
   }

   void get_strings(std::vector<std::string>& v, eosio::input_stream& s)
   {
      while (true)
      {
         auto str = get_string(s);
         if (str.empty())
            break;
         v.push_back(std::string{str});
      }
   }

   template <typename Stream>
   void write_string(const std::string& s, Stream& stream)
   {
      stream.write(s.c_str(), s.size() + 1);
   }

   struct line_header
   {
      uint8_t minimum_instruction_length = 1;
      uint8_t maximum_operations_per_instruction = 1;
      uint8_t default_is_stmt = 1;
      int8_t line_base = -5;
      uint8_t line_range = 14;
      uint8_t opcode_base = 13;
      std::vector<uint8_t> standard_opcode_lengths = {0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
      std::vector<std::string> include_directories;
      std::vector<std::string> file_names;
   };

   template <typename S>
   void from_bin(line_header& obj, S& s)
   {
      eosio::from_bin(obj.minimum_instruction_length, s);
      eosio::from_bin(obj.maximum_operations_per_instruction, s);
      eosio::from_bin(obj.default_is_stmt, s);
      eosio::from_bin(obj.line_base, s);
      eosio::from_bin(obj.line_range, s);
      eosio::from_bin(obj.opcode_base, s);
      obj.standard_opcode_lengths.clear();
      obj.standard_opcode_lengths.push_back(0);
      for (int i = 1; i < obj.opcode_base; ++i)
         obj.standard_opcode_lengths.push_back(eosio::from_bin<uint8_t>(s));
      obj.include_directories.push_back("");
      get_strings(obj.include_directories, s);

      obj.file_names.push_back("");
      while (true)
      {
         auto str = (std::string)get_string(s);
         if (str.empty())
            break;
         auto dir = eosio::varuint32_from_bin(s);
         auto mod_time = eosio::varuint32_from_bin(s);
         auto filesize = eosio::varuint32_from_bin(s);
         eosio::check(dir <= obj.include_directories.size(),
                      "invalid include_directory number in .debug_line");
         // Assumes dir will be 0 for absolute paths. Not required by the spec,
         // but it's what clang currently does.
         if (dir)
            str = obj.include_directories[dir] + "/" + str;
         obj.file_names.push_back(std::move(str));
      }
   }  // from_bin(line_header)

   template <typename S>
   void to_bin(const line_header& obj, S& s)
   {
      eosio::to_bin(obj.minimum_instruction_length, s);
      eosio::to_bin(obj.maximum_operations_per_instruction, s);
      eosio::to_bin(obj.default_is_stmt, s);
      eosio::to_bin(obj.line_base, s);
      eosio::to_bin(obj.line_range, s);
      eosio::to_bin(obj.opcode_base, s);
      eosio::check(obj.standard_opcode_lengths.size() == obj.opcode_base,
                   "mismatched standard_opcode_lengths size");
      for (int i = 1; i < obj.opcode_base; ++i)
         eosio::to_bin<uint8_t>(obj.standard_opcode_lengths[i], s);
      for (int i = 1; i < obj.include_directories.size(); ++i)
         write_string(obj.include_directories[i], s);
      s.write(0);

      for (int i = 1; i < obj.file_names.size(); ++i)
      {
         write_string(obj.file_names[i], s);
         s.write(0);  // dir
         s.write(0);  // mod_time
         s.write(0);  // filesize
      }
      s.write(0);
   }  // to_bin(line_header)

   struct line_state
   {
      std::optional<uint32_t> sequence_begin;
      uint32_t address = 0;
      uint32_t file = 1;
      uint32_t line = 1;
      uint32_t column = 0;
      bool is_stmt = false;
      bool basic_block = false;
      bool end_sequence = false;
      bool prologue_end = false;
      bool epilogue_begin = false;
      uint32_t isa = 0;
      uint32_t discriminator = 0;
   };

   void parse_debug_line_unit_header(line_header& header, eosio::input_stream& s)
   {
      auto version = eosio::from_bin<uint16_t>(s);
      eosio::check(version == lns_version, ".debug_line isn't from DWARF version 4");
      uint32_t header_length = eosio::from_bin<uint32_t>(s);
      eosio::check(header_length <= s.remaining(), "bad header_length in .debug_line");
      auto instructions_pos = s.pos + header_length;
      from_bin(header, s);
      eosio::check(instructions_pos == s.pos, "mismatched header_length in .debug_line");
   }

   void parse_debug_line_unit(info& result,
                              std::map<std::string, uint32_t>& files,
                              eosio::input_stream s)
   {
      line_header header;
      parse_debug_line_unit_header(header, s);
      eosio::check(header.minimum_instruction_length == 1,
                   "mismatched minimum_instruction_length in .debug_line");
      eosio::check(header.maximum_operations_per_instruction == 1,
                   "mismatched maximum_operations_per_instruction in .debug_line");
      line_state state;
      state.is_stmt = header.default_is_stmt;
      auto initial_state = state;

      std::optional<location> current;
      auto add_row = [&] {
         if (!state.sequence_begin)
            state.sequence_begin = state.address;
         if (current && (state.end_sequence || state.file != current->file_index ||
                         state.line != current->line))
         {
            current->end_address = state.address;
            eosio::check(current->file_index < header.file_names.size(),
                         "invalid file index in .debug_line");
            auto& filename = header.file_names[current->file_index];
            auto it = files.find(filename);
            if (it == files.end())
            {
               it = files.insert({filename, result.files.size()}).first;
               result.files.push_back(filename);
            }
            current->file_index = it->second;
            if (show_parsed_lines)
               fprintf(stderr, "%08x [%08x,%08x) %s:%d\n", *state.sequence_begin,
                       current->begin_address, current->end_address,
                       result.files[current->file_index].c_str(), current->line);
            if (*state.sequence_begin && *state.sequence_begin < 0xffff'ffff && current->line)
               result.locations.push_back(*current);
            current = {};
         }
         if (!state.end_sequence && !current)
            current = location{.begin_address = state.address,
                               .end_address = state.address,
                               .file_index = state.file,
                               .line = state.line};
      };

      while (s.remaining())
      {
         auto opcode = eosio::from_bin<uint8_t>(s);
         if (!opcode)
         {
            auto size = eosio::varuint32_from_bin(s);
            eosio::check(size <= s.remaining(), "bytecode overrun in .debug_line");
            eosio::input_stream extended{s.pos, s.pos + size};
            s.skip(size);
            auto extended_opcode = eosio::from_bin<uint8_t>(extended);
            switch (extended_opcode)
            {
               case dw_lne_end_sequence:
                  state.end_sequence = true;
                  add_row();
                  state = initial_state;
                  break;
               case dw_lne_set_address:
                  state.address = eosio::from_bin<uint32_t>(extended);
                  break;
               case dw_lne_set_discriminator:
                  state.discriminator = eosio::varuint32_from_bin(extended);
                  break;
               default:
                  if (show_parsed_lines)
                     fprintf(stderr, "extended opcode %d\n", (int)extended_opcode);
                  break;
            }
         }
         else if (opcode < header.opcode_base)
         {
            switch (opcode)
            {
               case dw_lns_copy:
                  add_row();
                  state.discriminator = 0;
                  state.basic_block = false;
                  state.prologue_end = false;
                  state.epilogue_begin = false;
                  break;
               case dw_lns_advance_pc:
                  state.address += eosio::varuint32_from_bin(s);
                  break;
               case dw_lns_advance_line:
                  state.line += sleb32_from_bin(s);
                  break;
               case dw_lns_set_file:
                  state.file = eosio::varuint32_from_bin(s);
                  break;
               case dw_lns_set_column:
                  state.column = eosio::varuint32_from_bin(s);
                  break;
               case dw_lns_negate_stmt:
                  state.is_stmt = !state.is_stmt;
                  break;
               case dw_lns_set_basic_block:
                  state.basic_block = true;
                  break;
               case dw_lns_const_add_pc:
                  state.address += (255 - header.opcode_base) / header.line_range;
                  break;
               case dw_lns_fixed_advance_pc:
                  state.address += eosio::from_bin<uint16_t>(s);
                  break;
               case dw_lns_set_prologue_end:
                  state.prologue_end = true;
                  break;
               case dw_lns_set_epilogue_begin:
                  state.epilogue_begin = true;
                  break;
               case dw_lns_set_isa:
                  state.isa = eosio::varuint32_from_bin(s);
                  break;
               default:
                  if (show_parsed_lines)
                  {
                     fprintf(stderr, "opcode %d\n", (int)opcode);
                     fprintf(stderr, "  args: %d\n", header.standard_opcode_lengths[opcode]);
                  }
                  for (uint8_t i = 0; i < header.standard_opcode_lengths[opcode]; ++i)
                     eosio::varuint32_from_bin(s);
                  break;
            }
         }  // opcode < header.opcode_base
         else
         {
            state.address += (opcode - header.opcode_base) / header.line_range;
            state.line += header.line_base + ((opcode - header.opcode_base) % header.line_range);
            add_row();
            state.basic_block = false;
            state.prologue_end = false;
            state.epilogue_begin = false;
            state.discriminator = 0;
         }
      }  // while (s.remaining())
   }     // parse_debug_line_unit

   void parse_debug_line(info& result,
                         std::map<std::string, uint32_t>& files,
                         eosio::input_stream s)
   {
      while (s.remaining())
      {
         uint32_t unit_length = eosio::from_bin<uint32_t>(s);
         eosio::check(unit_length < 0xffff'fff0,
                      "unit_length values in reserved range in .debug_line not supported");
         eosio::check(unit_length <= s.remaining(), "bad unit_length in .debug_line");
         parse_debug_line_unit(result, files, {s.pos, s.pos + unit_length});
         s.skip(unit_length);
      }
   }

   // wasm_addr is relative to beginning of file
   std::optional<uint32_t> get_wasm_fn(const info& info, uint32_t wasm_addr)
   {
      auto it = std::upper_bound(info.wasm_fns.begin(), info.wasm_fns.end(), wasm_addr,
                                 [](auto a, const auto& b) { return a < b.size_pos; });
      if (it == info.wasm_fns.begin())
         return {};
      --it;
      if (it->size_pos <= wasm_addr && wasm_addr < it->end_pos)
         return &*it - &info.wasm_fns[0];
      return {};
   }

   std::optional<std::pair<uint64_t, uint64_t>> get_addr_range(
       const info& info,
       const std::vector<jit_fn_loc>& fn_locs,
       const std::vector<jit_instr_loc>& instr_locs,
       const void* code_start,
       uint32_t begin,
       uint32_t end)
   {
      // TODO: cuts off a range which ends at the wasm's end
      auto it1 =
          std::lower_bound(instr_locs.begin(), instr_locs.end(), info.wasm_code_offset + begin,
                           [](const auto& a, auto b) { return a.wasm_addr < b; });
      auto it2 = std::lower_bound(instr_locs.begin(), instr_locs.end(), info.wasm_code_offset + end,
                                  [](const auto& a, auto b) { return a.wasm_addr < b; });
      if (it1 < it2 && it2 != instr_locs.end())
      {
         return std::pair{uint64_t((char*)code_start + it1->code_offset),
                          uint64_t((char*)code_start + it2->code_offset)};
      }
      return {};
   }

   template <typename S>
   void write_line_program(const info& info,
                           const std::vector<jit_fn_loc>& fn_locs,
                           const std::vector<jit_instr_loc>& instr_locs,
                           const void* code_start,
                           S& s)
   {
      uint64_t address = 0;
      uint32_t file = 1;
      uint32_t line = 1;

      auto extended = [&](auto f) {
         eosio::to_bin(uint8_t(0), s);
         eosio::size_stream sz;
         f(sz);
         eosio::varuint32_to_bin(sz.size, s);
         f(s);
      };

      for (auto& loc : info.locations)
      {
         auto range = get_addr_range(info, fn_locs, instr_locs, code_start, loc.begin_address,
                                     loc.end_address);
         if (!range)
            continue;

         if (show_generated_lines)
            fprintf(stderr, "%016lx-%016lx %s:%d\n", range->first + print_addr_adj,
                    range->second + print_addr_adj, info.files[loc.file_index].c_str(), loc.line);
         if (range->first != address)
         {
            if (range->first < address)
            {
               extended([&](auto& s) {
                  eosio::to_bin(uint8_t(dw_lne_end_sequence), s);
                  file = 1;
                  line = 1;
               });
            }
            extended([&](auto& s) {
               eosio::to_bin(uint8_t(dw_lne_set_address), s);
               eosio::to_bin(range->first, s);
               address = range->first;
            });
         }

         if (file != loc.file_index + 1)
         {
            eosio::to_bin(uint8_t(dw_lns_set_file), s);
            eosio::varuint32_to_bin(loc.file_index + 1, s);
            file = loc.file_index + 1;
         }

         if (line != loc.line)
         {
            eosio::to_bin(uint8_t(dw_lns_advance_line), s);
            eosio::sleb64_to_bin(int32_t(loc.line - line), s);
            line = loc.line;
         }

         eosio::to_bin(uint8_t(dw_lns_copy), s);

         if (address != range->second)
         {
            extended([&](auto& s) {
               eosio::to_bin(uint8_t(dw_lne_set_address), s);
               eosio::to_bin(range->second, s);
               address = range->second;
            });
         }
      }  // for(loc)

      extended([&](auto& s) {  //
         eosio::to_bin(uint8_t(dw_lne_end_sequence), s);
      });
   }  // write_line_program

   std::vector<char> generate_debug_line(const info& info,
                                         const std::vector<jit_fn_loc>& fn_locs,
                                         const std::vector<jit_instr_loc>& instr_locs,
                                         const void* code_start)
   {
      line_header header;
      header.file_names.push_back("");
      header.file_names.insert(header.file_names.end(), info.files.begin(), info.files.end());
      eosio::size_stream header_size;
      to_bin(header, header_size);
      eosio::size_stream program_size;
      write_line_program(info, fn_locs, instr_locs, code_start, program_size);

      std::vector<char> result(header_size.size + program_size.size + 22);
      eosio::fixed_buf_stream s{result.data(), result.size()};
      eosio::to_bin(uint32_t(0xffff'ffff), s);
      eosio::to_bin(uint64_t(header_size.size + program_size.size + 10), s);
      eosio::to_bin(uint16_t(lns_version), s);
      eosio::to_bin(uint64_t(header_size.size), s);
      to_bin(header, s);
      write_line_program(info, fn_locs, instr_locs, code_start, s);
      eosio::check(s.pos == s.end, "generate_debug_line: calculated incorrect stream size");
      return result;
   }

   void parse_debug_abbrev(info& result,
                           std::map<std::string, uint32_t>& files,
                           eosio::input_stream s)
   {
      auto begin = s.pos;
      while (s.remaining())
      {
         uint32_t table_offset = s.pos - begin;
         while (true)
         {
            abbrev_decl decl;
            decl.table_offset = table_offset;
            decl.code = eosio::varuint32_from_bin(s);
            if (!decl.code)
               break;
            decl.tag = eosio::varuint32_from_bin(s);
            decl.has_children = eosio::from_bin<uint8_t>(s);
            while (true)
            {
               abbrev_attr attr;
               attr.name = eosio::varuint32_from_bin(s);
               attr.form = eosio::varuint32_from_bin(s);
               if (!attr.name)
               {
                  eosio::check(!attr.form, "incorrectly terminated abbreviation");
                  break;
               }
               decl.attrs.push_back(attr);
            }
            if (show_parsed_abbrev)
               fprintf(stderr, "%08x [%d]: tag: %s children: %d attrs: %d\n", decl.table_offset,
                       decl.code, dw_tag_to_str(decl.tag).c_str(), decl.has_children,
                       (int)decl.attrs.size());
            result.abbrev_decls.push_back(std::move(decl));
         }
      }
   }

   struct attr_address
   {
      uint32_t value = 0;
   };

   struct attr_block
   {
      eosio::input_stream data;
   };

   struct attr_data
   {
      uint64_t value = 0;
   };

   struct attr_exprloc
   {
      eosio::input_stream data;
   };

   struct attr_flag
   {
      bool value = false;
   };

   struct attr_sec_offset
   {
      uint32_t value = 0;
   };

   struct attr_ref
   {
      uint64_t value = 0;
   };

   struct attr_ref_addr
   {
      uint32_t value = 0;
   };

   struct attr_ref_sig8
   {
      uint64_t value = 0;
   };

   using attr_value = std::variant<  //
       attr_address,
       attr_block,
       attr_data,
       attr_exprloc,
       attr_flag,
       attr_sec_offset,
       attr_ref,
       attr_ref_addr,
       attr_ref_sig8,
       std::string_view>;

   std::string hex(uint32_t v)
   {
      char b[11];
      snprintf(b, sizeof(b), "0x%08x", v);
      return b;
   }

   std::string to_string(const attr_value& v)
   {
      overloaded o{
          [](const attr_address& s) { return hex(s.value); },        //
          [](const attr_sec_offset& s) { return hex(s.value); },     //
          [](const attr_ref& s) { return hex(s.value); },            //
          [](const std::string_view& s) { return (std::string)s; },  //
          [](const auto&) { return std::string{}; }                  //
      };
      return std::visit(o, v);
   }

   std::optional<uint32_t> get_address(const attr_value& v)
   {
      if (auto* x = std::get_if<attr_address>(&v))
         return x->value;
      return {};
   }

   std::optional<uint64_t> get_data(const attr_value& v)
   {
      if (auto* x = std::get_if<attr_data>(&v))
         return x->value;
      return {};
   }

   std::optional<uint64_t> get_ref(const attr_value& v)
   {
      if (auto* x = std::get_if<attr_ref>(&v))
         return x->value;
      return {};
   }

   std::optional<std::string_view> get_string(const attr_value& v)
   {
      if (auto* x = std::get_if<std::string_view>(&v))
         return *x;
      return {};
   }

   attr_value parse_attr_value(info& result, uint32_t form, eosio::input_stream& s)
   {
      auto vardata = [&](size_t size) {
         eosio::check(size <= s.remaining(), "variable-length overrun in dwarf entry");
         eosio::input_stream result{s.pos, s.pos + size};
         s.skip(size);
         return result;
      };

      switch (form)
      {
         case dw_form_addr:
            return attr_address{eosio::from_bin<uint32_t>(s)};
         case dw_form_block:
            return attr_block{vardata(eosio::varuint32_from_bin(s))};
         case dw_form_block1:
            return attr_block{vardata(eosio::from_bin<uint8_t>(s))};
         case dw_form_block2:
            return attr_block{vardata(eosio::from_bin<uint16_t>(s))};
         case dw_form_block4:
            return attr_block{vardata(eosio::from_bin<uint32_t>(s))};
         case dw_form_sdata:
            return attr_data{(uint64_t)eosio::sleb64_from_bin(s)};
         case dw_form_udata:
            return attr_data{eosio::varuint64_from_bin(s)};
         case dw_form_data1:
            return attr_data{eosio::from_bin<uint8_t>(s)};
         case dw_form_data2:
            return attr_data{eosio::from_bin<uint16_t>(s)};
         case dw_form_data4:
            return attr_data{eosio::from_bin<uint32_t>(s)};
         case dw_form_data8:
            return attr_data{eosio::from_bin<uint64_t>(s)};
         case dw_form_exprloc:
            return attr_exprloc{vardata(eosio::varuint32_from_bin(s))};
         case dw_form_flag_present:
            return attr_flag{true};
         case dw_form_flag:
            return attr_flag{(bool)eosio::from_bin<uint8_t>(s)};
         case dw_form_sec_offset:
            return attr_sec_offset{eosio::from_bin<uint32_t>(s)};
         case dw_form_ref_udata:
            return attr_ref{eosio::varuint64_from_bin(s)};
         case dw_form_ref1:
            return attr_ref{eosio::from_bin<uint8_t>(s)};
         case dw_form_ref2:
            return attr_ref{eosio::from_bin<uint16_t>(s)};
         case dw_form_ref4:
            return attr_ref{eosio::from_bin<uint32_t>(s)};
         case dw_form_ref8:
            return attr_ref{eosio::from_bin<uint64_t>(s)};
         case dw_form_ref_addr:
            return attr_ref_addr{eosio::from_bin<uint32_t>(s)};
         case dw_form_ref_sig8:
            return attr_ref_sig8{eosio::from_bin<uint64_t>(s)};
         case dw_form_string:
            return get_string(s);
         case dw_form_strp:
            return std::string_view{result.get_str(eosio::from_bin<uint32_t>(s))};
         case dw_form_indirect:
            return parse_attr_value(result, eosio::varuint32_from_bin(s), s);
         default:
            throw std::runtime_error("unknown form in dwarf entry");
      }
   }  // parse_attr_value

   const abbrev_decl* get_die_abbrev(info& result,
                                     int indent,
                                     uint32_t debug_abbrev_offset,
                                     const eosio::input_stream& whole_s,
                                     eosio::input_stream& s)
   {
      const char* p = s.pos;
      auto code = eosio::varuint32_from_bin(s);
      if (!code)
      {
         if (show_parsed_dies)
            fprintf(stderr, "0x%08x: %*sNULL\n", uint32_t(p - whole_s.pos), indent - 12, "");
         return nullptr;
      }
      const auto* abbrev = result.get_abbrev_decl(debug_abbrev_offset, code);
      eosio::check(abbrev, "Bad abbrev in .debug_info");
      if (show_parsed_dies)
         fprintf(stderr, "0x%08x: %*s%s\n", uint32_t(p - whole_s.pos), indent - 12, "",
                 dw_tag_to_str(abbrev->tag).c_str());
      return abbrev;
   }

   template <typename F>
   void parse_die_attrs(info& result,
                        int indent,
                        uint32_t debug_abbrev_offset,
                        const abbrev_decl& abbrev,
                        const eosio::input_stream& whole_s,
                        const eosio::input_stream& unit_s,
                        eosio::input_stream& s,
                        F&& f)
   {
      for (const auto& attr : abbrev.attrs)
      {
         auto value = parse_attr_value(result, attr.form, s);
         if (show_parsed_dies)
            fprintf(stderr, "%*s%s %s: %s\n", indent + 2, "", dw_at_to_str(attr.name).c_str(),
                    dw_form_to_str(attr.form).c_str(), to_string(value).c_str());
         if (attr.name == dw_at_specification)
         {
            if (auto ref = get_ref(value))
            {
               if (show_parsed_dies)
                  fprintf(stderr, "%*sref: %08x, unit: %08x\n", indent + 4, "", uint32_t(*ref),
                          uint32_t(unit_s.pos - whole_s.pos));
               eosio::check(*ref < unit_s.remaining(), "DW_AT_specification out of range");
               eosio::input_stream ref_s{unit_s.pos + *ref, unit_s.end};
               auto ref_abbrev =
                   get_die_abbrev(result, indent + 4, debug_abbrev_offset, whole_s, ref_s);
               parse_die_attrs(result, indent + 4, debug_abbrev_offset, *ref_abbrev, whole_s,
                               unit_s, ref_s, f);
            }
         }
         else
            f(attr, value);
      }
   }

   std::string demangle(const std::string& name)
   {
      auto result = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, nullptr);
      auto fin = eosio::finally{[&] { free(result); }};
      if (result)
      {
         std::string x = result;
         return x;
      }
      return name;
   }

   struct common_attrs
   {
      std::optional<uint32_t> low_pc;
      std::optional<uint32_t> high_pc;
      std::optional<std::string> linkage_name;
      std::optional<std::string> name;

      std::string get_demangled_name() const
      {
         if (linkage_name)
            return demangle(*linkage_name);
         if (name)
            return *name;
         return "";
      }

      void operator()(const abbrev_attr& attr, attr_value& value)
      {
         switch (attr.name)
         {
            case dw_at_low_pc:
               low_pc = get_address(value);
               break;
            case dw_at_high_pc:
               high_pc = get_address(value);
               if (low_pc && !high_pc)
               {
                  auto size = get_data(value);
                  if (size)
                     high_pc = *low_pc + *size;
               }
               break;
            case dw_at_linkage_name:
               linkage_name = get_string(value);
               break;
            case dw_at_name:
               name = get_string(value);
               break;
            default:
               break;
         }
      }
   };  // common_attrs

   void skip_die_children(info& result,
                          int indent,
                          uint32_t debug_abbrev_offset,
                          const abbrev_decl& abbrev,
                          const eosio::input_stream& whole_s,
                          const eosio::input_stream& unit_s,
                          eosio::input_stream& s)
   {
      if (!abbrev.has_children)
         return;
      while (true)
      {
         auto* child = get_die_abbrev(result, indent, debug_abbrev_offset, whole_s, s);
         if (!child)
            break;
         parse_die_attrs(result, indent + 4, debug_abbrev_offset, *child, whole_s, unit_s, s,
                         [&](auto&&...) {});
         skip_die_children(result, indent + 4, debug_abbrev_offset, *child, whole_s, unit_s, s);
      }
   }

   void parse_die_children(info& result,
                           uint32_t indent,
                           uint32_t debug_abbrev_offset,
                           const abbrev_decl& abbrev,
                           const eosio::input_stream& whole_s,
                           const eosio::input_stream& unit_s,
                           eosio::input_stream& s)
   {
      if (!abbrev.has_children)
         return;
      while (true)
      {
         auto* child = get_die_abbrev(result, indent, debug_abbrev_offset, whole_s, s);
         if (!child)
            break;
         common_attrs common;
         parse_die_attrs(result, indent + 4, debug_abbrev_offset, *child, whole_s, unit_s, s,
                         common);
         if (child->tag == dw_tag_subprogram)
         {
            auto demangled_name = common.get_demangled_name();
            if (!demangled_name.empty() && common.low_pc && *common.low_pc &&
                *common.low_pc < 0xffff'ffff && common.high_pc)
            {
               subprogram p{
                   .begin_address = *common.low_pc,
                   .end_address = *common.high_pc,
                   .linkage_name = common.linkage_name,
                   .name = common.name,
                   .demangled_name = demangled_name,
               };
               if (show_parsed_dies)
               {
                  fprintf(stderr, "%*sbegin_address  = %08x\n", indent + 6, "", p.begin_address);
                  fprintf(stderr, "%*send_address    = %08x\n", indent + 6, "", p.end_address);
                  fprintf(stderr, "%*sdemangled_name = %s\n", indent + 6, "",
                          p.demangled_name.c_str());
               }
               result.subprograms.push_back(std::move(p));
               parse_die_children(result, indent + 4, debug_abbrev_offset, *child, whole_s, unit_s,
                                  s);
            }
            else
               skip_die_children(result, indent + 4, debug_abbrev_offset, *child, whole_s, unit_s,
                                 s);
         }
         else
            parse_die_children(result, indent + 4, debug_abbrev_offset, *child, whole_s, unit_s, s);
      }
   }  // parse_die_children

   void parse_debug_info_unit(info& result,
                              const eosio::input_stream& whole_s,
                              const eosio::input_stream& unit_s,
                              eosio::input_stream s)
   {
      uint32_t indent = 12;
      auto version = eosio::from_bin<uint16_t>(s);
      eosio::check(version == compile_unit_version, ".debug_info isn't from DWARF version 4");
      auto debug_abbrev_offset = eosio::from_bin<uint32_t>(s);
      auto address_size = eosio::from_bin<uint8_t>(s);
      eosio::check(address_size == 4, "mismatched address_size in .debug_info");

      auto* root = get_die_abbrev(result, indent, debug_abbrev_offset, whole_s, s);
      eosio::check(root && root->tag == dw_tag_compile_unit,
                   "missing DW_TAG_compile_unit in .debug_info");
      parse_die_attrs(result, indent + 4, debug_abbrev_offset, *root, whole_s, unit_s, s,
                      [&](auto&&...) {});
      parse_die_children(result, indent + 4, debug_abbrev_offset, *root, whole_s, unit_s, s);
   }  // parse_debug_info_unit

   size_t fill_parents(info& result, size_t parent, size_t pos)
   {
      auto& par = result.subprograms[parent];
      while (true)
      {
         if (pos >= result.subprograms.size())
            return pos;
         auto& subp = result.subprograms[pos];
         if (subp.begin_address >= par.end_address)
            return pos;
         eosio::check(subp.end_address <= par.end_address, "partial overlap in subprograms");
         par.children.push_back(pos);
         subp.parent = parent;
         pos = fill_parents(result, pos, pos + 1);
      }
   }

   void parse_debug_info(info& result, eosio::input_stream s)
   {
      auto whole_s = s;
      while (s.remaining())
      {
         auto unit_s = s;
         uint32_t unit_length = eosio::from_bin<uint32_t>(s);
         eosio::check(unit_length < 0xffff'fff0,
                      "unit_length values in reserved range in .debug_info not supported");
         eosio::check(unit_length <= s.remaining(), "bad unit_length in .debug_info");
         parse_debug_info_unit(result, whole_s, unit_s, {s.pos, s.pos + unit_length});
         s.skip(unit_length);
      }
      std::sort(result.subprograms.begin(), result.subprograms.end());
      for (size_t pos = 0; pos < result.subprograms.size();)
         pos = fill_parents(result, pos, pos + 1);
   }

   Elf64_Word add_str(std::vector<char>& strings, const char* s)
   {
      if (!s || !*s)
         return size_t(0);
      auto result = strings.size();
      strings.insert(strings.end(), s, s + strlen(s) + 1);
      return result;
   }

   struct attr_form_value
   {
      using value_type = std::variant<uint8_t, uint64_t, std::string>;

      uint32_t attr = 0;
      uint32_t form = 0;
      value_type value;

      auto key() const { return std::pair{attr, form}; }

      friend bool operator<(const attr_form_value& a, const attr_form_value& b)
      {
         return a.key() < b.key();
      }
   };

   struct die_pattern
   {
      uint32_t tag = 0;
      bool has_children = false;
      std::vector<attr_form_value> attrs;

      auto key() const { return std::tie(tag, has_children, attrs); }

      friend bool operator<(const die_pattern& a, const die_pattern& b)
      {
         return a.key() < b.key();
      }
   };

   void write_die(int indent,
                  std::vector<char>& abbrev_data,
                  std::vector<char>& info_data,
                  std::map<die_pattern, uint32_t>& codes,
                  const die_pattern& die)
   {
      auto it = codes.find(die);
      if (it == codes.end())
      {
         it = codes.insert(std::pair{die, codes.size() + 1}).first;
         eosio::vector_stream s{abbrev_data};
         eosio::varuint32_to_bin(it->second, s);
         eosio::varuint32_to_bin(die.tag, s);
         eosio::to_bin(die.has_children, s);
         for (const auto& attr : die.attrs)
         {
            eosio::varuint32_to_bin(attr.attr, s);
            eosio::varuint32_to_bin(attr.form, s);
         }
         eosio::varuint32_to_bin(0, s);
         eosio::varuint32_to_bin(0, s);
      }

      eosio::vector_stream s{info_data};
      eosio::varuint32_to_bin(it->second, s);  // code
      if (show_generated_dies)
         fprintf(stderr, "%*s%s\n", indent, "", dw_tag_to_str(die.tag).c_str());

      for (const auto& attr : die.attrs)
      {
         if (show_generated_dies)
            fprintf(stderr, "%*s%s %s\n", indent + 2, "", dw_at_to_str(attr.attr).c_str(),
                    dw_form_to_str(attr.form).c_str());
         std::visit(overloaded{
                        [&](uint8_t v) { eosio::to_bin(v, s); },
                        [&](uint64_t v) { eosio::to_bin(v, s); },
                        [&](const std::string& str) { write_string(str, s); },
                    },
                    attr.value);
      }
   }  // write_die

   struct sub_ref
   {
      uint64_t stream_pos;
      uint64_t subprogram;
   };

   void write_subprograms(uint16_t code_section,
                          std::vector<char>& strings,
                          std::vector<char>& abbrev_data,
                          std::vector<char>& info_data,
                          std::vector<char>& symbol_data,
                          const info& info,
                          const std::vector<jit_fn_loc>& fn_locs,
                          const std::vector<jit_instr_loc>& instr_locs,
                          const void* code_start,
                          size_t code_size)
   {
      std::map<die_pattern, uint32_t> codes;
      die_pattern die;

      eosio::vector_stream info_s{info_data};
      auto begin_pos = info_data.size();
      eosio::to_bin(uint32_t(0xffff'ffff), info_s);
      auto length_pos = info_data.size();
      eosio::to_bin(uint64_t(0), info_s);
      auto inner_pos = info_data.size();

      eosio::to_bin(uint16_t(compile_unit_version), info_s);
      eosio::to_bin(uint64_t(0), info_s);  // debug_abbrev_offset
      eosio::to_bin(uint8_t(8), info_s);   // address_size

      die.tag = dw_tag_compile_unit;
      die.has_children = true;
      die.attrs = {
          {dw_at_language, dw_form_data8, uint64_t(dw_lang_c_plus_plus)},
          {dw_at_low_pc, dw_form_addr, uint64_t(code_start)},
          {dw_at_high_pc, dw_form_addr, uint64_t((char*)code_start + code_size)},
          {dw_at_stmt_list, dw_form_sec_offset, uint64_t(0)},
      };
      write_die(0, abbrev_data, info_data, codes, die);

      Elf64_Sym null_sym;
      memset(&null_sym, 0, sizeof(null_sym));
      symbol_data.insert(symbol_data.end(), (char*)(&null_sym), (char*)(&null_sym + 1));

      std::vector<size_t> sub_positions(info.subprograms.size());
      std::vector<sub_ref> sub_refs;

      for (size_t i = 0; i < info.subprograms.size(); ++i)
      {
         auto& sub = info.subprograms[i];
         sub_positions[i] = info_data.size();
         auto fn = get_wasm_fn(info, info.wasm_code_offset + sub.begin_address);
         if (!fn || info.wasm_code_offset + sub.end_address > info.wasm_fns[*fn].end_pos)
         {
            if (show_generated_dies)
               fprintf(stderr, "address lookup fail: %s %08x-%08x\n", sub.demangled_name.c_str(),
                       info.wasm_code_offset + sub.begin_address,
                       info.wasm_code_offset + sub.end_address);
            continue;
         }
         if (sub.parent)
            continue;
         auto fn_begin = uint64_t((const char*)code_start + fn_locs[*fn].code_prologue);
         auto fn_end = uint64_t((const char*)code_start + fn_locs[*fn].code_end);
         if (show_generated_dies)
            fprintf(stderr, "    DIE 0x%lx (%ld) subprogram %08x-%08x %016lx-%016lx %s\n",
                    uint64_t(info_data.size()), uint64_t(i),
                    info.wasm_code_offset + sub.begin_address,
                    info.wasm_code_offset + sub.end_address, fn_begin + print_addr_adj,
                    fn_end + print_addr_adj, sub.demangled_name.c_str());

         die.tag = dw_tag_subprogram;
         die.has_children = false;
         die.attrs = {
             {dw_at_low_pc, dw_form_addr, fn_begin},
             {dw_at_high_pc, dw_form_addr, fn_end},
         };
         if (sub.linkage_name)
            die.attrs.push_back({dw_at_linkage_name, dw_form_string, *sub.linkage_name});
         if (sub.name)
            die.attrs.push_back({dw_at_name, dw_form_string, *sub.name});
         else if (sub.linkage_name)
            die.attrs.push_back({dw_at_name, dw_form_string, sub.demangled_name});
         write_die(4, abbrev_data, info_data, codes, die);

         Elf64_Sym sym = {
             .st_name = 0,
             .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
             .st_other = STV_DEFAULT,
             .st_shndx = code_section,
             .st_value = fn_begin,
             .st_size = fn_end - fn_begin,
         };
         if (sub.linkage_name)
            sym.st_name = add_str(strings, sub.linkage_name->c_str());
         else if (sub.name)
            sym.st_name = add_str(strings, sub.name->c_str());
         symbol_data.insert(symbol_data.end(), (char*)(&sym), (char*)(&sym + 1));
      }  // for(sub)

      for (auto& r : sub_refs)
      {
         uint64_t subprogram_offset = sub_positions[r.subprogram] - begin_pos;
         memcpy(info_data.data() + r.stream_pos, &subprogram_offset, sizeof(subprogram_offset));
      }

      eosio::varuint32_to_bin(0, info_s);  // end children
      eosio::varuint32_to_bin(0, info_s);  // end module
      uint64_t inner_size = info_data.size() - inner_pos;
      memcpy(info_data.data() + length_pos, &inner_size, sizeof(inner_size));
   }  // write_subprograms

   struct wasm_header
   {
      uint32_t magic = 0;
      uint32_t version = 0;
   };
   EOSIO_REFLECT(wasm_header, magic, version)

   struct wasm_section
   {
      uint8_t id = 0;
      eosio::input_stream data;
   };
   EOSIO_REFLECT(wasm_section, id, data)

   eosio::input_stream wasm_exclude_custom(eosio::input_stream stream)
   {
      auto begin = stream.pos;
      wasm_header header;
      eosio::from_bin(header, stream);
      eosio::check(header.magic == eosio::vm::constants::magic,
                   "wasm file magic number does not match");
      eosio::check(header.version == eosio::vm::constants::version,
                   "wasm file version does not match");
      const char* found = nullptr;
      while (stream.remaining())
      {
         auto section_begin = stream.pos;
         auto section = eosio::from_bin<wasm_section>(stream);
         if (section.id == eosio::vm::section_id::custom_section)
         {
            if (!found)
               found = section_begin;
         }
         else
         {
            eosio::check(!found, "custom sections before non-custom sections not supported");
         }
      }
      if (found)
         return {begin, found};
      return {begin, stream.pos};
   }

   info get_info_from_wasm(eosio::input_stream stream)
   {
      info result;
      auto file_begin = stream.pos;
      std::map<std::string, uint32_t> files;

      wasm_header header;
      eosio::from_bin(header, stream);
      eosio::check(header.magic == eosio::vm::constants::magic,
                   "wasm file magic number does not match");
      eosio::check(header.version == eosio::vm::constants::version,
                   "wasm file version does not match");

      auto scan = [&](auto stream, auto f) {
         while (stream.remaining())
         {
            auto section_begin = stream.pos;
            auto section = eosio::from_bin<wasm_section>(stream);
            f(section_begin, section);
         }
      };

      auto scan_custom = [&](auto stream, auto f) {
         scan(stream, [&](auto section_begin, auto& section) {
            if (section.id == eosio::vm::section_id::custom_section)
               f(section, eosio::from_bin<std::string>(section.data));
         });
      };

      scan(stream, [&](auto section_begin, auto& section) {
         if (section.id == eosio::vm::section_id::code_section)
         {
            result.wasm_code_offset = section.data.pos - file_begin;
            auto s = section.data;
            auto count = eosio::varuint32_from_bin(s);
            result.wasm_fns.resize(count);
            for (uint32_t i = 0; i < count; ++i)
            {
               auto& fn = result.wasm_fns[i];
               fn.size_pos = s.pos - file_begin;
               auto size = eosio::varuint32_from_bin(s);
               fn.locals_pos = s.pos - file_begin;
               s.skip(size);
               fn.end_pos = s.pos - file_begin;
            }
         }
      });

      if (show_wasm_fn_info)
      {
         scan(stream, [&](auto section_begin, auto& section) {
            if (section.id != eosio::vm::section_id::code_section)
               return;
            eosio::input_stream s{section_begin, stream.end};
            fprintf(stderr, "%08x %08x: code section id\n", uint32_t(s.pos - file_begin),
                    uint32_t(s.pos - section_begin));
            auto id = eosio::from_bin<uint8_t>(s);
            fprintf(stderr, "         =%d\n", id);
            fprintf(stderr, "%08x %08x: section size\n", uint32_t(s.pos - file_begin),
                    uint32_t(s.pos - section_begin));
            auto size = eosio::varuint32_from_bin(s);
            fprintf(stderr, "         =%08x\n", size);
            s.end = s.pos + size;
            fprintf(stderr, "%08x %08x: count\n", uint32_t(s.pos - file_begin),
                    uint32_t(s.pos - section_begin));
            fprintf(stderr, "**** reset section_begin to here\n");
            section_begin = s.pos;
            fprintf(stderr, "%08x %08x: count\n", uint32_t(s.pos - file_begin),
                    uint32_t(s.pos - section_begin));
            auto count = eosio::varuint32_from_bin(s);
            fprintf(stderr, "         count=%08x\n\n", count);
            fprintf(stderr, "%08x %08x\n", uint32_t(s.pos - file_begin),
                    uint32_t(s.pos - section_begin));
            for (uint32_t i = 0; i < count; ++i)
            {
               fprintf(stderr, "[%04d] %08x %08x: function size\n", i, uint32_t(s.pos - file_begin),
                       uint32_t(s.pos - section_begin));
               auto size = eosio::varuint32_from_bin(s);
               fprintf(stderr, "[%04d] %08x %08x: function body\n", i, uint32_t(s.pos - file_begin),
                       uint32_t(s.pos - section_begin));
               s.skip(size);
               fprintf(stderr, "[%04d] %08x %08x: function end\n\n", i,
                       uint32_t(s.pos - file_begin), uint32_t(s.pos - section_begin));
            }
         });
      }

      scan_custom(stream, [&](auto& section, const auto& name) {
         if (name == ".debug_line")
         {
            dwarf::parse_debug_line(result, files, section.data);
         }
         else if (name == ".debug_abbrev")
         {
            dwarf::parse_debug_abbrev(result, files, section.data);
         }
         else if (name == ".debug_str")
         {
            result.strings = std::vector<char>{section.data.pos, section.data.end};
            eosio::check(result.strings.empty() || result.strings.back() == 0,
                         ".debug_str is malformed");
         }
      });

      scan_custom(stream, [&](auto& section, const auto& name) {
         if (name == ".debug_info")
            dwarf::parse_debug_info(result, section.data);
      });

      std::sort(result.locations.begin(), result.locations.end());
      std::sort(result.abbrev_decls.begin(), result.abbrev_decls.end());
      if (show_wasm_loc_summary)
         for (auto& loc : result.locations)
            fprintf(stderr, "loc  [%08x,%08x) %s:%d\n", loc.begin_address, loc.end_address,
                    result.files[loc.file_index].c_str(), loc.line);
      if (show_wasm_subp_summary)
         for (auto& p : result.subprograms)
            fprintf(stderr, "subp %d [%08x,%08x) size=%08x %6s %s\n",
                    int(&p - &result.subprograms[0]), p.begin_address, p.end_address,
                    p.end_address - p.begin_address, p.parent ? "inline" : "",
                    p.demangled_name.c_str());
      return result;
   }  // get_info_from_wasm

   const char* info::get_str(uint32_t offset) const
   {
      eosio::check(offset < strings.size(), "string out of range in .debug_str");
      return &strings[offset];
   }

   const location* info::get_location(uint32_t address) const
   {
      auto it = std::upper_bound(locations.begin(), locations.end(), address,
                                 [](auto a, const auto& b) { return a < b.begin_address; });
      if (it != locations.begin() && address < (--it)->end_address)
         return &*it;
      return nullptr;
   }

   const abbrev_decl* info::get_abbrev_decl(uint32_t table_offset, uint32_t code) const
   {
      auto key = std::pair{table_offset, code};
      auto it = std::lower_bound(abbrev_decls.begin(), abbrev_decls.end(), key,
                                 [](const auto& a, const auto& b) { return a.key() < b; });
      if (it != abbrev_decls.end() && it->key() == key)
         return &*it;
      return nullptr;
   }

   const subprogram* info::get_subprogram(uint32_t address) const
   {
      auto it = std::upper_bound(subprograms.begin(), subprograms.end(), address,
                                 [](auto a, const auto& b) { return a < b.begin_address; });
      if (it != subprograms.begin() && address < (--it)->end_address)
         return &*it;
      return nullptr;
   }

   enum jit_actions : uint32_t
   {
      jit_noaction = 0,
      jit_register_fn,
      jit_unregister_fn
   };

   struct jit_code_entry
   {
      jit_code_entry* next_entry = nullptr;
      jit_code_entry* prev_entry = nullptr;
      const char* symfile_addr = nullptr;
      uint64_t symfile_size = 0;
   };

   struct jit_descriptor
   {
      uint32_t version = 1;
      jit_actions action_flag = jit_noaction;
      jit_code_entry* relevant_entry = nullptr;
      jit_code_entry* first_entry = nullptr;
   };
}  // namespace dwarf

extern "C"
{
   void __attribute__((noinline)) __jit_debug_register_code() { asm(""); };
   dwarf::jit_descriptor __jit_debug_descriptor;
}

namespace dwarf
{
   struct debugger_registration
   {
      jit_code_entry desc;
      std::vector<char> symfile;

      ~debugger_registration()
      {
         if (desc.next_entry)
            desc.next_entry->prev_entry = desc.prev_entry;
         if (desc.prev_entry)
            desc.prev_entry->next_entry = desc.next_entry;
         if (__jit_debug_descriptor.first_entry == &desc)
            __jit_debug_descriptor.first_entry = desc.next_entry;
         __jit_debug_descriptor.action_flag = jit_unregister_fn;
         __jit_debug_descriptor.relevant_entry = &desc;
         __jit_debug_register_code();
      }

      void reg()
      {
         desc.symfile_addr = symfile.data();
         desc.symfile_size = symfile.size();
         if (__jit_debug_descriptor.first_entry)
         {
            __jit_debug_descriptor.first_entry->prev_entry = &desc;
            desc.next_entry = __jit_debug_descriptor.first_entry;
         }
         __jit_debug_descriptor.action_flag = jit_register_fn;
         __jit_debug_descriptor.first_entry = &desc;
         __jit_debug_descriptor.relevant_entry = &desc;
         __jit_debug_register_code();
      }

      template <typename T>
      size_t write(const T& x)
      {
         auto result = symfile.size();
         symfile.insert(symfile.end(), (const char*)(&x), (const char*)(&x + 1));
         return result;
      }

      template <typename T>
      void write(size_t pos, const T& x)
      {
         memcpy(symfile.data() + pos, (const char*)(&x), sizeof(x));
      }

      size_t append(const std::vector<char>& v)
      {
         auto result = symfile.size();
         symfile.insert(symfile.end(), v.begin(), v.end());
         return result;
      }
   };

   std::shared_ptr<debugger_registration> register_with_debugger(  //
       info& info,
       const std::vector<jit_fn_loc>& fn_locs,
       const std::vector<jit_instr_loc>& instr_locs,
       const void* code_start,
       size_t code_size,
       const void* entry)
   {
      eosio::check(fn_locs.size() == info.wasm_fns.size(), "number of functions doesn't match");

      auto show_fn = [&](size_t fn) {
         if (show_fn_locs && fn < fn_locs.size())
         {
            auto& w = info.wasm_fns[fn];
            auto& l = fn_locs[fn];
            fprintf(stderr,
                    "fn %5ld: %016lx %016lx %016lx %016lx whole:%08x-%08x instr:%08x-%08x\n", fn,
                    (long)code_start + l.code_prologue, (long)code_start + l.code_body,  //
                    (long)code_start + l.code_epilogue, (long)code_start + l.code_end,   //
                    w.size_pos, w.end_pos, l.wasm_begin, l.wasm_end);
         }
      };
      auto show_instr = [&](const auto it) {
         if (show_instr_locs && it != instr_locs.end())
            fprintf(stderr, "          %016lx %08x\n", (long)code_start + it->code_offset,
                    it->wasm_addr);
      };

      if (show_fn_locs || show_instr_locs)
      {
         size_t fn = 0;
         auto instr = instr_locs.begin();
         show_fn(fn);
         while (instr != instr_locs.end())
         {
            while (fn < fn_locs.size() && instr->code_offset >= fn_locs[fn].code_end)
               show_fn(++fn);
            show_instr(instr);
            ++instr;
         }
         while (fn < fn_locs.size())
            show_fn(++fn);
      }

      auto result = std::make_shared<debugger_registration>();
      std::vector<char> strings;
      strings.push_back(0);

      constexpr uint16_t num_sections = 7;
      constexpr uint16_t strtab_section = 1;
      constexpr uint16_t code_section = 2;
      Elf64_Ehdr elf_header{
          .e_ident = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, ELFCLASS64, ELFDATA2LSB, EV_CURRENT,
                      ELFOSABI_LINUX, 0},
          .e_type = ET_EXEC,
          .e_machine = EM_X86_64,
          .e_version = EV_CURRENT,
          .e_entry = Elf64_Addr(entry),
          .e_phoff = 0,
          .e_shoff = 0,
          .e_flags = 0,
          .e_ehsize = sizeof(elf_header),
          .e_phentsize = sizeof(Elf64_Phdr),
          .e_phnum = 1,
          .e_shentsize = sizeof(Elf64_Shdr),
          .e_shnum = num_sections,
          .e_shstrndx = strtab_section,
      };
      auto elf_header_pos = result->write(elf_header);

      elf_header.e_phoff = result->symfile.size();
      Elf64_Phdr program_header{
          .p_type = PT_LOAD,
          .p_flags = PF_X | PF_R,
          .p_offset = 0,
          .p_vaddr = (Elf64_Addr)code_start,
          .p_paddr = 0,
          .p_filesz = 0,
          .p_memsz = code_size,
          .p_align = 0,
      };
      auto program_header_pos = result->write(program_header);

      elf_header.e_shoff = result->symfile.size();
      auto sec_header = [&](const char* name, Elf64_Word type, Elf64_Xword flags) {
         Elf64_Shdr header{
             .sh_name = add_str(strings, name),
             .sh_type = type,
             .sh_flags = flags,
             .sh_addr = 0,
             .sh_offset = 0,
             .sh_size = 0,
             .sh_link = 0,
             .sh_info = 0,
             .sh_addralign = 0,
             .sh_entsize = 0,
         };
         auto pos = result->write(header);
         return std::pair{header, pos};
      };
      auto [reserved_sec_header, reserved_sec_header_pos] = sec_header(0, 0, 0);
      auto [str_sec_header, str_sec_header_pos] = sec_header(".shstrtab", SHT_STRTAB, 0);
      auto [code_sec_header, code_sec_header_pos] =
          sec_header(".text", SHT_NOBITS, SHF_ALLOC | SHF_EXECINSTR);
      auto [line_sec_header, line_sec_header_pos] = sec_header(".debug_line", SHT_PROGBITS, 0);
      auto [abbrev_sec_header, abbrev_sec_header_pos] =
          sec_header(".debug_abbrev", SHT_PROGBITS, 0);
      auto [info_sec_header, info_sec_header_pos] = sec_header(".debug_info", SHT_PROGBITS, 0);
      auto [symbol_sec_header, symbol_sec_header_pos] = sec_header(".symtab", SHT_SYMTAB, 0);

      code_sec_header.sh_addr = Elf64_Addr(code_start);
      code_sec_header.sh_size = code_size;
      result->write(code_sec_header_pos, code_sec_header);

      auto write_sec = [&](auto& header, auto pos, const auto& data) {
         header.sh_offset = result->append(data);
         header.sh_size = data.size();
         result->write(pos, header);
      };

      std::vector<char> abbrev_data;
      std::vector<char> info_data;
      std::vector<char> symbol_data;
      symbol_sec_header.sh_link = strtab_section;
      symbol_sec_header.sh_entsize = sizeof(Elf64_Sym);
      write_subprograms(code_section, strings, abbrev_data, info_data, symbol_data, info, fn_locs,
                        instr_locs, code_start, code_size);

      write_sec(line_sec_header, line_sec_header_pos,
                generate_debug_line(info, fn_locs, instr_locs, code_start));
      write_sec(abbrev_sec_header, abbrev_sec_header_pos, abbrev_data);
      write_sec(info_sec_header, info_sec_header_pos, info_data);
      write_sec(symbol_sec_header, symbol_sec_header_pos, symbol_data);
      write_sec(str_sec_header, str_sec_header_pos, strings);
      result->write(elf_header_pos, elf_header);

      result->reg();
      return result;
   }

}  // namespace dwarf
