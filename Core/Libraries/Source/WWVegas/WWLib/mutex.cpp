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

#include "mutex.h"
#include "wwdebug.h"

// ----------------------------------------------------------------------------

MutexClass::MutexClass(const char* /*name*/) : locked(0)
{
}

MutexClass::~MutexClass()
{
	WWASSERT(!locked); // Can't delete locked mutex!
}

bool MutexClass::Lock(int time)
{
	if (time == WAIT_INFINITE) {
		m_mutex.lock();
		locked++;
		return true;
	} else {
		if (m_mutex.try_lock_for(std::chrono::milliseconds(time))) {
			locked++;
			return true;
		}
		return false;
	}
}

void MutexClass::Unlock()
{
	WWASSERT(locked);
	locked--;
	m_mutex.unlock();
}

// ----------------------------------------------------------------------------

MutexClass::LockClass::LockClass(MutexClass& mutex_,int time) : mutex(mutex_)
{
	failed=!mutex.Lock(time);
}

MutexClass::LockClass::~LockClass()
{
	if (!failed) mutex.Unlock();
}







// ----------------------------------------------------------------------------

CriticalSectionClass::CriticalSectionClass() : locked(0)
{
}

CriticalSectionClass::~CriticalSectionClass()
{
	WWASSERT(!locked); // Can't delete locked mutex!
}

void CriticalSectionClass::Lock()
{
	m_mutex.lock();
	locked++;
}

void CriticalSectionClass::Unlock()
{
	WWASSERT(locked);
	locked--;
	m_mutex.unlock();
}

// ----------------------------------------------------------------------------

CriticalSectionClass::LockClass::LockClass(CriticalSectionClass& critical_section) : CriticalSection(critical_section)
{
	CriticalSection.Lock();
}

CriticalSectionClass::LockClass::~LockClass()
{
	CriticalSection.Unlock();
}


