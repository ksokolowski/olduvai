// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Portable environment-variable set/unset for tests.
//
// POSIX has setenv/unsetenv; Windows (MinGW/MSVC, UCRT) only has
// _putenv_s, where an empty value removes the variable.  Tests use these
// helpers so the env save/restore RAII wrappers compile everywhere.
#pragma once

#include <cstdlib>

namespace olduvai::test {

inline void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

inline void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

}  // namespace olduvai::test
