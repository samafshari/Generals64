/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// Registry.cpp
// Simple interface for storing/retrieving registry values.
// On Windows: reads from the Windows Registry.
// On other platforms: returns FALSE (game falls back to defaults).
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"

#include "Common/Registry.h"

#ifdef _WIN32

// ---------- Windows: real registry access ----------

Bool  getStringFromRegistry(HKEY root, AsciiString path, AsciiString key, AsciiString& val)
{
	HKEY handle;
	unsigned char buffer[256];
	unsigned long size = 256;
	unsigned long type;
	int returnValue;

	if ((returnValue = RegOpenKeyEx( root, path.str(), 0, KEY_READ, &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), nullptr, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = (char *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool getUnsignedIntFromRegistry(HKEY root, AsciiString path, AsciiString key, UnsignedInt& val)
{
	HKEY handle;
	unsigned char buffer[4];
	unsigned long size = 4;
	unsigned long type;
	int returnValue;

	if ((returnValue = RegOpenKeyEx( root, path.str(), 0, KEY_READ, &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), nullptr, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = *(UnsignedInt *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool setStringInRegistry( HKEY root, AsciiString path, AsciiString key, AsciiString val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;
	char lpClass[] = "REG_NONE";

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, lpClass, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &handle, nullptr )) == ERROR_SUCCESS)
	{
		type = REG_SZ;
		size = val.getLength()+1;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)val.str(), size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool setUnsignedIntInRegistry( HKEY root, AsciiString path, AsciiString key, UnsignedInt val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;
	char lpClass[] = "REG_NONE";

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, lpClass, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &handle, nullptr )) == ERROR_SUCCESS)
	{
		type = REG_DWORD;
		size = 4;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)&val, size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

#else

// ---------- Non-Windows: stub implementations ----------
// Registry is a Windows-only concept. On macOS/Linux the game uses
// default paths and environment variables instead.

static Bool getStringFromRegistry(int /*root*/, AsciiString /*path*/, AsciiString /*key*/, AsciiString& /*val*/)
{
	return FALSE;
}

static Bool getUnsignedIntFromRegistry(int /*root*/, AsciiString /*path*/, AsciiString /*key*/, UnsignedInt& /*val*/)
{
	return FALSE;
}

#endif // _WIN32

// ---------- Cross-platform registry wrappers ----------

Bool GetStringFromGeneralsRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
#ifdef _WIN32
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
		return TRUE;
	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
#else
	// On non-Windows, check environment variable as fallback
	const char* envPath = getenv("GENERALS_INSTALL_PATH");
	if (envPath && key == "InstallPath")
	{
		val = envPath;
		return TRUE;
	}
	return FALSE;
#endif
}

Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
#ifdef _WIN32
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#endif
	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
		return TRUE;
	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
#else
	(void)path; (void)key; (void)val;
	return FALSE;
#endif
}

Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val)
{
#ifdef _WIN32
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#endif
	fullPath.concat(path);
	DEBUG_LOG(("GetUnsignedIntFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getUnsignedIntFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
		return TRUE;
	return getUnsignedIntFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
#else
	(void)path; (void)key; (void)val;
	return FALSE;
#endif
}

AsciiString GetRegistryLanguage()
{
	static Bool cached = FALSE;
	static AsciiString val = "english";
	if (cached)
		return val;
	cached = TRUE;
	GetStringFromRegistry("", "Language", val);
	return val;
}

AsciiString GetRegistryGameName()
{
	AsciiString val = "GeneralsMPTest";
	GetStringFromRegistry("", "SKU", val);
	return val;
}

UnsignedInt GetRegistryVersion()
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "Version", val);
	return val;
}

UnsignedInt GetRegistryMapPackVersion()
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "MapPackVersion", val);
	return val;
}
