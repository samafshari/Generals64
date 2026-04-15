/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// AIPathfindPrecomputed.h
// Deterministic map-load pathfinding accelerators: JPS+ jump tables and
// zone-distance matrices built once per map (in parallel) and consumed by
// Pathfinder::internalFindPath to reduce A* expansions.
//
// All data here is a pure function of the pathfind grid + locomotor class.
// No RNG, no float keys, no wall-clock dependency — multiplayer stays lockstep.

#pragma once

#include <atomic>
#include <vector>

#include "Lib/Basetype.h"		// IRegion2D, Int, Bool, UnsignedByte
#include "Common/GameType.h"	// PathfindLayerEnum
#include "GameLogic/LocomotorSet.h"

class Pathfinder;
class PathfindCell;
class PathfindLayer;

//-----------------------------------------------------------------------------
// Locomotor "class" — a reduced, deterministic categorisation of a unit's
// effective mobility. Zone tables in PathfindZoneManager already partition
// surfaces into these five equivalences; we mirror that here so one jump
// table per class covers every unit.
//
// The classifier is deterministic (pure function of LocomotorSet + crusher),
// which is critical for MP: two clients must pick the same class for the
// same unit or they'd consume different jump tables.
//-----------------------------------------------------------------------------
enum class PFLocoClass : UnsignedByte
{
	GROUND			= 0,	// clear ground only (infantry, wheeled, treads)
	AMPHIBIOUS		= 1,	// clear + water (hover, some boats)
	CLIFF_CLIMBER	= 2,	// clear + cliff
	RUBBLE_WALKER	= 3,	// clear + rubble
	CRUSHER			= 4,	// clear + crushable obstacle
	PF_LOCO_CLASS_COUNT
};

//-----------------------------------------------------------------------------
// Jump direction encoding — matches the delta[] ordering in
// Pathfinder::examineNeighboringCells so the consumer can index by the same `i`.
//   0: E  (+1,  0)
//   1: N  ( 0, +1)
//   2: W  (-1,  0)
//   3: S  ( 0, -1)
//   4: NE (+1, +1)
//   5: NW (-1, +1)
//   6: SW (-1, -1)
//   7: SE (+1, -1)
//-----------------------------------------------------------------------------
enum PFJumpDir
{
	PF_DIR_E  = 0,
	PF_DIR_N  = 1,
	PF_DIR_W  = 2,
	PF_DIR_S  = 3,
	PF_DIR_NE = 4,
	PF_DIR_NW = 5,
	PF_DIR_SW = 6,
	PF_DIR_SE = 7,
	PF_DIR_COUNT = 8
};

extern const Int PF_DIR_DX[PF_DIR_COUNT];
extern const Int PF_DIR_DY[PF_DIR_COUNT];

//-----------------------------------------------------------------------------
// Jump value encoding per cell per direction:
//   v >  0 : step exactly v cells in this direction to reach a jump point.
//   v == 0 : direction is blocked (adjacent cell not jumpable).
//   v <  0 : no jump point in this ray; |v| open cells before obstacle/edge.
// Using Int16: |v| ≤ 32767 cells is plenty (largest map is ~1024 on a side).
//-----------------------------------------------------------------------------
typedef Short PFJumpVal;

//-----------------------------------------------------------------------------
// Build status, per class. Accessed from the main logic thread and (later,
// phase 3) from worker threads, so values must be atomic.
//-----------------------------------------------------------------------------
enum class PFBuildStatus : Int
{
	IDLE    = 0,	// never built; fall back to classic A*
	BUILDING = 1,	// worker is rebuilding; fall back to classic A*
	READY    = 2	// tables are coherent with the current map state
};

//-----------------------------------------------------------------------------
// The accelerator itself. Owned by Pathfinder; built at newMap() end, after
// classifyMap() + calculateZones(). Safe to destroy/recreate on map reset.
//-----------------------------------------------------------------------------
class PathfindPrecomputed
{
public:
	PathfindPrecomputed();
	~PathfindPrecomputed();

	// Disallow copy — each table is allocated once and bound to an owner.
	PathfindPrecomputed( const PathfindPrecomputed& ) = delete;
	PathfindPrecomputed& operator=( const PathfindPrecomputed& ) = delete;

	/// Resize backing storage to cover the current map extent. Idempotent.
	/// Discards any existing tables; call buildAll() after to repopulate.
	void allocate( Pathfinder* owner, const IRegion2D& extent );

	/// Release all backing storage and return to IDLE.
	void release();

	/// Build every class's tables, currently sequentially.
	/// Runs on the calling thread — at map-load this is the main thread.
	/// Phase 1d will swap this for a parallel build.
	void buildAll();

	/// Mark every class as BUILDING and invalidate tables — use when
	/// structure changes require a full rebuild before reuse.
	void invalidateAll();

	/// Asynchronously rebuild every class's tables. Returns immediately;
	/// status atomics flip to BUILDING → READY when the worker completes.
	/// Safe to call even if a previous rebuild is still running — it joins
	/// before spawning new workers, so no overlap.
	void rebuildAsync();

	/// Join any in-flight async workers. Called from release() and from the
	/// destructor so the thread storage is safe to tear down.
	void waitForAsync();

	/// True iff the class's tables are coherent and safe to query.
	Bool isReady( PFLocoClass cls ) const;

	/// True iff the zone-distance table is coherent. Independent of the
	/// per-class jump-table readiness.
	Bool zoneDistReady() const { return m_zoneDistReady.load( std::memory_order_acquire ); }

	/// Admissible zone-distance heuristic between two raw zone IDs. Returns
	/// the minimum number of zone-boundary crossings × COST_ORTHOGONAL, which
	/// is a lower bound on true cell cost (each crossing requires at least one
	/// cell step). Returns 0 when zones are equal or the table is not ready.
	UnsignedInt zoneDistanceCost( UnsignedShort /*zoneStorageType*/ zoneA, UnsignedShort /*zoneStorageType*/ zoneB ) const;

	/// Jump distance from (cellX, cellY) in direction `dir` for class `cls`.
	/// MUST only be called when isReady(cls) is true and (cellX, cellY) is
	/// inside the map extent.
	inline PFJumpVal getJump( Int cellX, Int cellY, PFLocoClass cls, Int dir ) const
	{
		const Int idx = ((cellX - m_extentLoX) * m_height + (cellY - m_extentLoY)) * PF_DIR_COUNT + dir;
		return m_jump[static_cast<Int>( cls )][idx];
	}

	/// Convert a LocomotorSet + crusher status into the class whose jump table
	/// covers this unit. Deterministic function of its inputs.
	static PFLocoClass classifyLocomotorSet( LocomotorSurfaceTypeMask surfaces, Bool isCrusher );

	/// Diagnostic: total bytes allocated by all tables (for Inspector memory panel).
	size_t totalBytes() const;

private:
	void buildClass( PFLocoClass cls );
	void buildZoneDistanceTable();

	/// "Is this cell a uniform-cost, jumpable cell for this class?"
	/// True iff cell is passable under `cls` AND not pinched AND not on a
	/// zone boundary with a penalty. If false, sweeps stop at this cell so
	/// A* evaluates it classically.
	Bool isUniform( Int cellX, Int cellY, PFLocoClass cls ) const;

	/// "Is this cell blocked for this class?" — stricter than !isUniform:
	/// used to decide if an orthogonal neighbour is a forced-neighbour source.
	Bool isBlocked( Int cellX, Int cellY, PFLocoClass cls ) const;

	// Sweep primitives. Each is independent of every other sweep.
	void sweepStraight( PFLocoClass cls, Int dir );
	void sweepDiagonal( PFLocoClass cls, Int dir );

	// Bounds test for cellX/cellY against the map extent.
	inline Bool inExtent( Int cellX, Int cellY ) const
	{
		return cellX >= m_extentLoX && cellX <= m_extentHiX
		    && cellY >= m_extentLoY && cellY <= m_extentHiY;
	}

	inline Int cellIndex( Int cellX, Int cellY ) const
	{
		return (cellX - m_extentLoX) * m_height + (cellY - m_extentLoY);
	}

private:
	Pathfinder* m_owner;

	// Cached extent — copied out so we don't chase into Pathfinder per lookup.
	Int m_extentLoX, m_extentLoY, m_extentHiX, m_extentHiY;
	Int m_width, m_height;

	// Per class: flat array of PFJumpVal[cellIndex * 8 + dir].
	std::vector<PFJumpVal> m_jump[static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT )];

	// Per class build status. Int-wrapped so std::atomic default works cleanly.
	std::atomic<Int> m_status[static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT )];

	// Zone-distance table. Indexed [zoneA * m_zoneDistStride + zoneB], storing
	// the minimum number of raw-zone boundary crossings from A to B (UINT16_MAX
	// = unreachable). Built on the class-independent raw zone graph; admissible
	// as a lower bound for any class that can traverse the intervening zones.
	std::vector<UnsignedShort> m_zoneDist;
	Int m_zoneDistStride;
	std::atomic<bool> m_zoneDistReady;

	// Async rebuild worker storage. std::thread::joinable() governs lifetime.
	// Held across rebuildAsync() calls; joined by waitForAsync().
	std::vector<void*> m_asyncThreads;	// type-erased to keep <thread> out of header
};
