#pragma once

#include "Inline/BasicTypes.h"
#include "IR/Module.h"

#include "llvm/IR/Module.h"

#include <vector>
#include <map>

namespace eosio { namespace chain { namespace eosvmoc {

namespace LLVMJIT {
   bool getFunctionIndexFromExternalName(const char* externalName,Uptr& outFunctionDefIndex);
   const char* getTableSymbolName();
   llvm::Module* emitModule(const IR::Module& module);
}
}}}
