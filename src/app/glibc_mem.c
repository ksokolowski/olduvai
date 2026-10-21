/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Krzysztof Sokołowski */
/* zig cross builds only (CMakeLists.txt).  zig links its compiler runtime as
   one object whenever the program needs a helper from it, and that object's
   weak memset/memcpy/memmove (byte loops) then win over glibc's at link time.
   These strong definitions call glibc's own, bound by symbol version: a
   versioned reference cannot resolve to the runtime's unversioned copies.
   Built with -fno-builtin, so the compiler does not turn a body back into a
   call to the function it defines. */
#include <stddef.h>

#if defined(__aarch64__)
#define OLDUVAI_GLIBC_BASE "GLIBC_2.17"
#elif defined(__arm__)
#define OLDUVAI_GLIBC_BASE "GLIBC_2.4"
#else
#error "glibc_mem.c: add this architecture's glibc base version"
#endif

void* olduvai_glibc_memset(void* d, int c, size_t n);
void* olduvai_glibc_memcpy(void* d, const void* s, size_t n);
void* olduvai_glibc_memmove(void* d, const void* s, size_t n);
__asm__(".symver olduvai_glibc_memset, memset@" OLDUVAI_GLIBC_BASE);
__asm__(".symver olduvai_glibc_memcpy, memcpy@" OLDUVAI_GLIBC_BASE);
__asm__(".symver olduvai_glibc_memmove, memmove@" OLDUVAI_GLIBC_BASE);

/* Hidden: our own code links to these, but they must never be exported.  An
   exported memset lets the dynamic loader bind the versioned call above back
   to this very function (the aarch64 LTO build hung so, a jump to itself). */
#define OLDUVAI_HIDDEN __attribute__((visibility("hidden")))
OLDUVAI_HIDDEN void* memset(void* d, int c, size_t n) {
    return olduvai_glibc_memset(d, c, n);
}
OLDUVAI_HIDDEN void* memcpy(void* d, const void* s, size_t n) {
    return olduvai_glibc_memcpy(d, s, n);
}
OLDUVAI_HIDDEN void* memmove(void* d, const void* s, size_t n) {
    return olduvai_glibc_memmove(d, s, n);
}
