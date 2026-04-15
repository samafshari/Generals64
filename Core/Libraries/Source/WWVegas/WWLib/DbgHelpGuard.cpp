// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#include "DbgHelpGuard.h"

#include "DbgHelpLoader.h"


DbgHelpGuard::DbgHelpGuard()
	: m_needsUnload(false)
{
	activate();
}

DbgHelpGuard::~DbgHelpGuard()
{
	deactivate();
}

void DbgHelpGuard::activate()
{
	// Front load the DLL now to prevent other code from loading the potentially wrong DLL.
	DbgHelpLoader::load();
	m_needsUnload = true;
}

void DbgHelpGuard::deactivate()
{
	if (m_needsUnload)
	{
		DbgHelpLoader::unload();
		m_needsUnload = false;
	}
}
