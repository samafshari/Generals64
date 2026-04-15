// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#include "PreRTS.h"
#include "GameClient/ClientInstance.h"

#ifndef _WIN32
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define GENERALS_GUID "685EAFF2-3216-4265-B047-251C5F4B82F3"

namespace rts
{
HANDLE ClientInstance::s_mutexHandle = nullptr;
UnsignedInt ClientInstance::s_instanceIndex = 0;
Bool ClientInstance::s_isMultiInstance = true;

#ifndef _WIN32
static int s_lockFd = -1;
#endif

bool ClientInstance::initialize()
{
	if (isInitialized())
		return true;

	while (true)
	{
#ifdef _WIN32
		if (isMultiInstance())
		{
			std::string guidStr = getFirstInstanceName();
			if (s_instanceIndex > 0u)
			{
				char idStr[33];
				itoa(s_instanceIndex, idStr, 10);
				guidStr.push_back('-');
				guidStr.append(idStr);
			}
			s_mutexHandle = CreateMutex(nullptr, FALSE, guidStr.c_str());
			if (GetLastError() == ERROR_ALREADY_EXISTS)
			{
				if (s_mutexHandle != nullptr)
				{
					CloseHandle(s_mutexHandle);
					s_mutexHandle = nullptr;
				}
				++s_instanceIndex;
				continue;
			}
		}
		else
		{
			s_mutexHandle = CreateMutex(nullptr, FALSE, getFirstInstanceName());
			if (GetLastError() == ERROR_ALREADY_EXISTS)
			{
				if (s_mutexHandle != nullptr)
				{
					CloseHandle(s_mutexHandle);
					s_mutexHandle = nullptr;
				}
				return false;
			}
		}
#else
		// POSIX: use a lock file for single-instance detection
		const char* lockPath = "/tmp/generalszh.lock";
		s_lockFd = open(lockPath, O_CREAT | O_RDWR, 0666);
		if (s_lockFd >= 0 && flock(s_lockFd, LOCK_EX | LOCK_NB) == 0)
		{
			s_mutexHandle = (HANDLE)(intptr_t)1; // non-null = initialized
		}
		else if (!isMultiInstance())
		{
			return false;
		}
#endif
		break;
	}

	return true;
}

bool ClientInstance::isInitialized()
{
	return s_mutexHandle != nullptr;
}

bool ClientInstance::isMultiInstance()
{
	return s_isMultiInstance != FALSE;
}

void ClientInstance::setMultiInstance(bool v)
{
	if (isInitialized())
		return;
	s_isMultiInstance = v ? TRUE : FALSE;
}

void ClientInstance::skipPrimaryInstance()
{
	if (isInitialized())
		return;
	++s_instanceIndex;
}

UnsignedInt ClientInstance::getInstanceIndex()
{
	return s_instanceIndex;
}

UnsignedInt ClientInstance::getInstanceId()
{
	return s_instanceIndex + 1;
}

const char* ClientInstance::getFirstInstanceName()
{
	return "GeneralsZH_" GENERALS_GUID;
}

} // namespace rts
