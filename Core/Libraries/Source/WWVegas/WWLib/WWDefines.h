// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

// Raw animation interpolation is disabled by default to match retail behavior.
// Some assets rely on discrete raw keyframes, and forcing interpolation can
// produce invalid transforms during synced animation playback.
// @todo Implement a new flag per animation file to opt-in when appropriate.
#ifndef WW3D_ENABLE_RAW_ANIM_INTERPOLATION
#define WW3D_ENABLE_RAW_ANIM_INTERPOLATION (0)
#endif
