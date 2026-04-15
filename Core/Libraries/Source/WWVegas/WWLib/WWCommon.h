// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

#include "refcount.h"
#include "STLUtils.h"
#include "stringex.h"
#include <stdio.h>


// This macro serves as a general way to determine the number of elements within an array.
#ifndef ARRAY_SIZE
#if defined(_MSC_VER) && _MSC_VER < 1300
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#else
template <typename Type, size_t Size> char (*ArraySizeHelper(Type(&)[Size]))[Size];
#define ARRAY_SIZE(arr) sizeof(*ArraySizeHelper(arr))
#endif
#endif // ARRAY_SIZE

// Changing this will require tweaking all Drawable code that concerns the ww3d time step, including locomotion physics.
// IMPORTANT: campaign and shell maps require this baseline rate. The
// per-game-mode logic-rate override lives in GameLogic::startNewGame —
// skirmish and multiplayer set TheFramePacer->setLogicTimeScaleFps to a
// higher value for smoother gameplay, but the constant must stay at 30 so
// the campaign / shell map timing doesn't break.
static constexpr int WWSyncPerSecond = 30;
static constexpr int WWSyncMilliseconds = 1000 / WWSyncPerSecond;

#if defined(_MSC_VER) && _MSC_VER < 1300
typedef unsigned MemValueType;
typedef long Interlocked32; // To use with Interlocked functions
#else
typedef unsigned long long MemValueType;
typedef volatile long Interlocked32; // To use with Interlocked functions
#endif
