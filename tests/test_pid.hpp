// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Portable process id for unique per-run scratch directories in tests.
// POSIX getpid() lives in <unistd.h>; MSVC spells it _getpid() in <process.h>.
#pragma once

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace olduvai_test {
inline int pid() {
#ifdef _WIN32
    return _getpid();
#else
    return ::getpid();
#endif
}
}  // namespace olduvai_test
