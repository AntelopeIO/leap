#pragma once

#include "Inline/BasicTypes.h"
#include "IR/Module.h"

#include <vector>
#include <map>

namespace eosio { namespace chain { namespace eosvmoc {

struct instantiated_code {
   std::vector<uint8_t> code;
   std::map<unsigned, uintptr_t> function_offsets;
   uintptr_t table_offset;
};

namespace LLVMJIT {
   instantiated_code instantiateModule(const IR::Module& module, uint64_t stack_size_limit, size_t generated_code_size_limit);
}
}}}
