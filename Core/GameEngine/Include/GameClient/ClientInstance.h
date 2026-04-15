// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#include "Lib/BaseType.h"

namespace rts
{


class ClientInstance
{
public:
	// Can be called N times, but is initialized just once.
	static bool initialize();

	static bool isInitialized();

	static bool isMultiInstance();

	// Change multi instance on runtime. Must be called before initialize.
	static void setMultiInstance(bool v);

	// Skips using the primary instance. Must be called before initialize.
	// Useful when the new process is not meant to collide with another normal Generals process.
	static void skipPrimaryInstance();

	// Returns the instance index of this game client. Starts at 0.
	static UnsignedInt getInstanceIndex();

	// Returns the instance id of this game client. Starts at 1.
	static UnsignedInt getInstanceId();

	// Returns the instance name of the first game client.
	static const char* getFirstInstanceName();

private:
	static HANDLE s_mutexHandle;
	static UnsignedInt s_instanceIndex;
	static Bool s_isMultiInstance;
};

} // namespace rts
