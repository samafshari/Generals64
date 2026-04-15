// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

#include "always.h"


// This class temporarily loads and unloads dbghelp.dll from the desired location to prevent
// other code from potentially loading it from an undesired location.
// This helps avoid crashing on boot using recent AMD/ATI drivers, which attempt to load and use
// dbghelp.dll from the game install directory but are unable to do so without crashing because
// the dbghelp.dll that ships with the game is very old and the AMD/ATI code does not handle
// that correctly.

class DbgHelpGuard
{
public:

	DbgHelpGuard();
	~DbgHelpGuard();

	void activate();
	void deactivate();

private:

	bool m_needsUnload;
};
