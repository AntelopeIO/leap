#pragma once

#include <chainbase/environment.hpp>

// reflect chainbase::environment for --print-build-info option
FC_REFLECT_ENUM(chainbase::environment::os_t,
                (OS_LINUX) (OS_MACOS) (OS_WINDOWS) (OS_OTHER))
FC_REFLECT_ENUM(chainbase::environment::arch_t,
                (ARCH_X86_64) (ARCH_ARM) (ARCH_RISCV) (ARCH_OTHER))

namespace fc {

void to_variant(const chainbase::environment& bi, variant& v) {
   // the variant conversion ultimately binds a reference to each member, but chainbase::environment is packed making
   // a reference to an unaligned variable UB. The boost_version is the only offender
   unsigned aligned_boost_version = bi.boost_version;
   v = fc::mutable_variant_object()("debug", bi.debug)
                                   ("os", bi.os)
                                   ("arch", bi.arch)
                                   ("boost_version", aligned_boost_version)
                                   ("compiler", bi.compiler);

}

}