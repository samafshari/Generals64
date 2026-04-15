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

//////// Win32BIGFileSystem.h ///////////////////////////
// Bryan Cleveland, August 2002
/////////////////////////////////////////////////////////////

#include "Common/AudioAffect.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/file.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "Common/SearchPaths.h"

#include "Inspector/Inspector.h"

#if RTS_ZEROHOUR
#include "Common/Registry.h"
#endif

#include "Win32Device/Common/Win32BIGFile.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include <cstdlib>
#include <filesystem>

// Local big-endian -> host conversion for the 32-bit integers that BIG file
// headers store in network byte order.
static inline Int betoh(Int v)
{
	unsigned long u = static_cast<unsigned long>(v);
#if defined(_MSC_VER)
	return static_cast<Int>(_byteswap_ulong(u));
#elif defined(__GNUC__) || defined(__clang__)
	return static_cast<Int>(__builtin_bswap32(u));
#else
	return static_cast<Int>(
		((u & 0xFF000000u) >> 24) |
		((u & 0x00FF0000u) >> 8)  |
		((u & 0x0000FF00u) << 8)  |
		((u & 0x000000FFu) << 24));
#endif
}

static const char *BIGFileIdentifier = "BIGF";

Win32BIGFileSystem::Win32BIGFileSystem() : ArchiveFileSystem() {
}

Win32BIGFileSystem::~Win32BIGFileSystem() {
}

void Win32BIGFileSystem::init() {
	DEBUG_ASSERTCRASH(TheLocalFileSystem != nullptr, ("TheLocalFileSystem must be initialized before TheArchiveFileSystem."));
	if (TheLocalFileSystem == nullptr) {
		return;
	}

	// Walk paths.txt in priority order. Within ArchiveFileSystem the
	// first BIG that registers a given internal path wins (see
	// loadIntoDirectoryTree's overwrite=FALSE branch — entries are
	// appended at the END of the per-key list and lookups take the
	// first), so loading top-priority directories first is exactly
	// what we want.
	//
	// Scan is non-recursive — SearchPaths auto-detects any
	// `ZH_Generals/` subfolder and exposes it as its own implicit
	// next-priority search-path entry, so listing the Steam install
	// root in paths.txt produces two entries (the root itself plus its
	// ZH_Generals/ child) and each gets a flat top-level *.big scan
	// here. The canonical-path dedup inside loadBigFilesFromDirectory
	// is the safety net for paths.txt files that explicitly list both
	// a parent and its child.
	const std::vector<std::string>& searchPaths = SearchPaths::Get();
	for (const std::string& sp : searchPaths)
	{
		AsciiString dir;
		// loadBigFilesFromDirectory concatenates the dir straight into
		// the discovered filenames, so it needs a trailing separator
		// (a registry InstallPath traditionally ended in '\\').
		std::string withSep = sp;
		if (!withSep.empty() && withSep.back() != '/' && withSep.back() != '\\')
			withSep += '/';
		dir = withSep.c_str();

		const size_t beforeCount = m_archiveFileMap.size();
		loadBigFilesFromDirectory(dir, "*.big");
		const size_t addedByPath = m_archiveFileMap.size() - beforeCount;
		Inspector::Log("[BIGFS] %s : %zu BIG file(s) loaded",
			sp.c_str(), addedByPath);
	}

	Inspector::Log("[BIGFS] total: %zu BIG archives loaded",
		m_archiveFileMap.size());

#if RTS_ZEROHOUR
	// Backward-compat fallback: if the user has neither paths.txt nor
	// any BIGs in the search paths above, try the historical registry
	// path so an out-of-the-box Origin/EA App install still works.
	if (m_archiveFileMap.empty())
	{
		AsciiString installPath;
		GetStringFromGeneralsRegistry("", "InstallPath", installPath);
		if (!installPath.isEmpty())
		{
			loadBigFilesFromDirectory(installPath, "*.big");
		}
	}
#endif
}

void Win32BIGFileSystem::reset() {
}

void Win32BIGFileSystem::update() {
}

void Win32BIGFileSystem::postProcessLoad() {
}

ArchiveFile * Win32BIGFileSystem::openArchiveFile(const Char *filename) {
	File *fp = TheLocalFileSystem->openFile(filename, File::READ | File::BINARY);
	AsciiString archiveFileName;
	archiveFileName = filename;
	archiveFileName.toLower();
	Int archiveFileSize = 0;
	Int numLittleFiles = 0;

	DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - opening BIG file %s", filename));

	if (fp == nullptr) {
		DEBUG_CRASH(("Could not open archive file %s for parsing", filename));
		Inspector::Log("[BIGFS] FAILED to open %s", filename);
		return nullptr;
	}

	AsciiString asciibuf;
	char buffer[_MAX_PATH];
	fp->read(buffer, 4); // read the "BIG" at the beginning of the file.
	buffer[4] = 0;
	if (strcmp(buffer, BIGFileIdentifier) != 0) {
		DEBUG_CRASH(("Error reading BIG file identifier in file %s", filename));
		Inspector::Log("[BIGFS] BAD MAGIC (%.4s) in %s", buffer, filename);
		fp->close();
		fp = nullptr;
		return nullptr;
	}

	// read in the file size.
	fp->read(&archiveFileSize, 4);

	DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - size of archive file is %d bytes", archiveFileSize));

//	char t;

	// read in the number of files contained in this BIG file.
	// change the order of the bytes cause the file size is in reverse byte order for some reason.
	fp->read(&numLittleFiles, 4);
	numLittleFiles = betoh(numLittleFiles);

	DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - %d are contained in archive", numLittleFiles));
//	for (Int i = 0; i < 2; ++i) {
//		t = buffer[i];
//		buffer[i] = buffer[(4-i)-1];
//		buffer[(4-i)-1] = t;
//	}

	// seek to the beginning of the directory listing.
	fp->seek(0x10, File::START);
	// read in each directory listing.
	ArchivedFileInfo *fileInfo = NEW ArchivedFileInfo;
	ArchiveFile *archiveFile = NEW Win32BIGFile(filename, AsciiString::TheEmptyString);

	for (Int i = 0; i < numLittleFiles; ++i) {
		Int filesize = 0;
		Int fileOffset = 0;
		fp->read(&fileOffset, 4);
		fp->read(&filesize, 4);

		filesize = betoh(filesize);
		fileOffset = betoh(fileOffset);

		fileInfo->m_archiveFilename = archiveFileName;
		fileInfo->m_offset = fileOffset;
		fileInfo->m_size = filesize;

		// read in the path name of the file.
		Int pathIndex = -1;
		do {
			++pathIndex;
			fp->read(buffer + pathIndex, 1);
		} while (buffer[pathIndex] != 0);

		Int filenameIndex = pathIndex;
		while ((filenameIndex >= 0) && (buffer[filenameIndex] != '\\') && (buffer[filenameIndex] != '/')) {
			--filenameIndex;
		}

		fileInfo->m_filename = (char *)(buffer + filenameIndex + 1);
		fileInfo->m_filename.toLower();
		buffer[filenameIndex + 1] = 0;

		AsciiString path;
		path = buffer;

		AsciiString debugpath;
		debugpath = path;
		debugpath.concat(fileInfo->m_filename);
//		DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - adding file %s to archive file %s, file number %d", debugpath.str(), fileInfo->m_archiveFilename.str(), i));

		archiveFile->addFile(path, fileInfo);
	}

	archiveFile->attachFile(fp);

	delete fileInfo;
	fileInfo = nullptr;

	// leave fp open as the archive file will be using it.

	return archiveFile;
}

void Win32BIGFileSystem::closeArchiveFile(const Char *filename) {
	// Need to close the specified big file
	ArchiveFileMap::iterator it =  m_archiveFileMap.find(filename);
	if (it == m_archiveFileMap.end()) {
		return;
	}

	if (stricmp(filename, MUSIC_BIG) == 0) {
		// Stop the current audio
		TheAudio->stopAudio(AudioAffect_Music);

		// No need to turn off other audio, as the lookups will just fail.
	}
	DEBUG_ASSERTCRASH(stricmp(filename, MUSIC_BIG) == 0, ("Attempting to close Archive file '%s', need to add code to handle its shutdown correctly.", filename));

	// may need to do some other processing here first.

	delete (it->second);
	m_archiveFileMap.erase(it);
}

void Win32BIGFileSystem::closeAllArchiveFiles() {
}

void Win32BIGFileSystem::closeAllFiles() {
}

Bool Win32BIGFileSystem::loadBigFilesFromDirectory(AsciiString dir, AsciiString fileMask, Bool overwrite) {

	FilenameList filenameList;
	// Non-recursive top-level scan. Each search path in paths.txt is
	// treated as a flat directory of .big files; SearchPaths auto-
	// detects any `ZH_Generals/` subfolder and exposes it as its own
	// implicit search-path entry, so we don't need directory-recursive
	// discovery here. This also avoids the historical problem where
	// a recursive scan on the Steam install would walk into `Data/`,
	// `Manuals/`, `MSS/`, etc., and (with the INIZH.big quirk) drag
	// in per-SKU broken files.
	TheLocalFileSystem->getFileListInDirectory(dir, "", fileMask, filenameList, FALSE);

	Bool actuallyAdded = FALSE;
	FilenameListIter it = filenameList.begin();
	while (it != filenameList.end()) {
#if RTS_ZEROHOUR
		// English, Chinese, and Korean SKUs shipped with two INIZH.big files (one in Run directory, one in Run\Data\INI).
		// The DeleteFile cleanup doesn't work on EA App/Origin installs because the folder is not writable, so we skip loading it instead.
		if (it->endsWithNoCase("Data\\INI\\INIZH.big") || it->endsWithNoCase("Data/INI/INIZH.big")) {
			it++;
			continue;
		}
#endif

		// Rule 1: if this physical BIG file was already loaded through
		// another (higher-priority) search path, skip it. Canonicalize
		// via std::filesystem so backslash/forward-slash and case
		// differences collapse to the same key. Without this, two
		// search-path entries whose directory trees overlap (e.g. a
		// user listing both Steam root and Steam/ZH_Generals) would
		// load the same BIG twice and push duplicate per-file entries
		// into the archive directory tree.
		std::error_code canonEc;
		std::filesystem::path canonical =
			std::filesystem::weakly_canonical(std::filesystem::path(it->str()), canonEc);
		std::string canonKey = canonical.string();
		// Normalize the key — lowercase on Windows, replace both slash
		// flavors with forward slash so the map is stable across how
		// getFileListInDirectory happens to spell the path today.
		for (char& c : canonKey)
		{
			if (c == '\\') c = '/';
#ifdef _WIN32
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
		}
		if (!m_loadedBigCanonicalKeys.insert(canonKey).second)
		{
			Inspector::Log("[BIGFS] skip duplicate %s (already loaded)", it->str());
			it++;
			continue;
		}

		ArchiveFile *archiveFile = openArchiveFile((*it).str());

		if (archiveFile != nullptr) {
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - loading %s into the directory tree.", (*it).str()));
			loadIntoDirectoryTree(archiveFile, overwrite);
			m_archiveFileMap[(*it)] = archiveFile;
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - %s inserted into the archive file map.", (*it).str()));
			actuallyAdded = TRUE;
		}

		it++;
	}

	return actuallyAdded;
}
