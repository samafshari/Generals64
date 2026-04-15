// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// Derived from EA's GPL v3 Zero Hour source release.

// Minimal C++ language compat macros used throughout the engine.
// Our build always targets /std:c++20 on MSVC, so all of these are just
// passthroughs to the modern language feature.

#pragma once

#include <utility>

// MSVC spells the rdtsc intrinsic `__rdtsc` (two underscores).  Some legacy
// code imported from the mingw compat shim used `_rdtsc` (single underscore).
// Provide a macro alias so both spellings work.
#if defined(_MSC_VER)
    #include <intrin.h>
    #ifndef _rdtsc
        #define _rdtsc() __rdtsc()
    #endif
#endif

namespace stl
{
    // Our toolchain is always C++11+, so this just forwards to std::move.
    template <typename T>
    inline void move_or_swap(T& dst, T& src) { dst = std::move(src); }
}

#ifndef CPP_11
#define CPP_11(code) code
#endif

#ifndef FALLTHROUGH
#define FALLTHROUGH [[fallthrough]]
#endif

#ifndef REGISTER
#define REGISTER
#endif

#ifndef IUNKNOWN_NOEXCEPT
#define IUNKNOWN_NOEXCEPT noexcept
#endif
