// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#include "PreRTS.h"
#include "Common/FrameRateLimit.h"


FrameRateLimit::FrameRateLimit()
{
	m_start = std::chrono::steady_clock::now();
}

Real FrameRateLimit::wait(UnsignedInt maxFps)
{
	auto now = std::chrono::steady_clock::now();
	double elapsedSeconds = std::chrono::duration<double>(now - m_start).count();
	const double targetSeconds = 1.0 / maxFps;
	const double sleepSeconds = targetSeconds - elapsedSeconds - 0.0005; // leave ~0.5ms for spin wait

	if (sleepSeconds > 0.0)
	{
		std::this_thread::sleep_for(std::chrono::duration<double>(sleepSeconds));
	}

	// Spin wait for remaining sub-ms time
	do
	{
		std::this_thread::yield();
		now = std::chrono::steady_clock::now();
		elapsedSeconds = std::chrono::duration<double>(now - m_start).count();
	}
	while (elapsedSeconds < targetSeconds);

	m_start = now;
	return (Real)elapsedSeconds;
}


const UnsignedInt RenderFpsPreset::s_fpsValues[] = {
	30, 50, 56, 60, 65, 70, 72, 75, 80, 85, 90, 100, 110, 120, 144, 240, 480, UncappedFpsValue };

static_assert(LOGICFRAMES_PER_SECOND <= 30, "Min FPS values need to be revisited!");

UnsignedInt RenderFpsPreset::getNextFpsValue(UnsignedInt value)
{
	const Int first = 0;
	const Int last = ARRAY_SIZE(s_fpsValues) - 1;
	for (Int i = first; i < last; ++i)
	{
		if (value >= s_fpsValues[i] && value < s_fpsValues[i + 1])
		{
			return s_fpsValues[i + 1];
		}
	}
	return s_fpsValues[last];
}

UnsignedInt RenderFpsPreset::getPrevFpsValue(UnsignedInt value)
{
	const Int first = 0;
	const Int last = ARRAY_SIZE(s_fpsValues) - 1;
	for (Int i = last; i > first; --i)
	{
		if (value <= s_fpsValues[i] && value > s_fpsValues[i - 1])
		{
			return s_fpsValues[i - 1];
		}
	}
	return s_fpsValues[first];
}

UnsignedInt RenderFpsPreset::changeFpsValue(UnsignedInt value, FpsValueChange change)
{
	switch (change)
	{
	default:
	case FpsValueChange_Increase: return getNextFpsValue(value);
	case FpsValueChange_Decrease: return getPrevFpsValue(value);
	}
}


UnsignedInt LogicTimeScaleFpsPreset::getNextFpsValue(UnsignedInt value)
{
	return value + StepFpsValue;
}

UnsignedInt LogicTimeScaleFpsPreset::getPrevFpsValue(UnsignedInt value)
{
	if (value - StepFpsValue < MinFpsValue)
	{
		return MinFpsValue;
	}
	else
	{
		return value - StepFpsValue;
	}
}

UnsignedInt LogicTimeScaleFpsPreset::changeFpsValue(UnsignedInt value, FpsValueChange change)
{
	switch (change)
	{
	default:
	case FpsValueChange_Increase: return getNextFpsValue(value);
	case FpsValueChange_Decrease: return getPrevFpsValue(value);
	}
}
