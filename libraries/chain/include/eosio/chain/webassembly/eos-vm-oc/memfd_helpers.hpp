#pragma once

#include <linux/memfd.h>
#include <sys/mman.h>

namespace eosio::chain::eosvmoc {

// added in glibc 2.38
#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 8U
#endif

inline int exec_sealed_memfd_create(const char* name) {
   //kernels 6.3 through 6.6 by default warn when neither MFD_NOEXEC_SEAL nor MFD_EXEC are passed; optionally 6.3+
   // may enforce MFD_NOEXEC_SEAL. Prior to 6.3 these flags will EINVAL.
   if(int ret = memfd_create(name, MFD_CLOEXEC | MFD_NOEXEC_SEAL); ret >= 0 || errno != EINVAL)
      return ret;
   return memfd_create(name, MFD_CLOEXEC);
}

}
