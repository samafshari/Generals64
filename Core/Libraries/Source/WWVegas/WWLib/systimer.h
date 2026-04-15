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

/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/systimer.h                             $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 12/09/01 6:41p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#ifndef _SYSTIMER_H

#include "always.h"
#include <chrono>

#define TIMEGETTIME SystemTime.Get
#define MS_TIMER_SECOND 1000

// Portable replacement for Win32 timeGetTime() — returns milliseconds
// since process start via std::chrono. All code should use this instead
// of the Win32 API directly.
#ifndef _WIN32
#define timeGetTime() (SystemTime.Get())
#endif

/*
** Class that provides millisecond-resolution timing via std::chrono.
** Historically wrapped Win32 timeGetTime(); now fully cross-platform.
*/
class SysTimeClass
{

	public:

		SysTimeClass();
		~SysTimeClass() = default;

		/*
		** Get. Returns elapsed milliseconds since first call.
		*/
		WWINLINE unsigned long Get();
		WWINLINE unsigned long operator () () {return(Get());}
		WWINLINE operator unsigned long() {return(Get());}

		/*
		** Use periodically (like every few days!) to make sure the timer doesn't wrap.
		*/
		void Reset();

		/*
		** See if the timer is about to wrap.
		*/
		bool Is_Getting_Late();

	private:

		std::chrono::steady_clock::time_point m_startTime;

};

extern SysTimeClass SystemTime;


WWINLINE unsigned long SysTimeClass::Get()
{
	static bool is_init = false;

	if (!is_init) {
		Reset();
		is_init = true;
	}

	auto now = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count();
	return static_cast<unsigned long>(ms);
}


#endif //_SYSTIMER_H
