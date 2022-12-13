#pragma once

#define ENABLE_MODULE_RECOVERY 1

#define ECMULT_GEN_PREC_BITS 4
#define ECMULT_WINDOW_SIZE 15

//enable asm
#ifdef __x86_64__
  #define USE_ASM_X86_64 1
#endif
