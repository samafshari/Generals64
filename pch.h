#pragma once

// Precompiled header: heavy system and engine headers that rarely change.
// PreRTS.h brings in windows.h, system C headers, STL containers, and core engine types.
// always.h brings in WW3D foundation (memory management, compiler compat, defines).
// Both use _OPERATOR_NEW_DEFINED_ guard so operator new/delete definitions don't conflict.

#include "PreRTS.h"
#include "always.h"
