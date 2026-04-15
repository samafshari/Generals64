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

// This file contains all the header files that shouldn't change frequently.
// Be careful what you stick in here, because putting files that change often in here will
// tend to cheese people's goats.

#pragma once

//-----------------------------------------------------------------------------
// srj sez: this must come first, first, first.
#define _STLP_USE_NEWALLOC					1
//#define _STLP_USE_CUSTOM_NEWALLOC		STLSpecialAlloc
class STLSpecialAlloc;


//--------------------------------------------------------------------------------- System Includes
// Platform headers: Windows-specific on Win32, POSIX on macOS/Linux

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <atlbase.h>
#include <windows.h>
#include <direct.h>
#include <excpt.h>
#include <imagehlp.h>
#include <io.h>
#include <lmcons.h>
#if defined(_MSC_VER) && _MSC_VER < 1300
#include <mapicode.h>
#endif
#include <mmsystem.h>
#include <objbase.h>
#include <ocidl.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlguid.h>
#include <snmp.h>
#include <tchar.h>
#include <vfw.h>
#include <winerror.h>
#include <wininet.h>
#include <winreg.h>

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION	0x800
#endif
#include <dinput.h>

#else // !_WIN32 (macOS, Linux)
#include <unistd.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <signal.h>
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned short WORD;
typedef void* HINSTANCE;
typedef void* HWND;
typedef void* HANDLE;
typedef void* HMODULE;
typedef long HRESULT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef long LRESULT;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef const wchar_t* LPCWSTR;
typedef void* LPVOID;
typedef unsigned long ULONG;
typedef unsigned long* ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef unsigned char BYTE;
typedef DWORD COLORREF;
typedef void* HBITMAP;
typedef void* HICON;
typedef void* HCURSOR;
typedef void* HBRUSH;
typedef void* HDC;
typedef void* HFONT;
typedef void* HMENU;
typedef void* HGLOBAL;
typedef unsigned long* LPDWORD;
typedef struct { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
static inline void GetLocalTime(SYSTEMTIME* st) {
    time_t t = time(nullptr); struct tm* tm = localtime(&t);
    if (tm && st) { st->wYear=(WORD)(tm->tm_year+1900); st->wMonth=(WORD)(tm->tm_mon+1); st->wDay=(WORD)tm->tm_mday;
        st->wHour=(WORD)tm->tm_hour; st->wMinute=(WORD)tm->tm_min; st->wSecond=(WORD)tm->tm_sec; st->wMilliseconds=0; st->wDayOfWeek=(WORD)tm->tm_wday; }
}
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(hr) ((hr) >= 0)
#define FAILED(hr)    ((hr) < 0)
#define S_OK          ((HRESULT)0L)
#define S_FALSE       ((HRESULT)1L)
#define E_FAIL        ((HRESULT)0x80004005L)
#define E_NOTIMPL     ((HRESULT)0x80004001L)
#define CALLBACK
#define WINAPI
#define APIENTRY
#define STDMETHODCALLTYPE
#define INFINITE      0xFFFFFFFF
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define ERROR_ALREADY_EXISTS 183
#define WAIT_OBJECT_0        0
#define WAIT_TIMEOUT         0x102
// Win32 sync/thread API shims (minimal for compilation)
#include <pthread.h>
static inline HANDLE CreateMutex(void*, int, const char*) { return (HANDLE)(intptr_t)1; }
static inline HANDLE CreateEvent(void*, int, int, const char*) { return (HANDLE)(intptr_t)1; }
static inline HANDLE OpenEvent(unsigned long, int, const char*) { return (HANDLE)(intptr_t)1; }
static inline DWORD WaitForSingleObject(HANDLE, DWORD) { return WAIT_OBJECT_0; }
static inline int ReleaseMutex(HANDLE) { return 1; }
static inline int SetEvent(HANDLE) { return 1; }
static inline int CloseHandle(HANDLE) { return 1; }
static inline DWORD GetLastError() { return 0; }
static inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
static inline HANDLE CreateThread(void*, size_t, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD, DWORD*) {
    pthread_t t; pthread_create(&t, nullptr, (void*(*)(void*))fn, arg); return (HANDLE)t;
}
// Win32 file/path API shims
#include <cstdio>
#include <climits>
static inline DWORD GetModuleFileName(HMODULE, char* buf, DWORD size) {
    // Return empty on non-Windows; SDL_GetBasePath used for exe dir
    if (buf && size > 0) buf[0] = '\0';
    return 0;
}
static inline DWORD GetModuleFileNameW(HMODULE, wchar_t* buf, DWORD size) {
    if (buf && size > 0) buf[0] = L'\0';
    return 0;
}
static inline DWORD GetCurrentDirectory(DWORD size, char* buf) {
    if (getcwd(buf, size)) return (DWORD)strlen(buf);
    return 0;
}
static inline int SetCurrentDirectory(const char* dir) { return chdir(dir) == 0; }
#define wsprintf sprintf
#define itoa(val, buf, radix) sprintf(buf, "%d", val)
// FindFirstFile/FindNextFile shim (minimal, returns INVALID_HANDLE_VALUE)
typedef struct { char cFileName[260]; DWORD dwFileAttributes; } WIN32_FIND_DATA;
#define FILE_ATTRIBUTE_DIRECTORY 0x10
static inline HANDLE FindFirstFile(const char*, WIN32_FIND_DATA*) { return INVALID_HANDLE_VALUE; }
static inline int FindNextFile(HANDLE, WIN32_FIND_DATA*) { return 0; }
static inline int FindClose(HANDLE) { return 1; }
// Shared memory / file mapping (no-op)
#define PAGE_READWRITE 0x04
static inline HANDLE CreateFileMapping(HANDLE, void*, DWORD, DWORD, DWORD, const char*) { return nullptr; }
static inline void* MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, size_t) { return nullptr; }
static inline int UnmapViewOfFile(const void*) { return 1; }
// Console/pipe handles
#define STD_OUTPUT_HANDLE ((DWORD)-11)
static inline HANDLE GetStdHandle(DWORD) { return nullptr; }
static inline int WriteFile(HANDLE, const void* buf, DWORD len, DWORD* written, void*) {
    if (written) *written = (DWORD)fwrite(buf, 1, len, stdout);
    return 1;
}
static inline int ReadFile(HANDLE, void*, DWORD, DWORD* read, void*) {
    if (read) *read = 0;
    return 0;
}
#define GENERIC_READ  0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 0x01
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
static inline HANDLE CreateFile(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) { return INVALID_HANDLE_VALUE; }
#include <netdb.h>
typedef struct hostent HOSTENT;
#define MAKEINTRESOURCE(i) ((const char*)(uintptr_t)(i))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)((WORD)(l) | ((DWORD)(WORD)(h)) << 16))
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#endif // _WIN32

// Standard C headers (cross-platform)
#include <assert.h>
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <memory.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

//------------------------------------------------------------------------------------ STL Includes
// srj sez: no, include STLTypesdefs below, instead, thanks
//#include <algorithm>
//#include <bitset>
//#include <hash_map>
//#include <list>
//#include <map>
//#include <queue>
//#include <set>
//#include <stack>
//#include <string>
//#include <vector>

//------------------------------------------------------------------------------------ RTS Includes
// Icky. These have to be in this order.
#include "Lib/BaseType.h"
#include "Common/STLTypedefs.h"
#include "Common/Errors.h"
#include "Common/Debug.h"
#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"

#include "Common/GameCommon.h"
#include "Common/GameMemory.h"
#include "Common/GameType.h"
#include "Common/GlobalData.h"

// You might not want Kindof in here because it seems like it changes frequently, but the problem
// is that Kindof is included EVERYWHERE, so it might as well be precompiled.
#include "Common/INI.h"
#include "Common/KindOf.h"
#include "Common/DisabledTypes.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/ClientRandomValue.h"
#include "GameLogic/LogicRandomValue.h"
#include "Common/ObjectStatusTypes.h"

#include "Common/Thing.h"
#include "Common/UnicodeString.h"

#if defined(__GNUC__) && defined(_WIN32)
    #pragma GCC diagnostic pop
#endif
