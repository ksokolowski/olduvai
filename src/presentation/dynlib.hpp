// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Minimal cross-platform dynamic-library loader for the OPTIONAL runtime
// audio backends (libmt32emu, libfluidsynth) — loaded by name if the user
// has them, absent otherwise.  POSIX and MinGW (with dlfcn-win32) use
// <dlfcn.h>; MSVC has no dlopen, so it uses the Win32 loader directly.
// The two spellings behave identically for our use: open-by-name, resolve
// a symbol, close.

#pragma once

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX          // keep windows.h from defining min/max macros that
#define NOMINMAX          // would clobber std::min/std::max across the TU
#endif
#include <windows.h>

namespace olduvai::presentation {
inline void* dyn_open(const char* name) {
    return reinterpret_cast<void*>(::LoadLibraryA(name));
}
inline void* dyn_sym(void* handle, const char* symbol) {
    return reinterpret_cast<void*>(
        ::GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol));
}
inline void dyn_close(void* handle) {
    if (handle != nullptr) ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
}
}  // namespace olduvai::presentation

#else  // POSIX + MinGW (dlfcn-win32)

#include <dlfcn.h>

namespace olduvai::presentation {
inline void* dyn_open(const char* name) { return ::dlopen(name, RTLD_NOW); }
inline void* dyn_sym(void* handle, const char* symbol) {
    return ::dlsym(handle, symbol);
}
inline void dyn_close(void* handle) {
    if (handle != nullptr) ::dlclose(handle);
}
}  // namespace olduvai::presentation

#endif
