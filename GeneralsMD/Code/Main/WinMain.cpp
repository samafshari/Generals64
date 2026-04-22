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

// FILE: WinMain.cpp //////////////////////////////////////////////////////////
//
// Entry point for game application
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <stdlib.h>
#include <string>
#include <csignal>
#include <exception>
#ifdef _WIN32
#include <crtdbg.h>
#include <eh.h>
#include <ole2.h>
#include <dbt.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#endif

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "WinMain.h"
#include "Lib/BaseType.h"
#include "Common/CommandLine.h"
#include "Common/CriticalSection.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "Common/GameSounds.h"
#include "Common/Debug.h"
#include "Common/GameMemory.h"
#include "Common/StackDump.h"
#include "Common/MessageStream.h"
#include "Common/Registry.h"
#include "Common/Team.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameClient.h"
#include "GameClient/Display.h"
#include "GameClient/Shell.h"
#include "GameClient/HeaderTemplate.h"
#include "GameLogic/GameLogic.h"  ///< @todo for demo, remove
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/IMEManager.h"
#ifdef _WIN32
#include "Win32Device/GameClient/Win32Mouse.h"
#include "Win32Device/Common/Win32GameEngine.h"
#endif
#ifdef USE_SDL
#include "SDLGameEngine.h"
#include "SDLPlatform.h"
#include <SDL3/SDL.h>
#endif
#include "Common/version.h"
#include "BuildVersion.h"
#include "GeneratedVersion.h"
#include "resource.h"

#include <rts/profile.h>
#ifdef RTS_ENABLE_CRASHDUMP
#include "Common/MiniDumper.h"
#endif


// GLOBALS ////////////////////////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = nullptr;  ///< our application instance
HWND ApplicationHWnd = nullptr;  ///< our application window handle
#ifdef _WIN32
Win32Mouse *TheWin32Mouse = nullptr;  ///< for the WndProc() only
DWORD TheMessageTime = 0;	///< For getting the time that a message was posted from Windows.
#endif

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
const char *gAppPrefix = ""; /// So WB can have a different debug log file name.

static Bool gInitializing = false;
static Bool gDoPaint = true;
Bool gPendingResize = FALSE;
static Bool isWinMainActive = false;

static HBITMAP gLoadScreenBitmap = nullptr;

//#define DEBUG_WINDOWS_MESSAGES

#ifdef DEBUG_WINDOWS_MESSAGES
static const char *messageToString(unsigned int message)
{
	static char name[32];

	switch (message)
	{
	case WM_NULL: return "WM_NULL";
	case WM_CREATE: return  "WM_CREATE";
	case WM_DESTROY: return  "WM_DESTROY";
	case WM_MOVE: return  "WM_MOVE";
	case WM_SIZE: return  "WM_SIZE";
	case WM_ACTIVATE: return  "WM_ACTIVATE";
	case WM_SETFOCUS: return  "WM_SETFOCUS";
	case WM_KILLFOCUS: return  "WM_KILLFOCUS";
	case WM_ENABLE: return  "WM_ENABLE";
	case WM_SETREDRAW: return  "WM_SETREDRAW";
	case WM_SETTEXT: return  "WM_SETTEXT";
	case WM_GETTEXT: return  "WM_GETTEXT";
	case WM_GETTEXTLENGTH: return  "WM_GETTEXTLENGTH";
	case WM_PAINT: return  "WM_PAINT";
	case WM_CLOSE: return  "WM_CLOSE";
	case WM_QUERYENDSESSION: return  "WM_QUERYENDSESSION";
	case WM_QUIT: return  "WM_QUIT";
	case WM_QUERYOPEN: return  "WM_QUERYOPEN";
	case WM_ERASEBKGND: return  "WM_ERASEBKGND";
	case WM_SYSCOLORCHANGE: return  "WM_SYSCOLORCHANGE";
	case WM_ENDSESSION: return  "WM_ENDSESSION";
	case WM_SHOWWINDOW: return  "WM_SHOWWINDOW";
	case WM_WININICHANGE: return "WM_WININICHANGE";
	case WM_DEVMODECHANGE: return  "WM_DEVMODECHANGE";
	case WM_ACTIVATEAPP: return  "WM_ACTIVATEAPP";
	case WM_FONTCHANGE: return  "WM_FONTCHANGE";
	case WM_TIMECHANGE: return  "WM_TIMECHANGE";
	case WM_CANCELMODE: return  "WM_CANCELMODE";
	case WM_SETCURSOR: return  "WM_SETCURSOR";
	case WM_MOUSEACTIVATE: return  "WM_MOUSEACTIVATE";
	case WM_CHILDACTIVATE: return  "WM_CHILDACTIVATE";
	case WM_QUEUESYNC: return  "WM_QUEUESYNC";
	case WM_GETMINMAXINFO: return  "WM_GETMINMAXINFO";
	case WM_PAINTICON: return  "WM_PAINTICON";
	case WM_ICONERASEBKGND: return  "WM_ICONERASEBKGND";
	case WM_NEXTDLGCTL: return  "WM_NEXTDLGCTL";
	case WM_SPOOLERSTATUS: return  "WM_SPOOLERSTATUS";
	case WM_DRAWITEM: return  "WM_DRAWITEM";
	case WM_MEASUREITEM: return  "WM_MEASUREITEM";
	case WM_DELETEITEM: return  "WM_DELETEITEM";
	case WM_VKEYTOITEM: return  "WM_VKEYTOITEM";
	case WM_CHARTOITEM: return  "WM_CHARTOITEM";
	case WM_SETFONT: return  "WM_SETFONT";
	case WM_GETFONT: return  "WM_GETFONT";
	case WM_SETHOTKEY: return  "WM_SETHOTKEY";
	case WM_GETHOTKEY: return  "WM_GETHOTKEY";
	case WM_QUERYDRAGICON: return  "WM_QUERYDRAGICON";
	case WM_COMPAREITEM: return  "WM_COMPAREITEM";
	case WM_COMPACTING: return  "WM_COMPACTING";
	case WM_COMMNOTIFY: return  "WM_COMMNOTIFY";
	case WM_WINDOWPOSCHANGING: return  "WM_WINDOWPOSCHANGING";
	case WM_WINDOWPOSCHANGED: return  "WM_WINDOWPOSCHANGED";
	case WM_POWER: return  "WM_POWER";
	case WM_COPYDATA: return  "WM_COPYDATA";
	case WM_CANCELJOURNAL: return  "WM_CANCELJOURNAL";
	case WM_NOTIFY: return  "WM_NOTIFY";
	case WM_INPUTLANGCHANGEREQUEST: return  "WM_INPUTLANGCHANGEREQUES";
	case WM_INPUTLANGCHANGE: return  "WM_INPUTLANGCHANGE";
	case WM_TCARD: return  "WM_TCARD";
	case WM_HELP: return  "WM_HELP";
	case WM_USERCHANGED: return  "WM_USERCHANGED";
	case WM_NOTIFYFORMAT: return  "WM_NOTIFYFORMAT";
	case WM_CONTEXTMENU: return  "WM_CONTEXTMENU";
	case WM_STYLECHANGING: return  "WM_STYLECHANGING";
	case WM_STYLECHANGED: return  "WM_STYLECHANGED";
	case WM_DISPLAYCHANGE: return  "WM_DISPLAYCHANGE";
	case WM_GETICON: return  "WM_GETICON";
	case WM_SETICON: return  "WM_SETICON";
	case WM_NCCREATE: return  "WM_NCCREATE";
	case WM_NCDESTROY: return  "WM_NCDESTROY";
	case WM_NCCALCSIZE: return  "WM_NCCALCSIZE";
	case WM_NCHITTEST: return  "WM_NCHITTEST";
	case WM_NCPAINT: return  "WM_NCPAINT";
	case WM_NCACTIVATE: return  "WM_NCACTIVATE";
	case WM_GETDLGCODE: return  "WM_GETDLGCODE";
	case WM_SYNCPAINT: return  "WM_SYNCPAINT";
	case WM_NCMOUSEMOVE: return  "WM_NCMOUSEMOVE";
	case WM_NCLBUTTONDOWN: return  "WM_NCLBUTTONDOWN";
	case WM_NCLBUTTONUP: return  "WM_NCLBUTTONUP";
	case WM_NCLBUTTONDBLCLK: return  "WM_NCLBUTTONDBLCLK";
	case WM_NCRBUTTONDOWN: return  "WM_NCRBUTTONDOWN";
	case WM_NCRBUTTONUP: return  "WM_NCRBUTTONUP";
	case WM_NCRBUTTONDBLCLK: return  "WM_NCRBUTTONDBLCLK";
	case WM_NCMBUTTONDOWN: return  "WM_NCMBUTTONDOWN";
	case WM_NCMBUTTONUP: return  "WM_NCMBUTTONUP";
	case WM_NCMBUTTONDBLCLK: return  "WM_NCMBUTTONDBLCLK";
	case WM_KEYDOWN: return  "WM_KEYDOWN";
	case WM_KEYUP: return  "WM_KEYUP";
	case WM_CHAR: return  "WM_CHAR";
	case WM_DEADCHAR: return  "WM_DEADCHAR";
	case WM_SYSKEYDOWN: return  "WM_SYSKEYDOWN";
	case WM_SYSKEYUP: return  "WM_SYSKEYUP";
	case WM_SYSCHAR: return  "WM_SYSCHAR";
	case WM_SYSDEADCHAR: return  "WM_SYSDEADCHAR";
	case WM_KEYLAST: return  "WM_KEYLAST";
	case WM_IME_STARTCOMPOSITION: return  "WM_IME_STARTCOMPOSITION";
	case WM_IME_ENDCOMPOSITION: return  "WM_IME_ENDCOMPOSITION";
	case WM_IME_COMPOSITION: return  "WM_IME_COMPOSITION";
	case WM_INITDIALOG: return  "WM_INITDIALOG";
	case WM_COMMAND: return  "WM_COMMAND";
	case WM_SYSCOMMAND: return  "WM_SYSCOMMAND";
	case WM_TIMER: return  "WM_TIMER";
	case WM_HSCROLL: return  "WM_HSCROLL";
	case WM_VSCROLL: return  "WM_VSCROLL";
	case WM_INITMENU: return  "WM_INITMENU";
	case WM_INITMENUPOPUP: return  "WM_INITMENUPOPUP";
	case WM_MENUSELECT: return  "WM_MENUSELECT";
	case WM_MENUCHAR: return  "WM_MENUCHAR";
	case WM_ENTERIDLE: return  "WM_ENTERIDLE";
	case WM_CTLCOLORMSGBOX: return  "WM_CTLCOLORMSGBOX";
	case WM_CTLCOLOREDIT: return  "WM_CTLCOLOREDIT";
	case WM_CTLCOLORLISTBOX: return  "WM_CTLCOLORLISTBOX";
	case WM_CTLCOLORBTN: return  "WM_CTLCOLORBTN";
	case WM_CTLCOLORDLG: return  "WM_CTLCOLORDLG";
	case WM_CTLCOLORSCROLLBAR: return  "WM_CTLCOLORSCROLLBAR";
	case WM_CTLCOLORSTATIC: return  "WM_CTLCOLORSTATIC";
	case WM_MOUSEMOVE: return  "WM_MOUSEMOVE";
	case WM_LBUTTONDOWN: return  "WM_LBUTTONDOWN";
	case WM_LBUTTONUP: return  "WM_LBUTTONUP";
	case WM_LBUTTONDBLCLK: return  "WM_LBUTTONDBLCLK";
	case WM_RBUTTONDOWN: return  "WM_RBUTTONDOWN";
	case WM_RBUTTONUP: return  "WM_RBUTTONUP";
	case WM_RBUTTONDBLCLK: return  "WM_RBUTTONDBLCLK";
	case WM_MBUTTONDOWN: return  "WM_MBUTTONDOWN";
	case WM_MBUTTONUP: return  "WM_MBUTTONUP";
	case WM_MBUTTONDBLCLK: return  "WM_MBUTTONDBLCLK";
//	case WM_MOUSEWHEEL: return  "WM_MOUSEWHEEL";
	case WM_PARENTNOTIFY: return  "WM_PARENTNOTIFY";
	case WM_ENTERMENULOOP: return  "WM_ENTERMENULOOP";
	case WM_EXITMENULOOP: return  "WM_EXITMENULOOP";
	case WM_NEXTMENU: return  "WM_NEXTMENU";
	case WM_SIZING: return  "WM_SIZING";
	case WM_CAPTURECHANGED: return  "WM_CAPTURECHANGED";
	case WM_MOVING: return  "WM_MOVING";
	case WM_POWERBROADCAST: return  "WM_POWERBROADCAST";
	case WM_DEVICECHANGE: return  "WM_DEVICECHANGE";
	case WM_MDICREATE: return  "WM_MDICREATE";
	case WM_MDIDESTROY: return  "WM_MDIDESTROY";
	case WM_MDIACTIVATE: return  "WM_MDIACTIVATE";
	case WM_MDIRESTORE: return  "WM_MDIRESTORE";
	case WM_MDINEXT: return  "WM_MDINEXT";
	case WM_MDIMAXIMIZE: return  "WM_MDIMAXIMIZE";
	case WM_MDITILE: return  "WM_MDITILE";
	case WM_MDICASCADE: return  "WM_MDICASCADE";
	case WM_MDIICONARRANGE: return  "WM_MDIICONARRANGE";
	case WM_MDIGETACTIVE: return  "WM_MDIGETACTIVE";
	case WM_MDISETMENU: return  "WM_MDISETMENU";
	case WM_ENTERSIZEMOVE: return  "WM_ENTERSIZEMOVE";
	case WM_EXITSIZEMOVE: return  "WM_EXITSIZEMOVE";
	case WM_DROPFILES: return  "WM_DROPFILES";
	case WM_MDIREFRESHMENU: return  "WM_MDIREFRESHMENU";
	case WM_IME_SETCONTEXT: return  "WM_IME_SETCONTEXT";
	case WM_IME_NOTIFY: return  "WM_IME_NOTIFY";
	case WM_IME_CONTROL: return  "WM_IME_CONTROL";
	case WM_IME_COMPOSITIONFULL: return  "WM_IME_COMPOSITIONFULL";
	case WM_IME_SELECT: return  "WM_IME_SELECT";
	case WM_IME_CHAR: return  "WM_IME_CHAR";
	case WM_IME_KEYDOWN: return  "WM_IME_KEYDOWN";
	case WM_IME_KEYUP: return  "WM_IME_KEYUP";
//	case WM_MOUSEHOVER: return  "WM_MOUSEHOVER";
//	case WM_MOUSELEAVE: return  "WM_MOUSELEAVE";
	case WM_CUT: return  "WM_CUT";
	case WM_COPY: return  "WM_COPY";
	case WM_PASTE: return  "WM_PASTE";
	case WM_CLEAR: return  "WM_CLEAR";
	case WM_UNDO: return  "WM_UNDO";
	case WM_RENDERFORMAT: return  "WM_RENDERFORMAT";
	case WM_RENDERALLFORMATS: return  "WM_RENDERALLFORMATS";
	case WM_DESTROYCLIPBOARD: return  "WM_DESTROYCLIPBOARD";
	case WM_DRAWCLIPBOARD: return  "WM_DRAWCLIPBOARD";
	case WM_PAINTCLIPBOARD: return  "WM_PAINTCLIPBOARD";
	case WM_VSCROLLCLIPBOARD: return  "WM_VSCROLLCLIPBOARD";
	case WM_SIZECLIPBOARD: return  "WM_SIZECLIPBOARD";
	case WM_ASKCBFORMATNAME: return  "WM_ASKCBFORMATNAME";
	case WM_CHANGECBCHAIN: return  "WM_CHANGECBCHAIN";
	case WM_HSCROLLCLIPBOARD: return  "WM_HSCROLLCLIPBOARD";
	case WM_QUERYNEWPALETTE: return  "WM_QUERYNEWPALETTE";
	case WM_PALETTEISCHANGING: return  "WM_PALETTEISCHANGING";
	case WM_PALETTECHANGED: return  "WM_PALETTECHANGED";
	case WM_HOTKEY: return  "WM_HOTKEY";
	case WM_PRINT: return  "WM_PRINT";
	case WM_PRINTCLIENT: return  "WM_PRINTCLIENT";
	case WM_HANDHELDFIRST: return  "WM_HANDHELDFIRST";
	case WM_HANDHELDLAST: return  "WM_HANDHELDLAST";
	case WM_AFXFIRST: return  "WM_AFXFIRST";
	case WM_AFXLAST: return  "WM_AFXLAST";
	case WM_PENWINFIRST: return  "WM_PENWINFIRST";
	case WM_PENWINLAST: return  "WM_PENWINLAST";
	default: return "WM_UNKNOWN";
	};
}
#endif

// Called from Win32GameEngine::update() between frames to handle deferred resize.
// Idempotent: safe to call when no resize is actually pending (the dimension
// check early-outs). All HUD / shell / cursor fix-ups live here so both the
// WM_SIZE (maximize/restore) and WM_EXITSIZEMOVE (drag) paths converge on
// identical behavior.
void handleDeferredResize()
{
	if (!TheDisplay || !ApplicationHWnd)
		return;

	RECT clientRect;
	if (!GetClientRect(ApplicationHWnd, &clientRect))
		return;
	Int newWidth = clientRect.right - clientRect.left;
	Int newHeight = clientRect.bottom - clientRect.top;

	// Skip 0x0 (minimized) — ResizeBuffers with a zero extent is invalid and
	// there's nothing to adapt the HUD to until the window returns.
	if (newWidth <= 0 || newHeight <= 0)
		return;
	if (newWidth == (Int)TheDisplay->getWidth() && newHeight == (Int)TheDisplay->getHeight())
		return;

	try
	{
		if (!TheDisplay->setDisplayMode(newWidth, newHeight, TheDisplay->getBitDepth(), TRUE))
			return;

		TheWritableGlobalData->m_xResolution = newWidth;
		TheWritableGlobalData->m_yResolution = newHeight;
		if (TheHeaderTemplateManager)
			TheHeaderTemplateManager->onResolutionChanged();
		if (TheMouse)
			TheMouse->onResolutionChanged();

		// TheShell->recreateWindowLayouts() deconstructs the shell and
		// runs the shutdown callback of every pushed screen via
		// popImmediate(). The original ZH path only called it from
		// the main-menu Options dialog, where the shell stack contains
		// only menu screens. When invoked mid-mission (e.g. the user
		// changed resolution from the IN-GAME options menu, or just
		// dragged the window border), the re-entrant deconstruct runs
		// the in-game Options menu's shutdown and tears down enough
		// shell state to end the active mission. Skip the shell
		// rebuild while a real mission is running — the in-game UI
		// rebuild below still updates the control bar / fonts / cursor
		// limits, which is what actually needs to follow the new
		// framebuffer size.
		const bool inRealMission = TheGameLogic
			&& TheGameLogic->isInGame()
			&& !TheGameLogic->isInShellGame();
		if (TheShell && !inRealMission)
			TheShell->recreateWindowLayouts();
		if (TheInGameUI)
		{
			TheInGameUI->recreateControlBar();
			TheInGameUI->refreshCustomUiResources();
		}
	}
	catch (...)
	{
		DEBUG_LOG(("handleDeferredResize: exception during resize to %dx%d", newWidth, newHeight));
	}
}

#ifdef _WIN32
// WndProc ====================================================================
/** Window Procedure - Win32 only (SDL handles window events on other platforms) */
//=============================================================================
LRESULT CALLBACK WndProc( HWND hWnd, UINT message,
													WPARAM wParam, LPARAM lParam )
{

	try
	{
		// First let the IME manager do it's stuff.
		if ( TheIMEManager )
		{
			if ( TheIMEManager->serviceIMEMessage( hWnd, message, wParam, lParam ) )
			{
				// The manager intercepted an IME message so return the result
				return TheIMEManager->result();
			}
		}

#ifdef	DEBUG_WINDOWS_MESSAGES
		static msgCount=0;
		char testString[256];
		sprintf(testString,"\n%d: %s (%X,%X)", msgCount++,messageToString(message), wParam, lParam);
		OutputDebugString(testString);
#endif

		// handle all window messages
		switch( message )
		{
			//-------------------------------------------------------------------------
			case WM_NCHITTEST:
				// Prevent the user from selecting the menu in fullscreen mode
				if( !TheGlobalData->m_windowed )
					return HTCLIENT;
				break;

			//-------------------------------------------------------------------------
			case WM_POWERBROADCAST:
				switch( wParam )
				{
					#ifndef PBT_APMQUERYSUSPEND
						#define PBT_APMQUERYSUSPEND 0x0000
					#endif
					case PBT_APMQUERYSUSPEND:
						// At this point, the app should save any data for open
						// network connections, files, etc., and prepare to go into
						// a suspended mode.
						return TRUE;

					#ifndef PBT_APMRESUMESUSPEND
						#define PBT_APMRESUMESUSPEND 0x0007
					#endif
					case PBT_APMRESUMESUSPEND:
						// At this point, the app should recover any data, network
						// connections, files, etc., and resume running from when
						// the app was suspended.
						return TRUE;
				}
				break;
			//-------------------------------------------------------------------------
			case WM_SYSCOMMAND:
				switch( wParam & 0xFFF0 )
				{
					case SC_KEYMENU:
						// Block the F10/Alt menu activation (pauses the game loop),
						// but allow SC_CLOSE (Alt+F4) to pass through.
						return 0;
					case SC_CLOSE:
						// Let Alt+F4 work - falls through to WM_CLOSE
						break;
					case SC_MOVE:
					case SC_SIZE:
					case SC_MAXIMIZE:
					case SC_MONITORPOWER:
						if( !TheGlobalData->m_windowed )
							return 1;
						break;
				}
				break;

			case WM_QUERYENDSESSION:
				// Allow Windows shutdown/logoff
				return TRUE;

			// ------------------------------------------------------------------------
			case WM_CLOSE:
				// Quit immediately on X button or Alt+F4
				if (TheGameEngine)
				{
					TheGameEngine->setQuitting(TRUE);
				}
				_exit(0);
				return 0;

			//-------------------------------------------------------------------------
			case WM_MOVE:
			{
				if (TheMouse)
					TheMouse->refreshCursorCapture();

				break;
			}

			//-------------------------------------------------------------------------
			case WM_SIZE:
			{
				// When W3D initializes, it resizes the window.  So stop repainting.
				if (!gInitializing)
					gDoPaint = false;

				if (TheMouse)
					TheMouse->refreshCursorCapture();

				// Defer resize to main loop — don't reset D3D device from inside
				// the window proc, as the game loop may be mid-render.
				// SIZE_MINIMIZED is intentionally skipped (0x0 client rect would
				// drive a zero-sized swap chain resize). SIZE_RESTORED covers
				// both maximize-restore and user drag.
				if (!gInitializing && TheGlobalData && TheGlobalData->m_windowed
					&& (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED))
				{
					gPendingResize = TRUE;
				}
				break;
			}

			//-------------------------------------------------------------------------
			case WM_EXITSIZEMOVE:
			{
				// User finished dragging the window edge. Route through the same
				// deferred path as WM_SIZE so the renderer only sees one resize
				// per user action, and so the fix-ups live in a single place
				// (handleDeferredResize) instead of being duplicated here.
				if (TheGlobalData && TheGlobalData->m_windowed)
					gPendingResize = TRUE;
				break;
			}

			// ------------------------------------------------------------------------
			case WM_SETFOCUS:
			{
				//
				// reset the state of our keyboard cause we haven't been paying
				// attention to the keys while focus was away
				//
				if (TheKeyboard)
					TheKeyboard->resetKeys();

				if (TheMouse)
					TheMouse->regainFocus();

				break;
			}

			//-------------------------------------------------------------------------
			case WM_KILLFOCUS:
			{
				if (TheKeyboard)
					TheKeyboard->resetKeys();

				if (TheMouse)
				{
					TheMouse->loseFocus();

					if (TheMouse->isCursorInside())
					{
						TheMouse->onCursorMovedOutside();
					}
				}

				break;
			}

			//-------------------------------------------------------------------------
			case WM_ACTIVATEAPP:
			{
				if ((bool) wParam != isWinMainActive)
				{
					// intended to clear resources on a lost device in fullscreen, but effectively also in
					// windowed mode, if the DXMaximizedWindowedMode shim was applied in newer versions of Windows,
					// which lead to unfortunate application crashing. Resetting the device on WM_ACTIVATEAPP instead
					// of TestCooperativeLevel() == D3DERR_DEVICENOTRESET is not a requirement. There are other code
					// paths that take care of that.

					isWinMainActive = (BOOL) wParam;

					if (TheGameEngine)
						TheGameEngine->setIsActive(isWinMainActive);

					if (isWinMainActive)
					{
						//restore mouse cursor to our custom version.
						if (TheWin32Mouse)
							TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
					}
				}
				return 0;
			}

			//-------------------------------------------------------------------------
			case WM_ACTIVATE:
			{
				Int active = LOWORD( wParam );

				if( active == WA_INACTIVE )
				{
					if (TheAudio)
						TheAudio->muteAudio(AudioManager::MuteAudioReason_WindowFocus);
					if (TheVideoPlayer)
						TheVideoPlayer->loseFocus();
				}
				else
				{
					if (TheAudio)
						TheAudio->unmuteAudio(AudioManager::MuteAudioReason_WindowFocus);
					if (TheVideoPlayer)
						TheVideoPlayer->regainFocus();

					// Cursor can only be captured after one of the activation events.
					if (TheMouse)
						TheMouse->refreshCursorCapture();
				}
				break;
			}

			//-------------------------------------------------------------------------
			case WM_KEYDOWN:
			{
				Int key = (Int)wParam;

				switch( key )
				{
					case VK_ESCAPE:
					{
						PostQuitMessage( 0 );
						break;
					}
				}
				return 0;
			}

			//-------------------------------------------------------------------------
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_LBUTTONDBLCLK:

			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_MBUTTONDBLCLK:

			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_RBUTTONDBLCLK:
			{
				if( TheWin32Mouse )
					TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );

				return 0;
			}

			//-------------------------------------------------------------------------
			case 0x020A: // WM_MOUSEWHEEL
			{
				if( TheWin32Mouse == nullptr )
					return 0;

				long x = (long) LOWORD(lParam);
				long y = (long) HIWORD(lParam);
				RECT rect;

				// ignore when outside of client area
				GetWindowRect( ApplicationHWnd, &rect );
				if( x < rect.left || x > rect.right || y < rect.top || y > rect.bottom )
					return 0;

				TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );
				return 0;
			}

			//-------------------------------------------------------------------------
			case WM_MOUSEMOVE:
			{
				if( TheWin32Mouse == nullptr )
					return 0;

				// ignore when window is not active
				if( !isWinMainActive )
					return 0;

				Int x = (Int)LOWORD( lParam );
				Int y = (Int)HIWORD( lParam );
				RECT rect;

				// ignore when outside of client area
				GetClientRect( ApplicationHWnd, &rect );
				if( x < rect.left || x > rect.right || y < rect.top || y > rect.bottom )
				{
					if ( TheMouse->isCursorInside() )
					{
						TheMouse->onCursorMovedOutside();
					}
					return 0;
				}

				if( !TheMouse->isCursorInside() )
				{
					TheMouse->onCursorMovedInside();
				}

				TheWin32Mouse->addWin32Event( message, wParam, lParam, TheMessageTime );
				return 0;
			}

			//-------------------------------------------------------------------------
			case WM_SETCURSOR:
			{
				if (TheWin32Mouse && (HWND)wParam == ApplicationHWnd)
					TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
				return TRUE;	//tell Windows not to reset mouse cursor image to default.
			}

			case WM_PAINT:
			{
				if (gDoPaint) {
					PAINTSTRUCT paint;
					HDC dc = ::BeginPaint(hWnd, &paint);
					if (gLoadScreenBitmap!=nullptr) {
						Int savContext = ::SaveDC(dc);
						HDC tmpDC = ::CreateCompatibleDC(dc);
						HBITMAP savBitmap = (HBITMAP)::SelectObject(tmpDC, gLoadScreenBitmap);
						::BitBlt(dc, 0, 0, DEFAULT_DISPLAY_WIDTH, DEFAULT_DISPLAY_HEIGHT, tmpDC, 0, 0, SRCCOPY);
						::SelectObject(tmpDC, savBitmap);
						::DeleteDC(tmpDC);
						::RestoreDC(dc, savContext);
					}
					::SetBkMode(dc, TRANSPARENT);
					::SetTextColor(dc, RGB(200, 200, 200));
					HFONT font = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
						DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
					HFONT oldFont = (HFONT)::SelectObject(dc, font);
					::TextOut(dc, 10, 10, "Afshar Koloft Edition - Loading...", 34);
					::SelectObject(dc, oldFont);
					::DeleteObject(font);
					::EndPaint(hWnd, &paint);
					return TRUE;
				}
				break;
			}

			case WM_ERASEBKGND:
				// Always skip erase - D3D renders the entire window, erasing causes flicker
				return TRUE;

// Well, it was a nice idea, but we don't get a message for an ejection.
// (Really unfortunate, actually.) I'm leaving this in in-case some one wants
// to trap a different device change (for instance, removal of a mouse) - jkmcd
#if 0
			case WM_DEVICECHANGE:
			{
				if (((UINT) wParam) == DBT_DEVICEREMOVEPENDING)
				{
					DEV_BROADCAST_HDR *hdr = (DEV_BROADCAST_HDR*) lParam;
					if (!hdr) {
						break;
					}

					if (hdr->dbch_devicetype != DBT_DEVTYP_VOLUME)  {
						break;
					}

					// Lets discuss how Windows is a flaming pile of poo. I'm now casting the header
					// directly into the structure, because its the one I want, and this is just how
					// its done. I hate Windows. - jkmcd
					DEV_BROADCAST_VOLUME *vol = (DEV_BROADCAST_VOLUME*) (hdr);

					return TRUE;
				}
				break;
			}
#endif
		}

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in Main::WndProc... probably should not happen"));
		// no rethrow
	}

//In full-screen mode, only pass these messages onto the default windows handler.
//Appears to fix issues with dual monitor systems but doesn't seem safe?
///@todo: Look into proper support for dual monitor systems.
/*	if (!TheGlobalData->m_windowed)
	switch (message)
	{
		case WM_PAINT:
		case WM_NCCREATE:
		case WM_NCDESTROY:
		case WM_NCCALCSIZE:
		case WM_NCPAINT:
				return DefWindowProc( hWnd, message, wParam, lParam );
	}
	return 0;*/

	return DefWindowProc( hWnd, message, wParam, lParam );

}

// initializeAppWindows =======================================================
/** Register windows class and create application windows. */
//=============================================================================
static Bool initializeAppWindows( HINSTANCE hInstance, Int nCmdShow, Bool runWindowed )
{
	DWORD windowStyle;
	Int startWidth = DEFAULT_DISPLAY_WIDTH,
			startHeight = DEFAULT_DISPLAY_HEIGHT;

	// Honor command-line resolution if specified (-xres, -yres)
	if (TheGlobalData)
	{
		startWidth = TheGlobalData->m_xResolution;
		startHeight = TheGlobalData->m_yResolution;
	}

	// register the window class

  WNDCLASS wndClass = { CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, WndProc, 0, 0, hInstance,
                       LoadIcon (hInstance, MAKEINTRESOURCE(IDI_ApplicationIcon)),
                       nullptr/*LoadCursor(nullptr, IDC_ARROW)*/,
                       (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr,
	                     TEXT("Game Window") };
  RegisterClass( &wndClass );

   // Create our main window
	if (runWindowed) {
		// Proper windowed mode with title bar, close button, and resize frame
		windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	} else {
		// Borderless fullscreen window: covers the entire screen including taskbar.
		// Do NOT use WS_MAXIMIZE here - on Windows 10/11 it clips WS_POPUP windows
		// to the work area (screen minus taskbar) instead of the full screen.
		windowStyle = WS_POPUP | WS_VISIBLE;
	}

	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = startWidth;
	rect.bottom = startHeight;

	if (runWindowed) {
		// Adjust rect so the client area is exactly startWidth x startHeight
		AdjustWindowRect(&rect, windowStyle, FALSE);
	} else {
		// Borderless fullscreen: use the entire screen
		rect.left = 0;
		rect.top = 0;
		rect.right = GetSystemMetrics( SM_CXSCREEN );
		rect.bottom = GetSystemMetrics( SM_CYSCREEN );
	}

	Int winW = rect.right - rect.left;
	Int winH = rect.bottom - rect.top;
	Int posX, posY;

	if (runWindowed) {
		// Center the window on screen
		posX = (GetSystemMetrics( SM_CXSCREEN ) - winW) / 2;
		posY = (GetSystemMetrics( SM_CYSCREEN ) - winH) / 2;
	} else {
		posX = 0;
		posY = 0;
	}

	gInitializing = true;

  HWND hWnd = CreateWindow( TEXT("Game Window"),
                            TEXT("Generals Zero Hour - Afshar Koloft Edition 1"),
                            windowStyle,
                            posX, posY,
                            winW, winH,
                            nullptr,
                            nullptr,
                            hInstance,
                            nullptr );


	if (!runWindowed)
	{	// Borderless fullscreen: position window to cover the entire screen.
		// Use HWND_TOP (not HWND_TOPMOST) so the window doesn't stay on top after alt-tab.
		SetWindowPos(hWnd, HWND_TOP, 0, 0, winW, winH, SWP_FRAMECHANGED);
	}

	SetFocus(hWnd);
	SetForegroundWindow(hWnd);

	if (!runWindowed)
	{	// Use SW_SHOW, not SW_SHOWMAXIMIZED - the latter resizes popup windows
		// to the work area (excluding taskbar) which leaves a gap at the bottom.
		ShowWindow( hWnd, SW_SHOW );
	}
	else
	{
		ShowWindow( hWnd, nCmdShow );
	}
	UpdateWindow( hWnd );

	// save our application window handle for future use
	ApplicationHWnd = hWnd;
	gInitializing = false;
	if (!runWindowed) {
		gDoPaint = false;
	}

	return true;  // success

}
#endif // _WIN32 (WndProc + initializeAppWindows)

#ifdef USE_SDL
// initializeAppWindowsSDL - Create the game window using SDL3.
// The HWND is extracted from SDL for D3D11 rendering on Windows.
static Bool initializeAppWindowsSDL(Bool runWindowed)
{
	Int startWidth = DEFAULT_DISPLAY_WIDTH;
	Int startHeight = DEFAULT_DISPLAY_HEIGHT;

	if (TheGlobalData)
	{
		startWidth = TheGlobalData->m_xResolution;
		startHeight = TheGlobalData->m_yResolution;
	}

	auto& platform = Platform::SDLPlatform::Instance();
	uint32_t sdlBackendFlags = 0;
#ifdef BUILD_WITH_VULKAN
	sdlBackendFlags |= SDL_WINDOW_VULKAN;
#endif
#ifdef BUILD_WITH_VULKAN
	const char* windowTitle = "Generals Zero Hour [Vulkan]";
#else
	const char* windowTitle = "Generals Zero Hour [D3D11]";
#endif
	// When -xres / -yres was passed on the command line, the user explicitly
	// asked for a specific resolution. Open the window in normal (non-
	// maximized) state at that exact size; otherwise SDL_WINDOW_MAXIMIZED
	// would override the requested width/height with the work area / screen
	// dimensions and the user's resolution would silently be ignored.
	const bool startMaximized = !(TheGlobalData && TheGlobalData->m_explicitDisplayResolution);
	if (!platform.Init(startWidth, startHeight, runWindowed != FALSE,
		windowTitle, sdlBackendFlags, startMaximized))
	{
		return FALSE;
	}

	// Extract native HWND for D3D11 rendering and global compatibility
	ApplicationHWnd = (HWND)platform.GetNativeWindowHandle();

	// SDLPlatform::Init normally creates the window MAXIMIZED, so the real
	// client size is the screen work area (bordered) or full screen
	// (borderless), not the resolution we requested. (When -xres/-yres are
	// passed, startMaximized is false above and the actual size matches the
	// requested size.) Either way, push the actual size into TheGlobalData
	// *before* the renderer is created so the swap chain matches the real
	// window and isn't stretched.
	if (TheWritableGlobalData)
	{
		Int actualW = platform.GetWidth();
		Int actualH = platform.GetHeight();
		if (actualW > 0 && actualH > 0)
		{
			TheWritableGlobalData->m_xResolution = actualW;
			TheWritableGlobalData->m_yResolution = actualH;
		}
	}
	return TRUE;
}
#endif

// Necessary to allow memory managers and such to have useful critical sections
static CriticalSection critSec1, critSec2, critSec3, critSec4, critSec5;

#ifdef _WIN32
// UnHandledExceptionFilter ===================================================
/** Handler for unhandled win32 exceptions. */
//=============================================================================
static LONG WINAPI UnHandledExceptionFilter( struct _EXCEPTION_POINTERS* e_info )
{
	DumpExceptionInfo( e_info->ExceptionRecord->ExceptionCode, e_info );
#ifdef RTS_ENABLE_CRASHDUMP
	if (TheMiniDumper && TheMiniDumper->IsInitialized())
	{
		// Create both minimal and full memory dumps
		TheMiniDumper->TriggerMiniDumpForException(e_info, DumpType_Minimal);
		TheMiniDumper->TriggerMiniDumpForException(e_info, DumpType_Full);
	}

	MiniDumper::shutdownMiniDumper();
#endif
	return EXCEPTION_EXECUTE_HANDLER;
}

// WinMain ====================================================================
/** Application entry point */
//=============================================================================
// Vectored exception handler — fires BEFORE the SEH __try/__except chain so
// it sees abort() / __fastfail before they bypass our top-level CrashFilter.
// We only want to capture the FIRST crash and ignore harmless recoverable
// exceptions (e.g., DLL_PROCESS_ATTACH probes, C++ exception unwinding).
// Shared one-shot flag between VectoredCrashHandler and CrashFilter. Without
// this, both handlers would open crash.log with "w" in sequence — the second
// truncating whatever the first wrote, yielding an empty file if the second
// handler itself faulted during its dump. The handler that wins the race
// writes the "CRASH" header with "w"; the other appends supplemental info
// (vectored handler captures the original exception; CrashFilter has access
// to the last-module state / ring buffer).
static LONG g_crashLogFired = 0;

// Strip buffering so every fprintf hits disk. Critical for partial-write
// survival when the filter itself faults or the process is terminated
// before fclose runs.
static void CrashLogUnbuffered(FILE* f) {
	if (f) setvbuf(f, nullptr, _IONBF, 0);
}

// Forward declarations — the stack-overflow handler runs first in the
// vectored chain but refers to globals and helpers defined later in the file.
extern "C" {
    extern char g_lastUpdateModuleName[128];
    extern unsigned g_lastUpdateObjectId;
    extern char g_lastUpdateObjectTemplate[128];
    extern unsigned g_lastUpdateFrame;
    extern const void* g_lastUpdateModulePtr;
    extern const void* g_lastUpdateObjectPtr;
    extern const char* g_lastUpdateModuleClass;
}
static void DumpDispatchRingSEH(FILE* f);  // defined below

// STATUS_STACK_OVERFLOW specifically — the standard crash filter can't run
// because there's no stack left. We reset the stack's guard page (giving us
// a few KB of headroom), write a minimal log identifying the exception as a
// stack overflow, then let the process terminate. Without this the game just
// disappears silently — exactly what was observed with 2 humans vs 6 Brutal AI
// on Death Valley after ~30 k frames.
static LONG WINAPI StackOverflowHandler(EXCEPTION_POINTERS* ep) {
	// _resetstkoflw is the CRT's one-and-only "recover a few KB of stack after
	// a guard-page fault" routine. Without it, subsequent function calls would
	// re-fault and we'd loop in exception dispatch.
#ifdef _MSC_VER
	_resetstkoflw();
#endif
	static LONG fired = 0;
	if (InterlockedExchange(&fired, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;

	char crashLogName[64];
	strcpy(crashLogName, "crash.log");
	FILE* f = fopen(crashLogName, g_crashLogFired ? "a" : "w");
	if (!f) return EXCEPTION_CONTINUE_SEARCH;
	CrashLogUnbuffered(f);
	g_crashLogFired = 1;

	SYSTEMTIME st; GetLocalTime(&st);
	fprintf(f, "=== STACK OVERFLOW %04u-%02u-%02u %02u:%02u:%02u ===\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	fprintf(f, "The main thread blew its stack. This usually means a deep\n"
	           "recursion or a chain of nested callbacks.\n"
	           "RIP=%p\n", ep->ExceptionRecord->ExceptionAddress);

	// The dispatch ring may or may not be intact. Dump what we can.
	if (g_lastUpdateModulePtr) {
		fprintf(f, "LAST UPDATE MODULE class=%s ptr=%p obj=%p id=%u frame=%u\n",
			g_lastUpdateModuleClass ? g_lastUpdateModuleClass : "<unknown>",
			g_lastUpdateModulePtr, g_lastUpdateObjectPtr,
			g_lastUpdateObjectId, g_lastUpdateFrame);
	}
	__try { DumpDispatchRingSEH(f); }
	__except (EXCEPTION_EXECUTE_HANDLER) {
		fprintf(f, "(dispatch ring: resolver faulted)\n");
	}
	fflush(f);
	fclose(f);
	return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI VectoredCrashHandler(EXCEPTION_POINTERS* ep)
{
	const DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (code == EXCEPTION_STACK_OVERFLOW) {
		return StackOverflowHandler(ep);
	}
	// Only react to actually-fatal conditions; ignore the many software
	// exceptions Windows / runtime / drivers raise during normal operation:
	//   0xE06D7363 - C++ throw (caught by language runtime)
	//   0x406D1388 - MS_VC_EXCEPTION (SetThreadName, used by drivers/MSVC)
	//   DBG_PRINTEXCEPTION_* - OutputDebugString
	//   DBG_CONTROL_C - Ctrl+C
	//   EXCEPTION_BREAKPOINT, EXCEPTION_SINGLE_STEP - debugger
	//   0x40010005, 0x80000003, 0x4001000A - assorted debugger probes
	const bool isFatal =
		code == EXCEPTION_ACCESS_VIOLATION       ||
		code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED  ||
		code == EXCEPTION_DATATYPE_MISALIGNMENT  ||
		code == EXCEPTION_FLT_DIVIDE_BY_ZERO     ||
		code == EXCEPTION_FLT_INVALID_OPERATION  ||
		code == EXCEPTION_ILLEGAL_INSTRUCTION    ||
		code == EXCEPTION_IN_PAGE_ERROR          ||
		code == EXCEPTION_INT_DIVIDE_BY_ZERO     ||
		code == EXCEPTION_PRIV_INSTRUCTION       ||
		code == EXCEPTION_STACK_OVERFLOW         ||
		code == EXCEPTION_NONCONTINUABLE_EXCEPTION ||
		code == 0xC0000409 /* STATUS_STACK_BUFFER_OVERRUN / fast-fail */ ||
		code == 0xC0000374 /* STATUS_HEAP_CORRUPTION */;
	if (!isFatal)
		return EXCEPTION_CONTINUE_SEARCH;

	if (InterlockedExchange(&g_crashLogFired, 1) != 0)
		return EXCEPTION_CONTINUE_SEARCH;

	char crashLogName[64];
	if (rts::ClientInstance::getInstanceId() > 1u)
		snprintf(crashLogName, sizeof(crashLogName), "crash_Instance%.2u.log",
			rts::ClientInstance::getInstanceId());
	else
		strcpy(crashLogName, "crash.log");

	FILE* f = fopen(crashLogName, "w");
	if (!f) return EXCEPTION_CONTINUE_SEARCH;
	CrashLogUnbuffered(f);
	SYSTEMTIME st; GetLocalTime(&st);
	fprintf(f, "=== VECTORED CRASH %04u-%02u-%02u %02u:%02u:%02u ===\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	fprintf(f, "VECTORED CRASH code=0x%08X addr=%p flags=0x%X\n",
		code, ep->ExceptionRecord->ExceptionAddress, ep->ExceptionRecord->ExceptionFlags);
	if (ep->ExceptionRecord->NumberParameters > 0) {
		fprintf(f, "  params:");
		for (DWORD i = 0; i < ep->ExceptionRecord->NumberParameters && i < 4; ++i)
			fprintf(f, " [%u]=0x%llX", i, (unsigned long long)ep->ExceptionRecord->ExceptionInformation[i]);
		fprintf(f, "\n");
	}

#ifdef _M_AMD64
	CONTEXT* ctx = ep->ContextRecord;
	fprintf(f, "RIP=%p RSP=%p RBP=%p\n", (void*)ctx->Rip, (void*)ctx->Rsp, (void*)ctx->Rbp);
	fprintf(f, "RAX=%p RCX=%p RDX=%p RBX=%p\n",
		(void*)ctx->Rax, (void*)ctx->Rcx, (void*)ctx->Rdx, (void*)ctx->Rbx);

	HMODULE exeMod = GetModuleHandleA(nullptr);
	fprintf(f, "EXE base=%p\n\nSTACK (RtlVirtualUnwind):\n", (void*)exeMod);

	CONTEXT walk = *ctx;
	for (int frame = 0; frame < 64; ++frame) {
		HMODULE mod = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)walk.Rip, &mod);
		char modName[MAX_PATH] = "?";
		if (mod) GetModuleFileNameA(mod, modName, sizeof(modName));
		const char* base = strrchr(modName, '\\');
		base = base ? base + 1 : modName;
		uintptr_t off = mod ? (uintptr_t)walk.Rip - (uintptr_t)mod : 0;
		fprintf(f, "  #%02d  RIP=%p RSP=%p  %s+0x%llx\n",
			frame, (void*)walk.Rip, (void*)walk.Rsp, base, (unsigned long long)off);

		ULONG64 imgBase = 0;
		PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(walk.Rip, &imgBase, nullptr);
		if (!rf) {
			if (IsBadReadPtr((void*)walk.Rsp, sizeof(void*))) break;
			DWORD64 retAddr = *(DWORD64*)walk.Rsp;
			walk.Rip = retAddr;
			walk.Rsp += sizeof(void*);
			if (!walk.Rip) break;
			continue;
		}
		void* handlerData = nullptr;
		DWORD64 establisherFrame = 0;
		KNONVOLATILE_CONTEXT_POINTERS nvCtx = {};
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, imgBase, walk.Rip, rf, &walk,
			&handlerData, &establisherFrame, &nvCtx);
		if (!walk.Rip) break;
	}
#endif

	fflush(f);
	fclose(f);
	// Continue search so the rest of the runtime sees the exception too.
	return EXCEPTION_CONTINUE_SEARCH;
}

// Last-module tracker — GameLogic::update stamps these before each sleepy
// update call so the crash filter can name the faulty module without having
// to log every one (which was too slow for real-time play).
// As of the sleepy-loop perf fix, the hot path only stamps the pointer
// fields (g_lastUpdateModulePtr / g_lastUpdateObjectPtr / id / frame) — name
// strings are resolved lazily in CrashFilter under SEH so a corrupt name
// table won't crash the crash dumper itself.
extern "C" {
    char g_lastUpdateModuleName[128] = "";
    unsigned g_lastUpdateObjectId   = 0;
    char g_lastUpdateObjectTemplate[128] = "";
    unsigned g_lastUpdateFrame      = 0;
    const void* g_lastUpdateModulePtr = nullptr;
    const void* g_lastUpdateObjectPtr = nullptr;
    const char* g_lastUpdateModuleClass = nullptr;  // typeid(*u).name()

    // Dispatch ring buffer — GameLogic stamps every sleepy u->update() call
    // into one of these 32 slots, wrapping. When a crash happens we dump the
    // newest-to-oldest entries so we can see what the loop was doing just
    // before the fault, even if the fault is inside u->update() itself.
    struct DispatchRingEntry {
        const void* modulePtr;
        const void* objectPtr;
        unsigned    objectId;
        unsigned    frame;
        const char* className;  // typeid(*u).name() — static storage
    };
    enum { DISPATCH_RING_SIZE = 32 };
    DispatchRingEntry g_dispatchRing[DISPATCH_RING_SIZE] = {};
    unsigned g_dispatchRingHead = 0;  // next write slot; (head-1) is newest
}

// ---- Crash-dump helpers ---------------------------------------------------
// All dumpers are SEH-wrapped because the memory we're probing may itself be
// corrupt (that's often why the game crashed in the first place).

// Hex-dump `len` bytes starting at `p`. 16 bytes per line, with ASCII sidecar.
// Protected against bad pointers via VirtualQuery + IsBadReadPtr.
static void DumpBytesSEH(FILE* f, const char* label, const void* p, size_t len) {
	fprintf(f, "\n--- %s @ %p (%zu bytes) ---\n", label, p, len);
	if (!p) { fprintf(f, "  (null)\n"); return; }
	// Validate the entire span before we touch it.
	if (IsBadReadPtr(p, len)) {
		// Partial read — probe page-by-page and dump what we can.
		const unsigned char* q = (const unsigned char*)p;
		size_t done = 0;
		while (done < len) {
			size_t chunk = 16;
			if (done + chunk > len) chunk = len - done;
			if (IsBadReadPtr(q + done, chunk)) {
				fprintf(f, "  +%04zx  <unreadable>\n", done);
				break;
			}
			fprintf(f, "  +%04zx ", done);
			for (size_t i = 0; i < chunk; ++i) fprintf(f, "%02X ", q[done + i]);
			for (size_t i = chunk; i < 16; ++i) fprintf(f, "   ");
			fprintf(f, " ");
			for (size_t i = 0; i < chunk; ++i) {
				unsigned char c = q[done + i];
				fputc(c >= 0x20 && c < 0x7F ? c : '.', f);
			}
			fputc('\n', f);
			done += chunk;
		}
		return;
	}
	const unsigned char* q = (const unsigned char*)p;
	for (size_t off = 0; off < len; off += 16) {
		fprintf(f, "  +%04zx ", off);
		size_t row = (len - off < 16) ? (len - off) : 16;
		for (size_t i = 0; i < row; ++i) fprintf(f, "%02X ", q[off + i]);
		for (size_t i = row; i < 16; ++i) fprintf(f, "   ");
		fprintf(f, " ");
		for (size_t i = 0; i < row; ++i) {
			unsigned char c = q[off + i];
			fputc(c >= 0x20 && c < 0x7F ? c : '.', f);
		}
		fputc('\n', f);
	}
}

// Walks the module's vtable (first 8 slots), prints each slot's address and
// the owning module (.text section) it lies in. If any slot is outside any
// module's .text the module memory is definitely corrupt.
static void DumpVtableSEH(FILE* f, const void* modulePtr) {
	fprintf(f, "\n--- VTABLE of module @ %p ---\n", modulePtr);
	if (!modulePtr || IsBadReadPtr(modulePtr, sizeof(void*))) {
		fprintf(f, "  (unreadable this-pointer)\n");
		return;
	}
	const void* const* vtbl = *(const void* const* const*)modulePtr;
	if (!vtbl || IsBadReadPtr(vtbl, sizeof(void*) * 8)) {
		fprintf(f, "  vtable=%p (unreadable)\n", vtbl);
		return;
	}
	fprintf(f, "  vtable=%p\n", vtbl);
	for (int i = 0; i < 8; ++i) {
		const void* slot = vtbl[i];
		HMODULE mod = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)slot, &mod);
		char modName[MAX_PATH] = "?";
		if (mod) GetModuleFileNameA(mod, modName, sizeof(modName));
		const char* base = strrchr(modName, '\\');
		base = base ? base + 1 : modName;
		uintptr_t off = mod ? (uintptr_t)slot - (uintptr_t)mod : 0;
		fprintf(f, "  vtbl[%d] = %p  %s+0x%llx\n",
			i, slot, base, (unsigned long long)off);
	}
}

// Dump the dispatch ring in newest-to-oldest order. The most recent entry
// (head-1) is typically the module that was running when the crash happened.
static void DumpDispatchRingSEH(FILE* f) {
	fprintf(f, "\n--- DISPATCH RING (newest first, last %d) ---\n", (int)DISPATCH_RING_SIZE);
	unsigned head = g_dispatchRingHead;
	for (int back = 0; back < DISPATCH_RING_SIZE; ++back) {
		unsigned idx = (head - 1 - back) & (DISPATCH_RING_SIZE - 1);
		const DispatchRingEntry& e = g_dispatchRing[idx];
		if (!e.modulePtr && !e.className) continue;
		fprintf(f, "  [-%02d]  frame=%u  class=%s  mod=%p  obj=%p  id=%u\n",
			back,
			e.frame,
			e.className ? e.className : "<null>",
			e.modulePtr,
			e.objectPtr,
			e.objectId);
	}
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
	char crashLogName[64];
	if (rts::ClientInstance::getInstanceId() > 1u)
		snprintf(crashLogName, sizeof(crashLogName), "crash_Instance%.2u.log",
			rts::ClientInstance::getInstanceId());
	else
		strcpy(crashLogName, "crash.log");

	// If VectoredCrashHandler already opened and truncated the log with "w",
	// we must open in append mode or we'll wipe its contents.
	const bool vectoredRanFirst = (g_crashLogFired != 0);
	FILE* f = fopen(crashLogName, vectoredRanFirst ? "a" : "w");
	if (!f) return EXCEPTION_EXECUTE_HANDLER;
	CrashLogUnbuffered(f);

	// Timestamp for correlation across multiple runs.
	SYSTEMTIME st; GetLocalTime(&st);
	if (vectoredRanFirst) {
		fprintf(f, "\n=== CRASH FILTER (follow-up) %04u-%02u-%02u %02u:%02u:%02u ===\n",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	} else {
		g_crashLogFired = 1;  // mark so subsequent handlers append
		fprintf(f, "=== CRASH %04u-%02u-%02u %02u:%02u:%02u ===\n",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	}
	fprintf(f, "CRASH: code=0x%08X addr=%p\n",
		ep->ExceptionRecord->ExceptionCode,
		ep->ExceptionRecord->ExceptionAddress);
	fflush(f);
	// Last sleepy UpdateModule that started executing. The hot path only
	// stamps pointers; resolve names here under SEH so a corrupt module or
	// name table can't crash the crash filter.
	__try {
		if (g_lastUpdateModulePtr) {
			fprintf(f, "LAST UPDATE MODULE class=%s ptr=%p obj=%p id=%u frame=%u\n",
				g_lastUpdateModuleClass ? g_lastUpdateModuleClass : "<unknown>",
				g_lastUpdateModulePtr,
				g_lastUpdateObjectPtr,
				g_lastUpdateObjectId,
				g_lastUpdateFrame);
		}
		if (g_lastUpdateModuleName[0]) {
			fprintf(f, "LAST NAMED MODULE:  %s  on obj=%u (%s)  frame=%u\n",
				g_lastUpdateModuleName,
				g_lastUpdateObjectId,
				g_lastUpdateObjectTemplate,
				g_lastUpdateFrame);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		fprintf(f, "LAST UPDATE MODULE: (unavailable — resolver faulted)\n");
	}
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
	    ep->ExceptionRecord->NumberParameters >= 2) {
		// ExceptionInformation[0]: 0=READ, 1=WRITE, 8=DEP (execute NX page).
		const ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
		const char* kindStr = kind == 0 ? "READ"
		                    : kind == 1 ? "WRITE"
		                    : kind == 8 ? "EXEC(DEP)"
		                                : "?";
		fprintf(f, "  access %s addr=%p\n",
			kindStr,
			(void*)ep->ExceptionRecord->ExceptionInformation[1]);
	}

#ifdef _M_AMD64
	CONTEXT* ctx = ep->ContextRecord;
	fprintf(f, "RIP=%p RSP=%p RBP=%p\n", (void*)ctx->Rip, (void*)ctx->Rsp, (void*)ctx->Rbp);
	fprintf(f, "RAX=%p RBX=%p RCX=%p RDX=%p\n",
		(void*)ctx->Rax, (void*)ctx->Rbx, (void*)ctx->Rcx, (void*)ctx->Rdx);
	fprintf(f, "R8 =%p R9 =%p R10=%p R11=%p\n",
		(void*)ctx->R8, (void*)ctx->R9, (void*)ctx->R10, (void*)ctx->R11);
	fprintf(f, "RSI=%p RDI=%p R12=%p R13=%p R14=%p R15=%p\n",
		(void*)ctx->Rsi, (void*)ctx->Rdi,
		(void*)ctx->R12, (void*)ctx->R13, (void*)ctx->R14, (void*)ctx->R15);

	// Walk the stack via x64 RtlVirtualUnwind. This works because every x64
	// function has unwind info, so we don't need RBP-chained frame pointers.
	HMODULE exeMod = GetModuleHandleA(nullptr);
	fprintf(f, "EXE base=%p\n\nCALL STACK (RIP -> base):\n", (void*)exeMod);

	CONTEXT walk = *ctx;
	for (int frame = 0; frame < 64; ++frame) {
		HMODULE mod = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)walk.Rip, &mod);
		char modName[MAX_PATH] = "?";
		if (mod) GetModuleFileNameA(mod, modName, sizeof(modName));
		const char* base = strrchr(modName, '\\');
		base = base ? base + 1 : modName;
		uintptr_t off = mod ? (uintptr_t)walk.Rip - (uintptr_t)mod : 0;
		fprintf(f, "  #%02d  %p  %s+0x%llx\n", frame, (void*)walk.Rip, base,
			(unsigned long long)off);

		ULONG64 imgBase = 0;
		PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(walk.Rip, &imgBase, nullptr);
		if (!rf) {
			// Leaf function with no unwind info; try to step out via RSP.
			if (IsBadReadPtr((void*)walk.Rsp, sizeof(void*))) break;
			walk.Rip = *(DWORD64*)walk.Rsp;
			walk.Rsp += sizeof(void*);
			if (!walk.Rip) break;
			continue;
		}
		void* handlerData = nullptr;
		DWORD64 establisherFrame = 0;
		KNONVOLATILE_CONTEXT_POINTERS nvCtx = {};
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, imgBase, walk.Rip, rf, &walk,
			&handlerData, &establisherFrame, &nvCtx);
		if (!walk.Rip) break;
	}

	// --- Memory context dumps --------------------------------------------
	// Everything below is SEH-guarded. If any individual dumper faults, we
	// skip it and continue to the next one — can't let crash reporting crash
	// the crash reporter.

	// Code bytes at RIP — shows the exact instruction that faulted.
	__try {
		DumpBytesSEH(f, "CODE AROUND RIP (64 bytes, RIP-16 .. RIP+48)",
			(const void*)((uintptr_t)ctx->Rip - 16), 64);
	} __except (EXCEPTION_EXECUTE_HANDLER) {}

	// Faulting memory address — the thing being read/written that caused the AV.
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
	    ep->ExceptionRecord->NumberParameters >= 2) {
		uintptr_t fault = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
		__try {
			// Dump a small window around the faulting address — bounded so we
			// don't read off a page boundary into unmapped memory.
			const uintptr_t start = fault >= 32 ? fault - 32 : 0;
			DumpBytesSEH(f, "MEMORY AT FAULT ADDRESS (-32..+64)",
				(const void*)start, 96);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// Module + object state at the time of the crash.
	__try {
		if (g_lastUpdateModulePtr) {
			DumpBytesSEH(f, "MODULE memory (128 bytes from this-ptr)",
				g_lastUpdateModulePtr, 128);
			DumpVtableSEH(f, g_lastUpdateModulePtr);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {}

	__try {
		if (g_lastUpdateObjectPtr) {
			DumpBytesSEH(f, "OBJECT memory (256 bytes from this-ptr)",
				g_lastUpdateObjectPtr, 256);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {}

	// 512 bytes of stack around RSP — lets a reader spot return addresses
	// and locals the stack walker may have skipped.
	__try {
		DumpBytesSEH(f, "STACK BYTES (RSP .. RSP+512)",
			(const void*)ctx->Rsp, 512);
	} __except (EXCEPTION_EXECUTE_HANDLER) {}

	// Dispatch ring — shows the last 32 modules the sleepy loop executed.
	__try {
		DumpDispatchRingSEH(f);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		fprintf(f, "(dispatch ring: resolver faulted)\n");
	}
#endif

	fprintf(f, "\n=== end of crash log ===\n");
	fflush(f);
	fclose(f);
	return EXCEPTION_EXECUTE_HANDLER;
}
// atexit hook — fires when the process exits CLEANLY (exit(), return from main).
// Game-over / defeat / scripted-quit paths can call exit() without triggering
// SEH, so without this hook a crash-on-cleanup leaves no log at all. We append
// to the same crash.log so it's always the single source of truth.
static void AtExitDumpState(void) {
	// If a real crash already captured state, don't clobber it.
	if (g_crashLogFired) return;
	char crashLogName[64];
	if (rts::ClientInstance::getInstanceId() > 1u)
		snprintf(crashLogName, sizeof(crashLogName), "crash_Instance%.2u.log",
			rts::ClientInstance::getInstanceId());
	else
		strcpy(crashLogName, "crash.log");
	FILE* f = fopen(crashLogName, "w");
	if (!f) return;
	CrashLogUnbuffered(f);
	SYSTEMTIME st; GetLocalTime(&st);
	fprintf(f, "=== ATEXIT (clean shutdown) %04u-%02u-%02u %02u:%02u:%02u ===\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	// Even a clean-looking exit() can be triggered by a fatal code path (defeat
	// screen double-free, etc.) — dump the last sleepy-loop state so we have
	// something to triage against if game feels wrong afterwards.
	if (g_lastUpdateModulePtr) {
		fprintf(f, "LAST UPDATE MODULE class=%s ptr=%p obj=%p id=%u frame=%u\n",
			g_lastUpdateModuleClass ? g_lastUpdateModuleClass : "<unknown>",
			g_lastUpdateModulePtr,
			g_lastUpdateObjectPtr,
			g_lastUpdateObjectId,
			g_lastUpdateFrame);
	}
	__try { DumpDispatchRingSEH(f); }
	__except(EXCEPTION_EXECUTE_HANDLER) {
		fprintf(f, "(dispatch ring: resolver faulted on exit)\n");
	}
	fflush(f);
	fclose(f);
}

// Handler for abort() — typically called from assertion failures or __fastfail.
// The C runtime raises SIGABRT *after* uninstalling its default abort handler,
// so a signal hook is our only hope of logging.
static void OnSigAbrt(int sig) {
	(void)sig;
	// Reuse the vectored path by logging directly. Guard against re-entrance.
	static LONG fired = 0;
	if (InterlockedExchange(&fired, 1) != 0) return;
	char name[64];
	strcpy(name, "crash.log");
	FILE* f = fopen(name, g_crashLogFired ? "a" : "w");
	if (!f) return;
	CrashLogUnbuffered(f);
	g_crashLogFired = 1;
	SYSTEMTIME st; GetLocalTime(&st);
	fprintf(f, "=== SIGABRT %04u-%02u-%02u %02u:%02u:%02u ===\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	if (g_lastUpdateModulePtr) {
		fprintf(f, "LAST UPDATE MODULE class=%s ptr=%p obj=%p id=%u frame=%u\n",
			g_lastUpdateModuleClass ? g_lastUpdateModuleClass : "<unknown>",
			g_lastUpdateModulePtr, g_lastUpdateObjectPtr,
			g_lastUpdateObjectId, g_lastUpdateFrame);
	}
	__try { DumpDispatchRingSEH(f); }
	__except(EXCEPTION_EXECUTE_HANDLER) {}
	fflush(f);
	fclose(f);
}

// Centralized hook installer — called from every entry point.
static void InstallCrashHooks() {
	static LONG installed = 0;
	if (InterlockedExchange(&installed, 1) != 0) return;
	// Reserve 64 KB of committed stack for the SO handler. Without this,
	// _resetstkoflw() only gives us one 4 KB page and fprintf's internal
	// buffers blow through it immediately (observed: the handler itself
	// faulted in ucrtbase _vfprintf_l while logging the original overflow).
	{
		ULONG guarantee = 64 * 1024;
		SetThreadStackGuarantee(&guarantee);
	}
	AddVectoredExceptionHandler(1, VectoredCrashHandler);
	SetUnhandledExceptionFilter(CrashFilter);
	atexit(AtExitDumpState);
	signal(SIGABRT, OnSigAbrt);
	std::set_terminate([]() {
		OnSigAbrt(0); // reuse the dump path
		std::abort();  // preserves process-exit semantics
	});
}

#endif // _WIN32 (exception handlers)

// StartupTraceC is intentionally a no-op in shipping builds.  It was used
// during the shell-map hang investigation to write timestamped messages to
// D:\startup_trace.log; leaving the symbol in place avoids ripping out every
// call site now that the hang is fixed.
extern "C" void __stdcall StartupTraceC(const char* msg) { (void)msg; }

// Common game initialization shared by both WinMain and SDL main.
// Returns the exit code.
static Int GameStartup()
{
	Int exitcode = 1;

#ifdef RTS_PROFILE
  Profile::StartRange("init");
#endif

	try {

#ifdef _WIN32
		// On x64 the legacy UnHandledExceptionFilter (StackDump.cpp) is a no-op,
		// so we keep CrashFilter active for x64 builds. CrashFilter walks the
		// stack with RtlVirtualUnwind and writes crash.log.
#if defined(_M_IX86)
		SetUnhandledExceptionFilter( UnHandledExceptionFilter );
#endif
#endif

		TheAsciiStringCriticalSection = &critSec1;
		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		// initialize the memory manager early
		initMemoryManager();

		// Set working directory to the executable's directory
#ifdef _WIN32
		{
			Char buffer[ _MAX_PATH ];
			GetModuleFileName( nullptr, buffer, sizeof( buffer ) );
			if (Char *pEnd = strrchr(buffer, '\\'))
				*pEnd = 0;
			::SetCurrentDirectory(buffer);
		}
#elif defined(USE_SDL)
		{
			char* basePath = SDL_GetBasePath();
			if (basePath)
			{
				chdir(basePath);
				SDL_free(basePath);
			}
		}
#endif

#if defined(RTS_DEBUG) && defined(_MSC_VER)
		{
			int tmpFlag = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
			tmpFlag |= (_CRTDBG_LEAK_CHECK_DF|_CRTDBG_ALLOC_MEM_DF);
			tmpFlag &= ~_CRTDBG_CHECK_CRT_DF;
			_CrtSetDbgFlag( tmpFlag );
		}
#endif

		// Skip launcher dialog - go straight to game.
		// In Release builds DEBUG_LOGGING is not defined, so the DebugLog
		// macros are no-ops; opening the log file or the on-screen log console
		// would be wasted work that nothing writes to. Gate both on the same
		// preprocessor flag.
#ifdef DEBUG_LOGGING
		{
			extern Bool g_enableLogFile;
			extern Bool g_enableLogConsole;
			g_enableLogFile = TRUE;
			g_enableLogConsole = TRUE;

			if (g_enableLogConsole)
			{
				extern void initLogConsole();
				initLogConsole();
			}

			// If logging was disabled, close any log file that DebugInit opened early
			if (!g_enableLogFile)
			{
				extern void DebugCloseLogFile();
				DebugCloseLogFile();
			}
		}
#endif

// Windows-only splash bitmap loading
#ifdef _WIN32
#if defined(RTS_DEBUG) || defined RTS_PROFILE
		{
			char filePath[_MAX_PATH];
			const char *fileName = "Install_Final.bmp";
			static const char *localizedPathFormat = "Data/%s/";
			sprintf(filePath,localizedPathFormat, GetRegistryLanguage().str());
			strlcat(filePath, fileName, ARRAY_SIZE(filePath));
			FILE *fileImage = fopen(filePath, "r");
			if (fileImage) {
				fclose(fileImage);
				gLoadScreenBitmap = (HBITMAP)LoadImage(ApplicationHInstance, filePath, IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE);
			}
			else {
				gLoadScreenBitmap = (HBITMAP)LoadImage(ApplicationHInstance, fileName, IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE);
			}
		}
#else
		gLoadScreenBitmap = (HBITMAP)LoadImage(ApplicationHInstance, "Install_Final.bmp", IMAGE_BITMAP, 0, 0, LR_SHARED|LR_LOADFROMFILE);
#endif
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif // _WIN32

		CommandLine::parseCommandLineForStartup();


#ifdef RTS_ENABLE_CRASHDUMP
		// Initialize minidump facilities - requires TheGlobalData so performed after parseCommandLineForStartup
		MiniDumper::initMiniDumper(TheGlobalData->getPath_UserData());
#endif

		// Read windowed preference from Options.ini before creating the window,
		// so the window starts in the correct mode immediately.
		{
			FILE *fp = fopen("Options.ini", "r");
			if (!fp)
			{
				// Try user data directory
				char path[1024];
#ifdef _WIN32
				char *userHome = getenv("USERPROFILE");
				if (userHome)
					snprintf(path, sizeof(path), "%s\\Documents\\Command and Conquer Generals Zero Hour Data\\Options.ini", userHome);
#else
				char *userHome = getenv("HOME");
				if (userHome)
					snprintf(path, sizeof(path), "%s/Library/Application Support/Generals Zero Hour/Options.ini", userHome);
#endif
				if (userHome)
					fp = fopen(path, "r");
			}
			if (fp)
			{
				char line[256];
				while (fgets(line, sizeof(line), fp))
				{
					char *eq = strchr(line, '=');
					if (eq)
					{
						*eq = 0;
						char *key = line;
						char *val = eq + 1;
						while (*key == ' ') key++;
						while (*val == ' ') val++;
						char *end = key + strlen(key) - 1;
						while (end > key && *end == ' ') { *end = 0; end--; }
						end = val + strlen(val) - 1;
						while (end > val && (*end == ' ' || *end == '\r' || *end == '\n')) { *end = 0; end--; }

#ifdef _WIN32
						if (stricmp(key, "Windowed") == 0)
							TheWritableGlobalData->m_windowed = (stricmp(val, "yes") == 0);
#else
						if (strcasecmp(key, "Windowed") == 0)
							TheWritableGlobalData->m_windowed = (strcasecmp(val, "yes") == 0);
#endif
					}
				}
				fclose(fp);
			}
		}

#ifdef _WIN32
		// First-run AI video enhancement (optional, requires Real-ESRGAN tools)
		if (!TheGlobalData->m_headless)
		{
			extern bool VideoEnhancer_TryEnhance(HINSTANCE hInstance);
			VideoEnhancer_TryEnhance(ApplicationHInstance);
		}
#endif

		// Create the application window
#ifdef USE_SDL
		if(!TheGlobalData->m_headless && initializeAppWindowsSDL(TheGlobalData->m_windowed) == false)
		{
			return exitcode;
		}
#elif defined(_WIN32)
		if(!TheGlobalData->m_headless && initializeAppWindows(ApplicationHInstance, SW_SHOW, TheGlobalData->m_windowed) == false)
		{
			return exitcode;
		}
#endif

#ifdef _WIN32
		if (gLoadScreenBitmap!=nullptr) {
			::DeleteObject(gLoadScreenBitmap);
			gLoadScreenBitmap = nullptr;
		}
#endif

		// Set up version info
		TheVersion = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));

		if (!rts::ClientInstance::initialize())
		{
#ifdef _WIN32
			HWND ccwindow = FindWindow(rts::ClientInstance::getFirstInstanceName(), nullptr);
			if (ccwindow)
			{
				SetForegroundWindow(ccwindow);
				ShowWindow(ccwindow, SW_RESTORE);
			}
#endif
			DEBUG_LOG(("Generals is already running...Bail!"));
			delete TheVersion;
			TheVersion = nullptr;
			shutdownMemoryManager();
			return exitcode;
		}
		DEBUG_LOG(("Create Generals Mutex okay."));

		DEBUG_LOG(("CRC message is %d", GameMessage::MSG_LOGIC_CRC));

		// run the game main loop
		exitcode = GameMain();

		delete TheVersion;
		TheVersion = nullptr;

	#ifdef MEMORYPOOL_DEBUG
		TheMemoryPoolFactory->debugMemoryReport(REPORT_POOLINFO | REPORT_POOL_OVERFLOW | REPORT_SIMPLE_LEAKS, 0, 0);
	#endif
	#if defined(RTS_DEBUG)
		TheMemoryPoolFactory->memoryPoolUsageReport("AAAMemStats");
	#endif

		shutdownMemoryManager();

		// BGC - shut down COM
	//	OleUninitialize();
	}
	catch (...)
	{

	}

#ifdef RTS_ENABLE_CRASHDUMP
	MiniDumper::shutdownMiniDumper();
#endif
	TheAsciiStringCriticalSection = nullptr;
	TheUnicodeStringCriticalSection = nullptr;
	TheDmaCriticalSection = nullptr;
	TheMemoryPoolCriticalSection = nullptr;
	TheDebugLogCriticalSection = nullptr;

	return exitcode;

}

// --- Entry Points ---

#if defined(_WIN32) && !defined(USE_SDL)
// Windows-only entry point (no SDL). Sets up DPI awareness, crash filters.
Int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine, Int nCmdShow )
{
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nCmdShow;

	InstallCrashHooks();

	// DPI awareness for proper physical pixel sizes
	{
		HMODULE user32 = GetModuleHandleA("user32.dll");
		typedef BOOL(WINAPI* SetDPIAwarenessCtxFunc)(void*);
		auto fnCtx = (SetDPIAwarenessCtxFunc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (fnCtx)
			fnCtx((void*)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
	}

	ApplicationHInstance = hInstance;
	return GameStartup();
}

#else
// Portable entry point (SDL or macOS/Linux).
// On Windows with USE_SDL, SDL3's SDL_main.h provides a WinMain trampoline
// that calls this main(), handling DPI awareness and subsystem init.
#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	// Force unbuffered stderr/stdout so stage-3 diagnostics interleave in
	// true chronological order even when redirected to a file.
	setvbuf(stderr, nullptr, _IONBF, 0);
	setvbuf(stdout, nullptr, _IONBF, 0);

#ifdef _WIN32
	// Get HINSTANCE for Win32 APIs that still need it
	ApplicationHInstance = GetModuleHandle(nullptr);

	// Install vectored + unhandled + atexit + SIGABRT + set_terminate together.
	// Vectored runs before SEH frames and catches abort() / fastfail / heap
	// corruption that the top-level UnhandledExceptionFilter never sees.
	InstallCrashHooks();

	// DPI awareness
	{
		HMODULE user32 = GetModuleHandleA("user32.dll");
		typedef BOOL(WINAPI* SetDPIAwarenessCtxFunc)(void*);
		auto fnCtx = (SetDPIAwarenessCtxFunc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (fnCtx)
			fnCtx((void*)-4);
	}
#endif

	return GameStartup();
}
#endif

// CreateGameEngine ===========================================================
/** Create the game engine */
//=============================================================================
GameEngine *CreateGameEngine()
{
#ifdef USE_SDL
	SDLGameEngine *engine;
	engine = NEW SDLGameEngine;
#else
	Win32GameEngine *engine;
	engine = NEW Win32GameEngine;
#endif
	//game engine may not have existed when app got focus so make sure it
	//knows about current focus state.
	engine->setIsActive(isWinMainActive);

	return engine;

}
