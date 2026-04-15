// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

#include "Lib/BaseType.h"

// For miscellaneous game utility functions.

class Player;
typedef Int PlayerIndex;

namespace rts
{

bool localPlayerHasRadar();
Player* getObservedOrLocalPlayer(); ///< Get the current observed or local player. Is never null.
Player* getObservedOrLocalPlayer_Safe(); ///< Get the current observed or local player. Is never null, except when the application does not have players.
PlayerIndex getObservedOrLocalPlayerIndex_Safe(); ///< Get the current observed or local player index. Returns 0 when the application does not have players.

void changeLocalPlayer(Player* player); //< Change local player during game. Must not pass null.
void changeObservedPlayer(Player* player); ///< Change observed player during game. Can pass null: is identical to passing the "ReplayObserver" player.

} // namespace rts
