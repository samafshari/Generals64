/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// SearchPaths.h /////////////////////////////////////////////////////////
//
// Asset search-path resolver. Reads `paths.txt` from next to the
// executable on first use and exposes the resulting ordered list to the
// rest of the engine. Top of the file = highest priority. Used by both
// the BIG archive loader (Win32BIGFileSystem) to decide which directories
// to scan for `.big` files and by the loose-file system
// (Win32LocalFileSystem) to resolve relative read paths against the same
// priority order.
//
// The point of the system is to let us ship a thin GameData/ override
// folder on top of a stock Zero Hour install (Steam, Origin, retail) so
// the user only needs the files we actually changed/added. The bottom
// entry of paths.txt is normally the original game install; the top
// entry is `.` (the directory paths.txt itself lives in).
//
//////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>


namespace SearchPaths
{
	// Append a directory passed via the command line (`-path "..."`).
	// As soon as one of these is registered, the next Init()/EnsureInit()
	// pass uses ONLY the command-line list and ignores `paths.txt` —
	// the launcher is then the single source of truth for where game data
	// lives. Order is preserved (first call = highest priority). Safe to
	// call multiple times before the engine touches any file system.
	void AddCommandLinePath(const char* absPath);

	// Read-only access to the raw `-path` arguments registered via
	// AddCommandLinePath, in the order they were passed. Used by the
	// inspector's Launch Parameters panel so the developer can see what
	// the launcher actually handed the engine vs. what ended up in
	// the resolved Get() list.
	const std::vector<std::string>& GetCommandLinePaths();

	// Force the search-path list to be (re)read now. If any
	// AddCommandLinePath() entries exist, those are used; otherwise the
	// list is read from <exeDir>/paths.txt. Safe to call repeatedly.
	// The first call to Get() also triggers init.
	void Init();

	// Ordered list of resolved search-path directories, top-priority first.
	// Each entry is an absolute path with no trailing separator. Returns at
	// least one entry — if `paths.txt` is missing or empty, the executable
	// directory is used as the sole fallback so the engine still functions.
	const std::vector<std::string>& Get();

	// Try every search path in priority order; return the first
	// `<searchPath>/relPath` that actually exists on disk. Returns the
	// empty string if none of them contain the file. Absolute paths are
	// returned unchanged when they exist, otherwise empty.
	//
	// `relPath` is taken verbatim — callers shouldn't pre-prepend any of
	// the search paths. Both forward and backward slashes are accepted.
	std::string Resolve(const char* relPath);

	// Same as Resolve(), but only checks for existence — useful when the
	// caller will open the file itself via the legacy fopen() path and
	// just wants to know whether the search-path resolution succeeded.
	bool Exists(const char* relPath);
}
