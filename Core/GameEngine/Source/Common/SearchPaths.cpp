/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "PreRTS.h"

#include "Common/SearchPaths.h"

#include "Inspector/Inspector.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace
{
	// Lazy single-init, populated by EnsureInit() on first access. Reads
	// happen at startup before any threads exist, so no locking is needed.
	std::vector<std::string> s_paths;
	bool s_initialized = false;

	// Command-line overrides registered via SearchPaths::AddCommandLinePath
	// (driven by `-path "..."` flags). When this list is non-empty,
	// EnsureInit() uses ONLY these entries and skips paths.txt entirely —
	// the launcher becomes the single source of truth. Stored as raw,
	// unresolved strings; the same ResolveEntry/AddSearchPathWithImplicit
	// pipeline that paths.txt uses runs over them at init time so behavior
	// stays identical (relative-to-exe resolution + ZH_Generals fallback).
	std::vector<std::string> s_commandLinePaths;

	// Resolve cache: query string (lowercased + forward-slashes) ->
	// absolute path of the first search-path entry that contains the
	// file, or empty string if the query is known-absent. This is
	// Rule 2 from the design: once a data asset has been discovered,
	// every subsequent lookup for the same query returns the cached
	// absolute path instead of re-walking every search path. The
	// cache is cleared on Init() so hot-reloading paths.txt (via
	// SearchPaths::Init) gives a clean slate. Thread-safe.
	std::unordered_map<std::string, std::string> s_resolveCache;
	std::mutex s_resolveCacheMutex;

	// Normalize a query string into a stable cache key: lowercase on
	// Windows, forward slashes everywhere. The file system itself is
	// case-insensitive on Windows, so two queries differing only in
	// case should hit the same cache entry.
	std::string MakeCacheKey(const char* relPath)
	{
		std::string k = relPath;
		for (char& c : k)
		{
			if (c == '\\') c = '/';
#ifdef _WIN32
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
		}
		return k;
	}

	fs::path GetExecutableDir()
	{
#ifdef _WIN32
		wchar_t buf[MAX_PATH];
		DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
		if (n == 0 || n >= MAX_PATH)
		{
			std::error_code ec;
			return fs::current_path(ec);
		}
		return fs::path(buf).parent_path();
#elif defined(__APPLE__)
		char buf[PATH_MAX];
		uint32_t size = sizeof(buf);
		if (_NSGetExecutablePath(buf, &size) != 0)
		{
			std::error_code ec;
			return fs::current_path(ec);
		}
		return fs::path(buf).parent_path();
#elif defined(__linux__)
		char buf[PATH_MAX];
		ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (n <= 0)
		{
			std::error_code ec;
			return fs::current_path(ec);
		}
		buf[n] = 0;
		return fs::path(buf).parent_path();
#else
		std::error_code ec;
		return fs::current_path(ec);
#endif
	}

	// Strip leading/trailing whitespace (including \r so CRLF files work).
	std::string Trim(const std::string& s)
	{
		const char* ws = " \t\r\n";
		const size_t a = s.find_first_not_of(ws);
		if (a == std::string::npos)
			return std::string();
		const size_t b = s.find_last_not_of(ws);
		return s.substr(a, b - a + 1);
	}

	// Resolve one paths.txt entry against the executable directory.
	// `.` (and variants) maps to exeDir; relative paths join exeDir;
	// absolute paths pass through. Returns empty on parse failure.
	std::string ResolveEntry(const std::string& raw, const fs::path& exeDir)
	{
		if (raw == "." || raw == "./" || raw == ".\\")
			return exeDir.string();

		fs::path p(raw);
		if (!p.is_absolute())
			p = exeDir / p;

		std::error_code ec;
		fs::path canonical = fs::weakly_canonical(p, ec);
		if (ec || canonical.empty())
			canonical = p; // accept as-is even if it doesn't exist yet
		return canonical.string();
	}

	// Normalize a resolved directory string for dedup + storage: strip any
	// trailing slash/backslash and pass through the OS path. Returns the
	// input unchanged if it's already normalized.
	std::string NormalizeResolved(std::string s)
	{
		while (!s.empty() && (s.back() == '/' || s.back() == '\\'))
			s.pop_back();
		return s;
	}

	// Case-insensitive dedup on Windows (paths are case-insensitive),
	// case-sensitive elsewhere.
	bool PathsEqual(const std::string& a, const std::string& b)
	{
#ifdef _WIN32
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			char ca = a[i];
			char cb = b[i];
			if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
			if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
			// Treat / and \ as equivalent for dedup purposes.
			if (ca == '\\') ca = '/';
			if (cb == '\\') cb = '/';
			if (ca != cb)
				return false;
		}
		return true;
#else
		return a == b;
#endif
	}

	// Add a resolved search-path entry plus any implicit `ZH_Generals`
	// subfolder directly beneath it. The parent entry goes in first, then
	// the ZH_Generals entry is appended right after so it acts as the
	// next-lower-priority fallback — matching the original game where
	// Zero Hour overrides base Generals content. Both entries are deduped
	// against whatever is already in s_paths so users listing the same
	// directory twice (or listing ZH_Generals explicitly AND its parent)
	// don't get duplicate scans.
	void AddSearchPathWithImplicit(const std::string& resolvedRaw)
	{
		const std::string resolved = NormalizeResolved(resolvedRaw);
		if (resolved.empty())
			return;

		// Dedup against what we already have.
		auto alreadyHave = [&](const std::string& candidate) {
			for (const std::string& existing : s_paths)
				if (PathsEqual(existing, candidate))
					return true;
			return false;
		};

		if (!alreadyHave(resolved))
			s_paths.push_back(resolved);

		// Auto-detect a `ZH_Generals` subfolder (original Generals BIGs
		// that Steam ships alongside the Zero Hour expansion). If the
		// parent path already resolves TO a `ZH_Generals` folder, skip
		// the nested check — we don't want to recurse into a hypothetical
		// `ZH_Generals/ZH_Generals/`.
		fs::path parent(resolved);
		std::string leaf = parent.filename().string();
		for (char& c : leaf)
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (leaf == "zh_generals")
			return;

		std::error_code ec;
		fs::path zhGen = parent / "ZH_Generals";
		if (fs::is_directory(zhGen, ec))
		{
			std::string zhStr = NormalizeResolved(zhGen.string());
			if (!zhStr.empty() && !alreadyHave(zhStr))
				s_paths.push_back(zhStr);
		}
	}

	// Show a Windows-native error dialog and exit. Used when the
	// user double-clicks the exe with no command-line and no
	// paths.txt next to it — without game-data search paths the
	// very first INI load throws an unhelpful "Uncaught Exception
	// during initialization." crash. Surface a real error instead.
	[[noreturn]] void FatalNoSearchPaths(const fs::path& exeDir, const fs::path& pathsTxt)
	{
#ifdef _WIN32
		std::string body =
			"No game data search paths were configured.\n\n"
			"This launcher build looks for game data in two ways:\n"
			"\n"
			"  1. Command-line arguments: -path \"C:\\path\\to\\game\"\n"
			"  2. A paths.txt file next to the executable\n"
			"\n"
			"Neither was found. Either run the game through the\n"
			"Discombobulator launcher (which sets up search paths\n"
			"automatically), or create a paths.txt next to:\n"
			"\n"
			"  ";
		body += exeDir.string();
		body += "\n\nwith one search path per line, e.g.:\n\n"
			"  .\n"
			"  C:\\Program Files (x86)\\Steam\\steamapps\\common\\Command & Conquer Generals - Zero Hour";

		::MessageBoxA(nullptr, body.c_str(),
			"Generals: Game Data Not Found",
			MB_OK | MB_ICONERROR);
#else
		(void)exeDir;
		(void)pathsTxt;
#endif
		// Hard exit so the engine never tries to initialise INI
		// subsystems against an empty search-path list.
		std::exit(1);
	}

	void EnsureInit()
	{
		if (s_initialized)
			return;
		s_initialized = true;

		const fs::path exeDir = GetExecutableDir();
		const fs::path pathsTxt = exeDir / "paths.txt";

		// Command-line overrides win outright. The launcher passes `-path`
		// once per search-path entry (with the exe's own directory first),
		// so when we see any entries here we skip paths.txt entirely. This
		// keeps the launcher as the single source of truth and avoids the
		// "stale paths.txt next to the exe shadows what the launcher just
		// asked for" failure mode.
		bool pathsTxtOpened = false;
		if (!s_commandLinePaths.empty())
		{
			for (const std::string& raw : s_commandLinePaths)
			{
				std::string resolved = ResolveEntry(raw, exeDir);
				if (resolved.empty())
					continue;
				AddSearchPathWithImplicit(resolved);
			}
		}
		else
		{
			std::ifstream f(pathsTxt);
			if (f.is_open())
			{
				pathsTxtOpened = true;
				std::string line;
				while (std::getline(f, line))
				{
					const std::string trimmed = Trim(line);
					if (trimmed.empty() || trimmed[0] == '#')
						continue;

					std::string resolved = ResolveEntry(trimmed, exeDir);
					if (resolved.empty())
						continue;

					AddSearchPathWithImplicit(resolved);
				}
			}
		}

		// If neither -path args nor a paths.txt produced any usable
		// entries, we have nowhere to find game data. The previous
		// fallback (silently use the exe dir) just punted the failure
		// to the first INI load and surfaced as a generic
		// "Uncaught Exception during initialization." crash with no
		// actionable info. Show a real dialog instead and exit cleanly.
		if (s_paths.empty() && s_commandLinePaths.empty() && !pathsTxtOpened)
		{
			FatalNoSearchPaths(exeDir, pathsTxt);
		}

		// Defensive fallback: if paths.txt did open but contained
		// nothing usable (all comments / all unresolvable), or if
		// -path entries all failed to resolve, drop in the exe
		// directory so the engine still has *some* search root.
		// This used to be the universal "always fall back" behavior;
		// it now only fires when the user actively tried to provide
		// paths and they were just unusable.
		if (s_paths.empty())
		{
			std::string fallback = exeDir.string();
			while (!fallback.empty() &&
				(fallback.back() == '/' || fallback.back() == '\\'))
			{
				fallback.pop_back();
			}
			s_paths.push_back(fallback);
		}

		// Surface the resolved paths so it's obvious at startup whether
		// paths.txt was parsed correctly and which directories the asset
		// resolver will actually scan.
		if (!s_commandLinePaths.empty())
			Inspector::Log("[SearchPaths] source = command line (-path), %zu entr%s",
				s_commandLinePaths.size(), s_commandLinePaths.size() == 1 ? "y" : "ies");
		else
			Inspector::Log("[SearchPaths] source = paths.txt = %s", pathsTxt.string().c_str());
		for (size_t i = 0; i < s_paths.size(); ++i)
		{
			Inspector::Log("[SearchPaths] #%zu (%s) = %s",
				i,
				i == 0 ? "highest priority" : "fallback",
				s_paths[i].c_str());
		}
	}
}


namespace SearchPaths
{
	void AddCommandLinePath(const char* absPath)
	{
		if (absPath == nullptr || absPath[0] == '\0')
			return;

		// If the engine has already initialized, the new entry needs to
		// take effect: invalidate so the next Get()/Resolve() repopulates
		// from the (now-extended) command-line list. The CommandLine
		// parser runs before the file systems mount, so in practice this
		// branch never fires — it's a safety net for callers that wire up
		// search paths from elsewhere.
		s_commandLinePaths.emplace_back(absPath);
		if (s_initialized)
		{
			s_initialized = false;
			s_paths.clear();
			std::lock_guard<std::mutex> lock(s_resolveCacheMutex);
			s_resolveCache.clear();
		}
	}

	const std::vector<std::string>& GetCommandLinePaths()
	{
		return s_commandLinePaths;
	}

	void Init()
	{
		s_initialized = false;
		s_paths.clear();
		{
			std::lock_guard<std::mutex> lock(s_resolveCacheMutex);
			s_resolveCache.clear();
		}
		EnsureInit();
	}

	const std::vector<std::string>& Get()
	{
		EnsureInit();
		return s_paths;
	}

	std::string Resolve(const char* relPath)
	{
		if (relPath == nullptr || relPath[0] == '\0')
			return std::string();

		EnsureInit();

		// Absolute paths are not cached — they bypass the search-path
		// walk entirely because the caller already knows where the
		// file is (or isn't).
		fs::path p(relPath);
		std::error_code ec;
		if (p.is_absolute())
			return fs::exists(p, ec) ? p.string() : std::string();

		// Rule 2: runtime cache. Once a data asset is found through
		// the search-path walk, remember the absolute path so the next
		// lookup skips straight to `fs::exists(cached)` instead of
		// iterating every path again. The value is empty-string for
		// negative hits (the query didn't resolve in any path), which
		// are still worth caching — the engine probes for many
		// optional files and each of those misses would otherwise
		// cost a stat call per search-path entry every time.
		const std::string key = MakeCacheKey(relPath);
		{
			std::lock_guard<std::mutex> lock(s_resolveCacheMutex);
			auto it = s_resolveCache.find(key);
			if (it != s_resolveCache.end())
				return it->second;
		}

		std::string resolved;
		for (const std::string& sp : s_paths)
		{
			fs::path candidate = fs::path(sp) / p;
			if (fs::exists(candidate, ec))
			{
				resolved = candidate.string();
				break;
			}
		}

		{
			std::lock_guard<std::mutex> lock(s_resolveCacheMutex);
			s_resolveCache.emplace(key, resolved);
		}
		return resolved;
	}

	bool Exists(const char* relPath)
	{
		return !Resolve(relPath).empty();
	}
}
