/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// Win32OSDisplay.cpp — Message box and system busy state.
// Portable: uses SDL_ShowSimpleMessageBox when USE_SDL is defined,
// falls back to Win32 MessageBox otherwise.

#include "Common/OSDisplay.h"
#include "Common/SubsystemInterface.h"
#include "Common/STLTypedefs.h"
#include "Common/AsciiString.h"
#include "Common/SystemInfo.h"
#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"

#ifdef USE_SDL
#include <SDL3/SDL.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern HWND ApplicationHWnd;
#endif

OSDisplayButtonType OSDisplayWarningBox(AsciiString p, AsciiString m, UnsignedInt buttonFlags, UnsignedInt otherFlags)
{
	if (!TheGameText) {
		return OSDBT_ERROR;
	}

	UnicodeString promptStr = TheGameText->fetch(p);
	UnicodeString mesgStr = TheGameText->fetch(m);

#ifdef USE_SDL
	// Use SDL message box — portable across all platforms
	uint32_t sdlFlags = SDL_MESSAGEBOX_INFORMATION;
	if (BitIsSet(otherFlags, OSDOF_ERRORICON) || BitIsSet(otherFlags, OSDOF_STOPICON))
		sdlFlags = SDL_MESSAGEBOX_ERROR;
	else if (BitIsSet(otherFlags, OSDOF_EXCLAMATIONICON))
		sdlFlags = SDL_MESSAGEBOX_WARNING;

	// Convert to ASCII for SDL (it doesn't take wide strings)
	AsciiString promptA, mesgA;
	promptA.translate(promptStr);
	mesgA.translate(mesgStr);

	SDL_ShowSimpleMessageBox(sdlFlags, promptA.str(), mesgA.str(), nullptr);
	return OSDBT_OK;

#elif defined(_WIN32)
	// Win32 MessageBox path
	UnsignedInt windowsFlags = 0;
	if (BitIsSet(buttonFlags, OSDBT_OK))      windowsFlags |= MB_OK;
	if (BitIsSet(buttonFlags, OSDBT_CANCEL))   windowsFlags |= MB_OKCANCEL;
	if (BitIsSet(otherFlags, OSDOF_SYSTEMMODAL))      windowsFlags |= MB_SYSTEMMODAL;
	if (BitIsSet(otherFlags, OSDOF_APPLICATIONMODAL))  windowsFlags |= MB_APPLMODAL;
	if (BitIsSet(otherFlags, OSDOF_TASKMODAL))         windowsFlags |= MB_TASKMODAL;
	if (BitIsSet(otherFlags, OSDOF_EXCLAMATIONICON))   windowsFlags |= MB_ICONEXCLAMATION;
	if (BitIsSet(otherFlags, OSDOF_INFORMATIONICON))   windowsFlags |= MB_ICONINFORMATION;
	if (BitIsSet(otherFlags, OSDOF_ERRORICON))         windowsFlags |= MB_ICONERROR;
	if (BitIsSet(otherFlags, OSDOF_STOPICON))          windowsFlags |= MB_ICONSTOP;

	Int result = 0;
	if (TheSystemIsUnicode)
	{
		result = ::MessageBoxW(nullptr, mesgStr.str(), promptStr.str(), windowsFlags);
	}
	else
	{
		AsciiString promptA, mesgA;
		promptA.translate(promptStr);
		mesgA.translate(mesgStr);
		::SetWindowPos(ApplicationHWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		result = ::MessageBoxA(nullptr, mesgA.str(), promptA.str(), windowsFlags);
	}

	return (result == IDOK) ? OSDBT_OK : OSDBT_CANCEL;
#else
	// Fallback: log to stderr
	AsciiString promptA, mesgA;
	promptA.translate(promptStr);
	mesgA.translate(mesgStr);
	fprintf(stderr, "[%s] %s\n", promptA.str(), mesgA.str());
	return OSDBT_OK;
#endif
}

void OSDisplaySetBusyState(Bool busyDisplay, Bool busySystem)
{
#ifdef _WIN32
	EXECUTION_STATE state = ES_CONTINUOUS;
	state |= busyDisplay ? ES_DISPLAY_REQUIRED : 0;
	state |= busySystem ? ES_SYSTEM_REQUIRED : 0;
	::SetThreadExecutionState(state);
#else
	// On macOS/Linux, SDL handles display/system idle prevention
	// via SDL_DisableScreenSaver() which is called in SDLPlatform::Init
	(void)busyDisplay;
	(void)busySystem;
#endif
}
