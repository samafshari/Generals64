/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

///////// Win32LocalFileSystem.cpp /////////////////////////
// Bryan Cleveland, August 2002
// Ported to std::filesystem for cross-platform support
////////////////////////////////////////////////////////////

#include <filesystem>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <io.h>     // _access on MSVC
#define F_OK 0
#else
#include <unistd.h> // access() on POSIX
#endif

// Normalize path separators: convert backslashes to forward slashes
// on non-Windows platforms. Returns the input unchanged on Windows.
static std::string NormalizePath(const char* path)
{
    std::string result(path);
#ifndef _WIN32
    for (char& c : result)
        if (c == '\\') c = '/';
#endif
    return result;
}

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "Common/PerfTimer.h"
#include "Common/SearchPaths.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32LocalFile.h"

namespace fs = std::filesystem;

Win32LocalFileSystem::Win32LocalFileSystem() : LocalFileSystem()
{
}

Win32LocalFileSystem::~Win32LocalFileSystem() {
}

File * Win32LocalFileSystem::openFile(const Char *filename, Int access, size_t bufferSize)
{
	if (strlen(filename) <= 0) {
		return nullptr;
	}

	if (access & File::WRITE) {
		// Ensure parent directories exist before creating the file.
		// Writes always go to the literal path the caller asked for —
		// search paths only apply to reads.
		AsciiString string;
		string = filename;
		AsciiString token;
		AsciiString dirName;
		string.nextToken(&token, "\\/");
		dirName = token;
		while ((token.find('.') == nullptr) || (string.find('.') != nullptr)) {
			createDirectory(dirName);
			string.nextToken(&token, "\\/");
			dirName.concat('/');
			dirName.concat(token);
		}

		Win32LocalFile *file = newInstance( Win32LocalFile );
		if (file->open(filename, access, bufferSize) == FALSE) {
			deleteInstance(file);
			return nullptr;
		}
		file->deleteOnClose();
		return file;
	}

	// READ access. For absolute paths, just fopen the literal path.
	std::filesystem::path inputPath(filename);
	if (inputPath.is_absolute())
	{
		Win32LocalFile *file = newInstance( Win32LocalFile );
		if (file->open(filename, access, bufferSize) == TRUE) {
			file->deleteOnClose();
			return file;
		}
		deleteInstance(file);
		return nullptr;
	}

	// For relative paths, go through SearchPaths::Resolve. Resolve
	// walks each search-path entry from paths.txt, returns the first
	// `<sp>/<filename>` that exists on disk, and caches the result so
	// subsequent identical queries don't re-walk. A cached negative
	// hit returns an empty string without touching the FS at all.
	{
		std::string resolved = SearchPaths::Resolve(filename);
		if (!resolved.empty())
		{
			Win32LocalFile *file = newInstance( Win32LocalFile );
			if (file->open(resolved.c_str(), access, bufferSize) == TRUE) {
				file->deleteOnClose();
				return file;
			}
			deleteInstance(file);
		}
	}

	// Last-ditch: try the literal filename as-is. This catches any
	// caller that supplied a path already resolvable from the current
	// working directory and which SearchPaths happens to not have in
	// its list (e.g. engine writing under the legacy `../Run` tree).
	// This branch almost never hits in practice but preserves the
	// pre-search-path behavior for the rare write-then-read case.
	{
		Win32LocalFile *file = newInstance( Win32LocalFile );
		if (file->open(filename, access, bufferSize) == TRUE) {
			file->deleteOnClose();
			return file;
		}
		deleteInstance(file);
	}

	return nullptr;
}

void Win32LocalFileSystem::update() {}
void Win32LocalFileSystem::init() {}
void Win32LocalFileSystem::reset() {}

Bool Win32LocalFileSystem::doesFileExist(const Char *filename) const
{
	std::string path = NormalizePath(filename);

	// Absolute paths bypass the search-path cache entirely.
	if (std::filesystem::path(path).is_absolute())
	{
#ifdef _WIN32
		return (_access(path.c_str(), F_OK) == 0) ? TRUE : FALSE;
#else
		return (access(path.c_str(), F_OK) == 0) ? TRUE : FALSE;
#endif
	}

	// Relative paths go through the cached search-path resolver.
	// SearchPaths::Exists hits the same cache as openFile() so two
	// callers asking about the same asset only stat the filesystem
	// once between them.
	return SearchPaths::Exists(path.c_str()) ? TRUE : FALSE;
}

void Win32LocalFileSystem::getFileListInDirectory(
	const AsciiString& currentDirectory,
	const AsciiString& originalDirectory,
	const AsciiString& searchName,
	FilenameList& filenameList,
	Bool searchSubdirectories) const
{
	// Build the search base path
	std::string basePath = std::string(originalDirectory.str()) + std::string(currentDirectory.str());

	// Extract the wildcard pattern from searchName (e.g. "*.ini")
	std::string pattern = searchName.str();

	// Convert glob pattern to a simple extension match
	// Supports "*.ext" and "*.*" patterns (the most common cases)
	std::string extension;
	if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.')
		extension = pattern.substr(1); // ".ini", ".*"

	std::error_code ec;
	if (!fs::is_directory(basePath, ec))
		return;

	// Iterate files in the directory
	for (const auto& entry : fs::directory_iterator(basePath, ec))
	{
		if (!entry.is_regular_file(ec))
			continue;

		std::string filename = entry.path().filename().string();

		// Check if filename matches the pattern
		bool matches = false;
		if (pattern == "*.*" || pattern == "*")
		{
			matches = true;
		}
		else if (!extension.empty())
		{
			// Case-insensitive extension match
			std::string fileExt = entry.path().extension().string();
			if (fileExt.size() == extension.size())
			{
				matches = true;
				for (size_t i = 0; i < fileExt.size(); ++i)
				{
					if (tolower((unsigned char)fileExt[i]) != tolower((unsigned char)extension[i]))
					{
						matches = false;
						break;
					}
				}
			}
		}

		if (matches)
		{
			AsciiString newFilename;
			newFilename = originalDirectory;
			newFilename.concat(currentDirectory);
			newFilename.concat(filename.c_str());
			if (filenameList.find(newFilename) == filenameList.end())
				filenameList.insert(newFilename);
		}
	}

	// Recurse into subdirectories if requested
	if (searchSubdirectories)
	{
		for (const auto& entry : fs::directory_iterator(basePath, ec))
		{
			if (!entry.is_directory(ec))
				continue;

			std::string dirName = entry.path().filename().string();
			if (dirName == "." || dirName == "..")
				continue;

			AsciiString subDir;
			subDir.concat(currentDirectory);
			subDir.concat(dirName.c_str());
			subDir.concat('/');

			getFileListInDirectory(subDir, originalDirectory, searchName, filenameList, searchSubdirectories);
		}
	}
}

Bool Win32LocalFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	std::error_code ec;
	fs::path p(filename.str());

	if (!fs::exists(p, ec))
		return FALSE;

	auto fileSize = fs::file_size(p, ec);
	if (ec)
		return FALSE;

	auto lastWrite = fs::last_write_time(p, ec);
	if (ec)
		return FALSE;

	// Convert file_time_type to a 64-bit timestamp
	// Use the duration since epoch as a portable timestamp
	auto duration = lastWrite.time_since_epoch();
	auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

	fileInfo->timestampHigh = (UnsignedInt)(ticks >> 32);
	fileInfo->timestampLow = (UnsignedInt)(ticks & 0xFFFFFFFF);
	fileInfo->sizeHigh = (UnsignedInt)(fileSize >> 32);
	fileInfo->sizeLow = (UnsignedInt)(fileSize & 0xFFFFFFFF);

	return TRUE;
}

Bool Win32LocalFileSystem::createDirectory(AsciiString directory)
{
	if (directory.isEmpty() || directory.getLength() >= 260)
		return FALSE;

	std::error_code ec;
	fs::create_directories(directory.str(), ec);
	return !ec;
}

AsciiString Win32LocalFileSystem::normalizePath(const AsciiString& filePath) const
{
	std::error_code ec;
	fs::path normalized = fs::absolute(filePath.str(), ec);
	if (ec)
		return AsciiString::TheEmptyString;

	std::string result = normalized.string();
	AsciiString out;
	out = result.c_str();
	return out;
}
