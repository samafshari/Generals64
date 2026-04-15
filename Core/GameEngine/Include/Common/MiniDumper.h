// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

#ifdef RTS_ENABLE_CRASHDUMP
#include "DbgHelpLoader.h"

enum DumpType : Char
{
	// Smallest dump type with call stacks and some supporting variables
	DumpType_Minimal = 'M',
	// Largest dump size including complete memory contents of the process
	DumpType_Full = 'F',
};

class MiniDumper
{
	enum MiniDumperExitCode : Int
	{
		MiniDumperExitCode_Success = 0x0,
		MiniDumperExitCode_FailureWait = 0x37DA1040,
		MiniDumperExitCode_FailureParam = 0x4EA527BB,
		MiniDumperExitCode_ForcedTerminate = 0x158B1154,
	};

public:
	MiniDumper();
	Bool IsInitialized() const;
	void TriggerMiniDump(DumpType dumpType);
	void TriggerMiniDumpForException(_EXCEPTION_POINTERS* e_info, DumpType dumpType);
	static void initMiniDumper(const AsciiString& userDirPath);
	static void shutdownMiniDumper();
	static LONG WINAPI DumpingExceptionFilter(_EXCEPTION_POINTERS* e_info);

private:
	void Initialize(const AsciiString& userDirPath);
	void ShutDown();
	void CreateMiniDump(DumpType dumpType);
	void CleanupResources();
	Bool IsDumpThreadStillRunning() const;
	void ShutdownDumpThread();

	// Thread procs
	static DWORD WINAPI MiniDumpThreadProc(LPVOID lpParam);
	DWORD ThreadProcInternal();

	// Dump file directory bookkeeping
	Bool InitializeDumpDirectory(const AsciiString& userDirPath);
	static void KeepNewestFiles(const std::string& directory, const DumpType dumpType, const Int keepCount);

	// Struct to hold file information
	struct FileInfo
	{
		std::string name;
		FILETIME lastWriteTime;
	};

	static bool CompareByLastWriteTime(const FileInfo& a, const FileInfo& b);

private:
	Bool m_miniDumpInitialized;
	Bool m_loadedDbgHelp;
	DumpType m_requestedDumpType;

	// Path buffers
	Char m_dumpDir[MAX_PATH];
	Char m_dumpFile[MAX_PATH];
	WideChar m_executablePath[MAX_PATH];

	// Event handles
	HANDLE m_dumpRequested;
	HANDLE m_dumpComplete;
	HANDLE m_quitting;

	// Thread handles
	HANDLE m_dumpThread;
	DWORD m_dumpThreadId;
};

extern MiniDumper* TheMiniDumper;
#endif
