// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

class ReplaySimulation
{
public:

	// Simulate a list of replays without graphics.
	// Returns exit code 1 if mismatch or other error occurred
	// Returns exit code 0 if all replays were successfully simulated without mismatches
	static int simulateReplays(const std::vector<AsciiString> &filenames, int maxProcesses);

	static void stop() { s_isRunning = false; }

	static Bool isRunning() { return s_isRunning; }
	static UnsignedInt getCurrentReplayIndex() { return s_replayIndex; }
	static UnsignedInt getReplayCount() { return s_replayCount; }

private:

	static int simulateReplaysInThisProcess(const std::vector<AsciiString> &filenames);
	static int simulateReplaysInWorkerProcesses(const std::vector<AsciiString> &filenames, int maxProcesses);
	static std::vector<AsciiString> resolveFilenameWildcards(const std::vector<AsciiString> &filenames);

private:

	static Bool s_isRunning;
	static UnsignedInt s_replayIndex;
	static UnsignedInt s_replayCount;
};
