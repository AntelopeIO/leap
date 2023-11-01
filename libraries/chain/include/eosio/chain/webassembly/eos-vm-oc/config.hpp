#pragma once

#include <istream>
#include <ostream>
#include <vector>
#include <string>

#include <fc/reflect/reflect.hpp>

#include <sys/resource.h>

namespace eosio { namespace chain { namespace eosvmoc {

struct config {
   uint64_t cache_size = 1024u*1024u*1024u;
   uint64_t threads    = 1u;

   // subjective limits for OC compilation.
   // nodeos enforces the limits by the default values.
   // libtester disables the limits in all tests, except enforces the limits
   // in the tests in unittests/eosvmoc_limits_tests.cpp.
   rlimit   cpu_limits                {20u, 20u};
   rlimit   vm_limits                 {512u*1024u*1024u, 512u*1024u*1024u};
   uint64_t stack_size_limit          {16u*1024u};
   size_t   generated_code_size_limit {16u*1024u*1024u};
};

}}}

FC_REFLECT(rlimit, (rlim_cur)(rlim_max))
FC_REFLECT(eosio::chain::eosvmoc::config, (cache_size)(threads)(cpu_limits)(vm_limits)(stack_size_limit)(generated_code_size_limit))
