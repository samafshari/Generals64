// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

#include "Common/GameCommon.h"
#include <chrono>
#include <thread>


class FrameRateLimit
{
public:
	FrameRateLimit();

	Real wait(UnsignedInt maxFps);

private:
	std::chrono::steady_clock::time_point m_start;
};


enum FpsValueChange
{
	FpsValueChange_Increase,
	FpsValueChange_Decrease,
};


class RenderFpsPreset
{
public:
	enum : UnsignedInt
	{
		UncappedFpsValue = 1000000,
	};

	static UnsignedInt getNextFpsValue(UnsignedInt value);
	static UnsignedInt getPrevFpsValue(UnsignedInt value);
	static UnsignedInt changeFpsValue(UnsignedInt value, FpsValueChange change);

private:
	static const UnsignedInt s_fpsValues[];
};


class LogicTimeScaleFpsPreset
{
public:
	enum : UnsignedInt
	{
#if RTS_DEBUG
		MinFpsValue = 5,
#else
		MinFpsValue = LOGICFRAMES_PER_SECOND,
#endif
		StepFpsValue = 5,
	};

	static UnsignedInt getNextFpsValue(UnsignedInt value);
	static UnsignedInt getPrevFpsValue(UnsignedInt value);
	static UnsignedInt changeFpsValue(UnsignedInt value, FpsValueChange change);
};

