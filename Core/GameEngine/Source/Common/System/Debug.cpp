/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Debug.cpp
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Debug.cpp
//
// Created:   Steven Johnson, August 2001
//
// Desc:      Debug logging and other debug utilities
//
// ----------------------------------------------------------------------------

// SYSTEM INCLUDES
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


// USER INCLUDES

// Uncomment this to show normal logging stuff in the crc logging.
// This can be helpful for context, but can also clutter diffs because normal logs aren't necessarily
// deterministic or the same on all peers in multiplayer games.
//#define INCLUDE_DEBUG_LOG_IN_CRC_LOG

#define DEBUG_THREADSAFE
#ifdef DEBUG_THREADSAFE
#include "Common/CriticalSection.h"
#endif
#include "Common/CommandLine.h"
#include "Common/Debug.h"
#include "Common/CRCDebug.h"
#include "Common/SystemInfo.h"
#include "Common/UnicodeString.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/GameText.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#if defined(DEBUG_STACKTRACE) || defined(IG_DEBUG_STACKTRACE)
	#include "Common/StackDump.h"
#endif
#ifdef RTS_ENABLE_CRASHDUMP
#include "Common/MiniDumper.h"
#endif

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
extern HWND ApplicationHWnd;

extern const char *gAppPrefix; /// So WB can have a different log file name.


// ----------------------------------------------------------------------------
// DEFINES
// ----------------------------------------------------------------------------

#ifdef DEBUG_LOGGING

#if defined(RTS_DEBUG)
	#define DEBUG_FILE_NAME				"DebugLogFileD"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrevD"
#else
	#define DEBUG_FILE_NAME				"DebugLogFile"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrev"
#endif

#endif

// ----------------------------------------------------------------------------
// PRIVATE TYPES
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// PRIVATE DATA
// ----------------------------------------------------------------------------
// because DebugInit can be called during static module initialization before the main function is called.
#ifdef DEBUG_LOGGING
static FILE *theLogFile = nullptr;
static char theLogFileName[ _MAX_PATH ];
static char theLogFileNamePrev[ _MAX_PATH ];
#endif
#define LARGE_BUFFER	8192
static char theBuffer[ LARGE_BUFFER ];	// make it big to avoid weird overflow bugs in debug mode
static int theDebugFlags = 0;
static DWORD theMainThreadID = 0;
// ----------------------------------------------------------------------------
// PUBLIC DATA
// ----------------------------------------------------------------------------

char* TheCurrentIgnoreCrashPtr = nullptr;
Bool g_enableLogConsole = FALSE;
Bool g_enableLogFile = FALSE;  // OFF by default; launcher checkbox enables
#ifdef DEBUG_LOGGING
UnsignedInt DebugLevelMask = 0;
const char *TheDebugLevels[DEBUG_LEVEL_MAX] = {
	"NET"
};
#endif

// ----------------------------------------------------------------------------
// PRIVATE PROTOTYPES
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString();
static const char *getCurrentTickString();
static void prepBuffer(char *buffer);
#ifdef DEBUG_LOGGING
static void doLogOutput(const char *buffer);
static void doLogOutput(const char *buffer, const char *endline);
#endif
#ifdef DEBUG_CRASHING
static int doCrashBox(const char *buffer, Bool logResult);
#endif
static void whackFunnyCharacters(char *buf);
#ifdef DEBUG_STACKTRACE
static void doStackDump();
#endif

// ----------------------------------------------------------------------------
// PRIVATE FUNCTIONS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
inline Bool ignoringAsserts()
{
	if (!DX8Wrapper_IsWindowed)
		return true;
	if (TheGlobalData && TheGlobalData->m_headless)
		return true;
#ifdef DEBUG_CRASHING
	if (TheGlobalData && TheGlobalData->m_debugIgnoreAsserts)
		return true;
#endif

	return false;
}

// ----------------------------------------------------------------------------
inline HWND getThreadHWND()
{
	return (theMainThreadID == GetCurrentThreadId())?ApplicationHWnd:nullptr;
}

// ----------------------------------------------------------------------------

int MessageBoxWrapper( LPCSTR lpText, LPCSTR lpCaption, UINT uType )
{
	HWND threadHWND = getThreadHWND();
	return ::MessageBox(threadHWND, lpText, lpCaption, uType);
}

// ----------------------------------------------------------------------------
// getCurrentTimeString
/**
	Return the current time in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString()
{
	time_t aclock;
	time(&aclock);
	struct tm *newtime = localtime(&aclock);
	return asctime(newtime);
}

// ----------------------------------------------------------------------------
// getCurrentTickString
/**
	Return the current TickCount in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTickString()
{
	static char TheTickString[32];
	snprintf(TheTickString, ARRAY_SIZE(TheTickString), "(T=%08lx)", ::GetTickCount());
	return TheTickString;
}

// ----------------------------------------------------------------------------
// prepBuffer
// zap the buffer and optionally prepend the tick time.
// ----------------------------------------------------------------------------
/**
	Empty the buffer passed in, then optionally prepend the current TickCount
	value in string form, depending on the setting of theDebugFlags.
*/
static void prepBuffer(char *buffer)
{
	buffer[0] = 0;
#ifdef ALLOW_DEBUG_UTILS
	if (theDebugFlags & DEBUG_FLAG_PREPEND_TIME)
	{
		strcpy(buffer, getCurrentTickString());
		strcat(buffer, " ");
	}
#endif
}

// ----------------------------------------------------------------------------
// doLogOutput
/**
	send a string directly to the log file and/or console without further processing.
*/
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
static void doLogOutput(const char *buffer)
{
		doLogOutput(buffer, "\n");
}

// --- In-game log console window (idTech-style) ---
#ifdef _WIN32
static HWND s_consoleHwnd = nullptr;
static HWND s_consoleEdit = nullptr;
static volatile Bool s_consoleReady = FALSE;
static DWORD s_consoleThreadId = 0;
static HBRUSH s_bgBrush = nullptr;
static HFONT s_consoleFont = nullptr;

#define WM_LOG_FLUSH (WM_USER + 1)

static void flushConsoleText(char *text)
{
	SendMessage(s_consoleEdit, WM_SETREDRAW, FALSE, 0);
	int len = GetWindowTextLength(s_consoleEdit);
	if (len > 0x100000)
	{
		SendMessage(s_consoleEdit, EM_SETSEL, 0, len / 2);
		SendMessage(s_consoleEdit, EM_REPLACESEL, FALSE, (LPARAM)"[...truncated...]\r\n");
		len = GetWindowTextLength(s_consoleEdit);
	}
	SendMessage(s_consoleEdit, EM_SETSEL, len, len);
	SendMessage(s_consoleEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
	SendMessage(s_consoleEdit, WM_SETREDRAW, TRUE, 0);
}

static LRESULT CALLBACK ConsoleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_SIZE:
		if (s_consoleEdit)
			MoveWindow(s_consoleEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
		return 0;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
		{
			HDC hdc = (HDC)wParam;
			SetTextColor(hdc, RGB(200, 220, 200));
			SetBkColor(hdc, RGB(12, 14, 18));
			if (!s_bgBrush) s_bgBrush = CreateSolidBrush(RGB(12, 14, 18));
			return (LRESULT)s_bgBrush;
		}
	case WM_LOG_FLUSH:
		{
			// Drain any coalesced flush messages
			MSG peekMsg;
			while (PeekMessage(&peekMsg, hwnd, WM_LOG_FLUSH, WM_LOG_FLUSH, PM_REMOVE))
			{
				char *extra = (char *)peekMsg.lParam;
				if (extra) { flushConsoleText(extra); free(extra); }
			}
			char *text = (char *)lParam;
			if (text) { flushConsoleText(text); free(text); }
			return 0;
		}
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI ConsoleThreadProc(LPVOID)
{
	WNDCLASSA wc = {};
	wc.lpfnWndProc = ConsoleWndProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = "ZHLogConsole";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassA(&wc);

	s_consoleHwnd = CreateWindowExA(
		WS_EX_TOOLWINDOW, "ZHLogConsole", "Generals - Log Console",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 1000, 600,
		nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

	s_consoleEdit = CreateWindowExA(
		0, "EDIT", "",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
		0, 0, 1000, 600,
		s_consoleHwnd, nullptr, GetModuleHandle(nullptr), nullptr);

	s_consoleFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
	SendMessage(s_consoleEdit, WM_SETFONT, (WPARAM)s_consoleFont, TRUE);
	SendMessage(s_consoleEdit, EM_SETLIMITTEXT, 0x200000, 0);

	ShowWindow(s_consoleHwnd, SW_SHOW);
	UpdateWindow(s_consoleHwnd);
	s_consoleReady = TRUE;

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}

void initLogConsole()
{
	if (s_consoleThreadId)
		return;
	HANDLE hThread = CreateThread(nullptr, 0, ConsoleThreadProc, nullptr, 0, &s_consoleThreadId);
	if (hThread) CloseHandle(hThread);
	for (int i = 0; i < 200 && !s_consoleReady; ++i)
		Sleep(5);
}

static void appendToConsole(const char *text)
{
	if (!s_consoleHwnd || !s_consoleReady)
		return;

	// Batch log messages and flush periodically via non-blocking PostMessage.
	// This avoids expensive cross-thread SendMessage calls that were blocking the game thread.
	static char pendingBuf[65536];
	static int pendingLen = 0;
	static DWORD lastFlush = 0;

	int textLen = (int)strlen(text);
	if (pendingLen + textLen < (int)sizeof(pendingBuf) - 1)
	{
		memcpy(pendingBuf + pendingLen, text, textLen);
		pendingLen += textLen;
		pendingBuf[pendingLen] = 0;
	}

	DWORD now = GetTickCount();
	if (now - lastFlush < 100 && pendingLen < 32768)
		return;
	lastFlush = now;

	char *copy = (char *)malloc(pendingLen + 1);
	if (copy)
	{
		memcpy(copy, pendingBuf, pendingLen + 1);
		if (!PostMessage(s_consoleHwnd, WM_LOG_FLUSH, 0, (LPARAM)copy))
			free(copy);
	}
	pendingLen = 0;
	pendingBuf[0] = 0;
}
// --- End log console ---
#endif // _WIN32

// Optional sink hook for tooling. The Inspector module registers one
// during init so the in-game log panel can mirror everything DEBUG_LOG
// emits without intercepting OutputDebugString or fprintf. The sink is
// called from doLogOutput on the same thread as DebugLog and must not
// re-enter the logger (which would deadlock the critical section).
static DebugLogSink s_logSink = nullptr;

DEBUG_EXTERN_C void DebugSetLogSink(DebugLogSink sink)
{
	s_logSink = sink;
}

static void doLogOutput(const char *buffer, const char *endline)
{
	// log message to file
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_FILE)
	{
		if (theLogFile)
		{
			fprintf(theLogFile, "%s%s", buffer, endline);
			// fflush removed: per-call flushing causes massive I/O overhead
			// (3ms per module update × 285 modules = 850ms/frame)
			// stdio buffering handles flushing automatically
		}
	}

	// log message to dev studio output window
#ifdef _WIN32
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_CONSOLE)
	{
		::OutputDebugString(buffer);
		::OutputDebugString(endline);
	}

	// log message to in-game console window
	if (s_consoleReady)
	{
		char consoleBuf[LARGE_BUFFER + 4];
		snprintf(consoleBuf, sizeof(consoleBuf), "%s\r\n", buffer);
		appendToConsole(consoleBuf);
	}
#endif

#ifdef INCLUDE_DEBUG_LOG_IN_CRC_LOG
	addCRCDebugLineNoCounter("%s%s", buffer, endline);
#endif

	// Tooling sink runs last so it sees the same formatted text the
	// file/console writes received. We pass only the buffer (not the
	// endline separator) so the sink can decide whether to append its
	// own newline — the in-process inspector wants exactly one line
	// per entry without any trailing whitespace.
	if (s_logSink)
		s_logSink(buffer);
}
#endif // DEBUG_LOGGING

// ----------------------------------------------------------------------------
// doCrashBox
/*
	present a messagebox with the given message. Depending on user selection,
	we exit the app, break into debugger, or continue execution.
*/
// ----------------------------------------------------------------------------
#ifdef DEBUG_CRASHING
static int doCrashBox(const char *buffer, Bool logResult)
{
	int result;

	if (!ignoringAsserts()) {
		result = MessageBoxWrapper(buffer, "Assertion Failure", MB_ABORTRETRYIGNORE|MB_TASKMODAL|MB_ICONWARNING|MB_DEFBUTTON3);
		//result = MessageBoxWrapper(buffer, "Assertion Failure", MB_ABORTRETRYIGNORE|MB_TASKMODAL|MB_ICONWARNING);
	}	else {
		result = IDIGNORE;
	}

	switch(result)
	{
		case IDABORT:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Abort]");
#endif
			_exit(1);
			break;
		case IDRETRY:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Retry]");
#endif
			::DebugBreak();
			break;
		case IDIGNORE:
#ifdef DEBUG_LOGGING
			// do nothing, just keep going
			if (logResult)
				DebugLog("[Ignore]");
#endif
			break;
	}
	return result;
}
#endif

#ifdef DEBUG_STACKTRACE
// ----------------------------------------------------------------------------
/**
	Dumps a stack trace (from the current PC) to logfile and/or console.
*/
static void doStackDump()
{
	const int STACKTRACE_SIZE	= 24;
	const int STACKTRACE_SKIP = 2;
	void* stacktrace[STACKTRACE_SIZE];

	doLogOutput("\nStack Dump:");
	::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
	::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, doLogOutput);
}
#endif

// ----------------------------------------------------------------------------
// whackFunnyCharacters
/**
	Eliminates any undesirable nonprinting characters, aside from newline,
	replacing them with spaces.
*/
// ----------------------------------------------------------------------------
static void whackFunnyCharacters(char *buf)
{
	for (char *p = buf + strlen(buf) - 1; p >= buf; --p)
	{
		// ok, these are naughty magic numbers, but I'm guessing you know ASCII....
		if (*p >= 0 && *p < 32 && *p != 10 && *p != 13)
			*p = 32;
	}
}

// ----------------------------------------------------------------------------
// PUBLIC FUNCTIONS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// DebugInit
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Initialize the debug utilities. This should be called once, as near to the
	start of the app as possible, before anything else (since other code will
	probably want to make use of it).
*/
void DebugInit(int flags)
{
//	if (theDebugFlags != 0)
//		::MessageBox(nullptr, "Debug already inited", "", MB_OK|MB_APPLMODAL);

	// just quietly allow multiple calls to this, so that static ctors can call it.
	if (theDebugFlags == 0)
	{
		theDebugFlags = flags;

		theMainThreadID = GetCurrentThreadId();

	#ifdef DEBUG_LOGGING

		// Determine the client instance id before creating the log file with an instance specific name.
		CommandLine::parseCommandLineForStartup();

		if (!rts::ClientInstance::initialize())
			return;

		char dirbuf[ _MAX_PATH ];
		::GetModuleFileName( nullptr, dirbuf, sizeof( dirbuf ) );
		if (char *pEnd = strrchr(dirbuf, '\\'))
		{
			*(pEnd + 1) = 0;
		}

		static_assert(ARRAY_SIZE(theLogFileNamePrev) >= ARRAY_SIZE(dirbuf), "Incorrect array size");
		strcpy(theLogFileNamePrev, dirbuf);
		strlcat(theLogFileNamePrev, gAppPrefix, ARRAY_SIZE(theLogFileNamePrev));
		strlcat(theLogFileNamePrev, DEBUG_FILE_NAME_PREV, ARRAY_SIZE(theLogFileNamePrev));
		if (rts::ClientInstance::getInstanceId() > 1u)
		{
			size_t offset = strlen(theLogFileNamePrev);
			snprintf(theLogFileNamePrev + offset, ARRAY_SIZE(theLogFileNamePrev) - offset, "_Instance%.2u", rts::ClientInstance::getInstanceId());
		}
		strlcat(theLogFileNamePrev, ".txt", ARRAY_SIZE(theLogFileNamePrev));

		static_assert(ARRAY_SIZE(theLogFileName) >= ARRAY_SIZE(dirbuf), "Incorrect array size");
		strcpy(theLogFileName, dirbuf);
		strlcat(theLogFileName, gAppPrefix, ARRAY_SIZE(theLogFileNamePrev));
		strlcat(theLogFileName, DEBUG_FILE_NAME, ARRAY_SIZE(theLogFileNamePrev));
		if (rts::ClientInstance::getInstanceId() > 1u)
		{
			size_t offset = strlen(theLogFileName);
			snprintf(theLogFileName + offset, ARRAY_SIZE(theLogFileName) - offset, "_Instance%.2u", rts::ClientInstance::getInstanceId());
		}
		strlcat(theLogFileName, ".txt", ARRAY_SIZE(theLogFileNamePrev));

		if (g_enableLogFile)
		{
			remove(theLogFileNamePrev);
			if (rename(theLogFileName, theLogFileNamePrev) != 0)
			{
				if (remove(theLogFileName) != 0) {}
			}

			theLogFile = fopen(theLogFileName, "w");
			if (theLogFile != nullptr)
			{
				DebugLog("Log %s opened: %s", theLogFileName, getCurrentTimeString());
			}
		}

		// Log console is launched later from WinMain after the launcher dialog
	#endif
	}

}
#endif

void DebugCloseLogFile()
{
#ifdef DEBUG_LOGGING
	if (theLogFile)
	{
		fclose(theLogFile);
		theLogFile = nullptr;
	}
	// Remove the empty/short log files
	remove(theLogFileName);
	remove(theLogFileNamePrev);
#endif
}

void DebugOpenLogFile()
{
#ifdef DEBUG_LOGGING
	if (theLogFile)
		return; // already open

	if (theLogFileName[0] == 0)
		return; // DebugInit hasn't run yet

	remove(theLogFileNamePrev);
	rename(theLogFileName, theLogFileNamePrev);
	theLogFile = fopen(theLogFileName, "w");
	if (theLogFile)
	{
		DebugLog("Log %s opened: %s", theLogFileName, getCurrentTimeString());
	}
#endif
}

// ----------------------------------------------------------------------------
// DebugLog
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
/**
	Print a string to the log file and/or console.
*/
void DebugLog(const char *format, ...)
{
	// Performance: skip all debug logging during gameplay.
	// The original per-call fprintf+fflush caused 1000ms/frame overhead.
	// Re-enable by removing this early return for debugging.
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_FILE)
	{
		// Only log if explicitly requested to file (startup logs)
		// Skip per-frame gameplay logging
		static int s_logCallCount = 0;
		if (++s_logCallCount > 5000)
			return; // Stop logging after initial 5000 messages
	}

#ifdef DEBUG_THREADSAFE
	ScopedCriticalSection scopedCriticalSection(TheDebugLogCriticalSection);
#endif

	if (theDebugFlags == 0)
		return; // Not initialized

	prepBuffer(theBuffer);

	va_list args;
	va_start(args, format);
	size_t offset = strlen(theBuffer);
	vsnprintf(theBuffer + offset, ARRAY_SIZE(theBuffer) - offset, format, args);
	va_end(args);

	whackFunnyCharacters(theBuffer);
	doLogOutput(theBuffer);
}

/**
	Print a string with no modifications to the log file and/or console.
*/
void DebugLogRaw(const char *format, ...)
{
#ifdef DEBUG_THREADSAFE
	ScopedCriticalSection scopedCriticalSection(TheDebugLogCriticalSection);
#endif

	if (theDebugFlags == 0)
		MessageBoxWrapper("DebugLogRaw - Debug not inited properly", "", MB_OK|MB_TASKMODAL);

	theBuffer[0] = 0;

	va_list args;
	va_start(args, format);
	vsnprintf(theBuffer, ARRAY_SIZE(theBuffer), format, args);
	va_end(args);

	if (strlen(theBuffer) >= sizeof(theBuffer))
		MessageBoxWrapper("String too long for debug buffer", "", MB_OK|MB_TASKMODAL);

	doLogOutput(theBuffer, "");
}

const char* DebugGetLogFileName()
{
	return theLogFileName;
}

const char* DebugGetLogFileNamePrev()
{
	return theLogFileNamePrev;
}

#endif

// ----------------------------------------------------------------------------
// DebugCrash
// ----------------------------------------------------------------------------
#ifdef DEBUG_CRASHING
/**
	Print a character string to the log file and/or console, then halt execution
	while presenting the user with an exit/debug/ignore dialog containing the same
	text message. Shows a message box without any logging when debug was not yet
	initialized.
*/
void DebugCrash(const char *format, ...)
{
	// Note: You might want to make this thread safe, but we cannot. The reason is that
	// there is an implicit requirement on other threads that the message loop be running.

	// make it not static so that it'll be thread-safe.
	// make it big to avoid weird overflow bugs in debug mode
	char theCrashBuffer[ LARGE_BUFFER ];

	prepBuffer(theCrashBuffer);
	strlcat(theCrashBuffer, "ASSERTION FAILURE: ", ARRAY_SIZE(theCrashBuffer));

	va_list arg;
	va_start(arg, format);
	size_t offset =  strlen(theCrashBuffer);
	vsnprintf(theCrashBuffer + offset, ARRAY_SIZE(theCrashBuffer) - offset, format, arg);
	va_end(arg);

	whackFunnyCharacters(theCrashBuffer);

	const bool useLogging = theDebugFlags != 0;

	if (useLogging)
	{
#ifdef DEBUG_LOGGING
		if (ignoringAsserts())
		{
			doLogOutput("**** CRASH IN FULL SCREEN - Auto-ignored, CHECK THIS LOG!");
		}
		doLogOutput(theCrashBuffer);
#endif
#ifdef DEBUG_STACKTRACE
		if (!(TheGlobalData && TheGlobalData->m_debugIgnoreStackTrace))
		{
			doStackDump();
		}
#endif
	}

	strlcat(theCrashBuffer, "\n\nAbort->exception; Retry->debugger; Ignore->continue", ARRAY_SIZE(theCrashBuffer));

	const int result = doCrashBox(theCrashBuffer, useLogging);

	if (result == IDIGNORE && TheCurrentIgnoreCrashPtr != nullptr)
	{
		int yn;
		if (!ignoringAsserts())
		{
			yn = MessageBoxWrapper("Ignore this crash from now on?", "", MB_YESNO|MB_TASKMODAL);
		}
		else
		{
			yn = IDYES;
		}
		if (yn == IDYES)
			*TheCurrentIgnoreCrashPtr = 1;
		if( TheKeyboard )
			TheKeyboard->resetKeys();
		if( TheMouse )
			TheMouse->reset();
	}

}
#endif

// ----------------------------------------------------------------------------
// DebugShutdown
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Shut down the debug utilities. This should be called once, as near to the
	end of the app as possible, after everything else (since other code will
	probably want to make use of it).
*/
void DebugShutdown()
{
#ifdef DEBUG_LOGGING
	if (theLogFile)
	{
		DebugLog("Log closed: %s", getCurrentTimeString());
		fclose(theLogFile);
	}
	theLogFile = nullptr;
#endif
	theDebugFlags = 0;
}

// ----------------------------------------------------------------------------
// DebugGetFlags
// ----------------------------------------------------------------------------
/**
	Get the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
int DebugGetFlags()
{
	return theDebugFlags;
}

// ----------------------------------------------------------------------------
// DebugSetFlags
// ----------------------------------------------------------------------------
/**
	Set the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
void DebugSetFlags(int flags)
{
	theDebugFlags = flags;
}

#endif	// ALLOW_DEBUG_UTILS

#ifdef DEBUG_PROFILE
#include <cstdint>
#ifndef _WIN32
#include <chrono>
#endif
// ----------------------------------------------------------------------------
SimpleProfiler::SimpleProfiler()
{
#ifdef _WIN32
	QueryPerformanceFrequency((LARGE_INTEGER*)&m_freq);
#else
	m_freq = 1000000000LL; // nanoseconds (std::chrono)
#endif
	m_startThisSession = 0;
	m_totalThisSession = 0;
	m_totalAllSessions = 0;
	m_numSessions = 0;
}

// ----------------------------------------------------------------------------
static int64_t GetPerfCounter()
{
#ifdef _WIN32
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return li.QuadPart;
#else
	return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}

void SimpleProfiler::start()
{
	DEBUG_ASSERTCRASH(m_startThisSession == 0, ("already started"));
	m_startThisSession = GetPerfCounter();
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stop()
{
	if (m_startThisSession != 0)
	{
		int64_t stop = GetPerfCounter();
		m_totalThisSession = stop - m_startThisSession;
		m_totalAllSessions += stop - m_startThisSession;
		m_startThisSession = 0;
		++m_numSessions;
	}
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stopAndLog(const char *msg, int howOftenToLog, int howOftenToResetAvg)
{
	stop();
	// howOftenToResetAvg==0 means "never reset"
	if (howOftenToResetAvg > 0 && m_numSessions >= howOftenToResetAvg)
	{
		m_numSessions = 0;
		m_totalAllSessions = 0;
		DEBUG_LOG(("%s: reset averages",msg));
	}
	DEBUG_ASSERTLOG(m_numSessions % howOftenToLog != 0, ("%s: %f msec, total %f msec, avg %f msec",msg,getTime(),getTotalTime(),getAverageTime()));
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTime()
{
	stop();
	return (double)m_totalThisSession / (double)m_freq * 1000.0;
}

// ----------------------------------------------------------------------------
int SimpleProfiler::getNumSessions()
{
	stop();
	return m_numSessions;
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTotalTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return (double)m_totalAllSessions * 1000.0 / ((double)m_freq);
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getAverageTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return (double)m_totalAllSessions * 1000.0 / ((double)m_freq * (double)m_numSessions);
}

#endif	// DEBUG_PROFILE

// ----------------------------------------------------------------------------
// ReleaseCrash
// ----------------------------------------------------------------------------
/**
	Halt the application, EVEN IN FINAL RELEASE BUILDS. This should be called
	only when a crash is guaranteed by continuing, and no meaningful continuation
	of processing is possible, even by throwing an exception.
*/

	#define RELEASECRASH_FILE_NAME				"ReleaseCrashInfo.txt"
	#define RELEASECRASH_FILE_NAME_PREV		"ReleaseCrashInfoPrev.txt"

	static FILE *theReleaseCrashLogFile = nullptr;

	static void releaseCrashLogOutput(const char *buffer)
	{
		if (theReleaseCrashLogFile)
		{
			fprintf(theReleaseCrashLogFile, "%s\n", buffer);
			fflush(theReleaseCrashLogFile);
		}
	}


static void TriggerMiniDump()
{
#ifdef RTS_ENABLE_CRASHDUMP
	if (TheMiniDumper && TheMiniDumper->IsInitialized())
	{
		// Create both minimal and full memory dumps
		TheMiniDumper->TriggerMiniDump(DumpType_Minimal);
		TheMiniDumper->TriggerMiniDump(DumpType_Full);
	}

	MiniDumper::shutdownMiniDumper();
#endif
}


void ReleaseCrash(const char *reason)
{
	/// do additional reporting on the crash, if possible

	if (!DX8Wrapper_IsWindowed) {
		if (ApplicationHWnd) {
			ShowWindow(ApplicationHWnd, SW_HIDE);
		}
	}

	TriggerMiniDump();

	char prevbuf[ _MAX_PATH ];
	char curbuf[ _MAX_PATH ];

	if (TheGlobalData==nullptr) {
		return; // We are shutting down, and TheGlobalData has been freed.  jba. [4/15/2003]
	}

	strlcpy(prevbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(prevbuf));
	strlcat(prevbuf, RELEASECRASH_FILE_NAME_PREV, ARRAY_SIZE(prevbuf));
	strlcpy(curbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(curbuf));
	strlcat(curbuf, RELEASECRASH_FILE_NAME, ARRAY_SIZE(curbuf));

 	remove(prevbuf);
	if (rename(curbuf, prevbuf) != 0)
	{
#ifdef DEBUG_LOGGING
		DebugLog("Warning: Could not rename buffer file '%s' to '%s'. Will remove instead", curbuf, prevbuf);
#endif
		if (remove(curbuf) != 0)
		{
#ifdef DEBUG_LOGGING
			DebugLog("Warning: Failed to remove file '%s'", curbuf);
#endif
		}
	}

	theReleaseCrashLogFile = fopen(curbuf, "w");
	if (theReleaseCrashLogFile)
	{
		fprintf(theReleaseCrashLogFile, "Release Crash at %s; Reason %s\n", getCurrentTimeString(), reason);
		fprintf(theReleaseCrashLogFile, "\nLast error:\n%s\n\nCurrent stack:\n", g_LastErrorDump.str());
		const int STACKTRACE_SIZE	= 12;
		const int STACKTRACE_SKIP = 6;
		void* stacktrace[STACKTRACE_SIZE];
		::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
		::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, releaseCrashLogOutput);

		fflush(theReleaseCrashLogFile);
		fclose(theReleaseCrashLogFile);
		theReleaseCrashLogFile = nullptr;
	}

	if (!DX8Wrapper_IsWindowed) {
		if (ApplicationHWnd) {
			ShowWindow(ApplicationHWnd, SW_HIDE);
		}
	}

#if defined(RTS_DEBUG)
	/* static */ char buff[8192]; // not so static so we can be threadsafe
	snprintf(buff, 8192, "Sorry, a serious error occurred. (%s)", reason);
	::MessageBox(nullptr, buff, "Operation Discombobulated", MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
#else
// crash error messaged changed 3/6/03 BGC
//	::MessageBox(nullptr, "Sorry, a serious error occurred.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);
//	::MessageBox(nullptr, "You have encountered a serious error.  Serious errors can be caused by many things including viruses, overheated hardware and hardware that does not meet the minimum specifications for the game. Please visit the forums at www.generals.ea.com for suggested courses of action or consult your manual for Technical Support contact information.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);

// crash error message changed again 8/22/03 M Lorenzen... made this message box modal to the system so it will appear on top of any task-modal windows, splash-screen, etc.
  ::MessageBox(nullptr, "You have encountered a serious error.  Serious errors can be caused by many things including viruses, overheated hardware and hardware that does not meet the minimum specifications for the game. Please visit the forums at www.generals.ea.com for suggested courses of action or consult your manual for Technical Support contact information.",
   "Operation Discombobulated",
   MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);


#endif

	_exit(1);
}

void ReleaseCrashLocalized(const AsciiString& p, const AsciiString& m)
{
	if (!TheGameText) {
		ReleaseCrash(m.str());
		// This won't ever return
		return;
	}

	TriggerMiniDump();

	UnicodeString prompt = TheGameText->fetch(p);
	UnicodeString mesg = TheGameText->fetch(m);


	/// do additional reporting on the crash, if possible

	if (!DX8Wrapper_IsWindowed) {
		if (ApplicationHWnd) {
			ShowWindow(ApplicationHWnd, SW_HIDE);
		}
	}

	if (TheSystemIsUnicode)
	{
		::MessageBoxW(nullptr, mesg.str(), prompt.str(), MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
	}
	else
	{
		// However, if we're using the default version of the message box, we need to
		// translate the string into an AsciiString
		AsciiString promptA, mesgA;
		promptA.translate(prompt);
		mesgA.translate(mesg);
		//Make sure main window is not TOP_MOST
		::SetWindowPos(ApplicationHWnd, HWND_NOTOPMOST, 0, 0, 0, 0,SWP_NOSIZE |SWP_NOMOVE);
		::MessageBoxA(nullptr, mesgA.str(), promptA.str(), MB_OK|MB_TASKMODAL|MB_ICONERROR);
	}

	char prevbuf[ _MAX_PATH ];
	char curbuf[ _MAX_PATH ];

	strlcpy(prevbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(prevbuf));
	strlcat(prevbuf, RELEASECRASH_FILE_NAME_PREV, ARRAY_SIZE(prevbuf));
	strlcpy(curbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(curbuf));
	strlcat(curbuf, RELEASECRASH_FILE_NAME, ARRAY_SIZE(curbuf));

 	remove(prevbuf);
	if (rename(curbuf, prevbuf) != 0)
	{
#ifdef DEBUG_LOGGING
		DebugLog("Warning: Could not rename buffer file '%s' to '%s'. Will remove instead", curbuf, prevbuf);
#endif
		if (remove(curbuf) != 0)
		{
#ifdef DEBUG_LOGGING
			DebugLog("Warning: Failed to remove file '%s'", curbuf);
#endif
		}
	}

	theReleaseCrashLogFile = fopen(curbuf, "w");
	if (theReleaseCrashLogFile)
	{
		fprintf(theReleaseCrashLogFile, "Release Crash at %s; Reason %ls\n", getCurrentTimeString(), mesg.str());

		const int STACKTRACE_SIZE	= 12;
		const int STACKTRACE_SKIP = 6;
		void* stacktrace[STACKTRACE_SIZE];
		::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
		::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, releaseCrashLogOutput);

		fflush(theReleaseCrashLogFile);
		fclose(theReleaseCrashLogFile);
		theReleaseCrashLogFile = nullptr;
	}

	_exit(1);
}
