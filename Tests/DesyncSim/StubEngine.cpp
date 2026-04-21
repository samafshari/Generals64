// StubEngine.cpp
//
// Definitions for the global stub singletons declared in StubEngine.h.
//
// Only one translation unit in each executable should compile this file.
// All other TUs that need TheStubClock or TheStubPlayerList must include
// StubEngine.h which declares them extern.

#include "StubEngine.h"

// Global stub singletons — one definition per program.
StubClock      TheStubClock;
StubPlayerList TheStubPlayerList;
