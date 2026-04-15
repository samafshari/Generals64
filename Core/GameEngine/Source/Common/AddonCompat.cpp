// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#include "PreRTS.h"

#include "Common/AddonCompat.h"
#include "Common/FileSystem.h"

namespace addon
{
Bool HasFullviewportDat()
{
	Char value = '0';
	if (File* file = TheFileSystem->openFile("GenTool/fullviewport.dat", File::READ | File::BINARY))
	{
		file->read(&value, 1);
	}
	return value != '0';
}

} // namespace addon
