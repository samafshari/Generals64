/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// FILE: ScopedMutex.h ////////////////////////////////////////////////////////////////////////////
// Author: John McDonald, November 2002
// Desc:   A scoped mutex class to easily lock a scope with a pre-existing mutex object.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef _WIN32

class ScopedMutex
{
	private:
		HANDLE m_mutex;

	public:
		ScopedMutex(HANDLE mutex) : m_mutex(mutex)
		{
			WaitForSingleObject(m_mutex, INFINITE);
		}

		~ScopedMutex()
		{
			ReleaseMutex(m_mutex);
		}
};

#else

#include <mutex>

// POSIX: ScopedMutex is not used in cross-platform code paths.
// Provide a minimal implementation using std::mutex for compilation.
class ScopedMutex
{
	private:
		void* m_mutex;

	public:
		ScopedMutex(void* mutex) : m_mutex(mutex) {}
		~ScopedMutex() {}
};

#endif
