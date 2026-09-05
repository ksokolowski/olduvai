// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Portable setenv/unsetenv for the test tree.
//
// MSVC has no POSIX `setenv`/`unsetenv` — it offers `_putenv_s` instead — so
// tests calling them compiled everywhere except the one platform no developer
// here builds on.  test_env_num.cpp and test_rom_dirs.cpp both did, and both
// landed AFTER v0.9.5, so no release had ever compiled them under `cl`: the
// 0.9.6 pre-tag dry run is what surfaced it, as
//   error C3861: 'setenv': identifier not found
//
// One header rather than a shim per file, because two copies of one policy is
// how the tally drift started (BACKLOG §3.14a).  Sibling of test_pid.hpp,
// which does the same job for getpid() and set the olduvai_test convention.
//
// `_putenv_s(k, "")` is the documented Windows way to REMOVE a variable, so
// getenv() returns null afterwards exactly as it does after POSIX unsetenv —
// the semantics the callers depend on.
#pragma once

#include <cstdlib>

namespace olduvai_test {

inline void set_env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

inline void unset_env(const char* key) {
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

}  // namespace olduvai_test
