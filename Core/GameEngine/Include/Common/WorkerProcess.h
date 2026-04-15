// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

// Helper class that allows you to start a worker process and retrieve its exit code
// and console output as a string.
// It also makes sure that the started process is killed in case our process exits in any way.
class WorkerProcess
{
public:
	WorkerProcess();

	bool startProcess(UnicodeString command);

	void update();

	bool isRunning() const;

	// returns true iff the process exited.
	bool isDone() const;

	DWORD getExitCode() const;
	AsciiString getStdOutput() const;

	// Terminate Process if it's running
	void kill();

private:
	// returns true if all output has been received
	// returns false if the worker is still running
	bool fetchStdOutput();

private:
	HANDLE m_processHandle;
	HANDLE m_readHandle;
	HANDLE m_jobHandle;
	AsciiString m_stdOutput;
	DWORD m_exitcode;
	bool m_isDone;
};
