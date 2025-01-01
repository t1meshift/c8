#pragma once

/**
 * I just found out that C23 support in MSVC is... well, worse than in Clang.
 * So this header is a small (I hope so) code compatibility layer for inferior
 * compilers.
 */

// MSVC fixes
// Bump the version below if it does not compile on newer versions
// last checked on MSVC v17.12.3
#if (defined(_MSC_VER) && _MSC_VER <= 1942)

// MSVC does not have embedded bool type yet
#include <stdbool.h>
// MSVC does not support `enum A : uint8_t{}` syntax yet
#define C23_COMPAT_NO_ENUM_TYPES
// MSVC does not have nullptr_t yet
#define nullptr NULL

#endif
