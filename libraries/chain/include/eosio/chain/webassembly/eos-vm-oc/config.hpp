#pragma once

#include <istream>
#include <ostream>
#include <vector>
#include <string>

#include <fc/io/raw.hpp>

#include <sys/resource.h>

namespace eosio { namespace chain { namespace eosvmoc {

struct config {
   uint64_t cache_size = 1024u*1024u*1024u;
   uint64_t threads    = 1u;

   // subjective limits for OC compilation.
   // nodeos enforces the limits by the default values.
   // libtester disables the limits in all tests, except enforces the limits
   // in the tests in unittests/eosvmoc_limits_tests.cpp.
   std::optional<rlim_t>   cpu_limit {20u};
   std::optional<rlim_t>   vm_limit  {512u*1024u*1024u};
   std::optional<uint64_t> stack_size_limit {16u*1024u};
   std::optional<size_t>   generated_code_size_limit {16u*1024u*1024u};
};

//work around unexpected std::optional behavior
template <typename DS>
inline DS& operator>>(DS& ds, eosio::chain::eosvmoc::config& cfg) {
   fc::raw::pack(ds, cfg.cache_size);
   fc::raw::pack(ds, cfg.threads);

   auto better_optional_unpack = [&]<typename T>(std::optional<T>& t) {
      bool b; fc::raw::unpack( ds, b );
      if(b) { t = T(); fc::raw::unpack( ds, *t ); }
      else { t.reset(); }
   };
   better_optional_unpack(cfg.cpu_limit);
   better_optional_unpack(cfg.vm_limit);
   better_optional_unpack(cfg.stack_size_limit);
   better_optional_unpack(cfg.generated_code_size_limit);

   return ds;
}

template <typename DS>
inline DS& operator<<(DS& ds, const eosio::chain::eosvmoc::config& cfg) {
   fc::raw::pack(ds, cfg.cache_size);
   fc::raw::pack(ds, cfg.threads);
   fc::raw::pack(ds, cfg.cpu_limit);
   fc::raw::pack(ds, cfg.vm_limit);
   fc::raw::pack(ds, cfg.stack_size_limit);
   fc::raw::pack(ds, cfg.generated_code_size_limit);
   return ds;
}

}}}
