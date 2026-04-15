/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// AIPathfindFlowField.h
// Shared-goal flow fields: one Dijkstra from a goal cell produces a per-cell
// "step-next" direction grid. Every unit heading to that goal consults the
// grid in O(1), so per-frame pathfinder cost stops scaling with unit count
// and starts scaling with *distinct active goals*.
//
// Determinism: integer costs, stable tiebreak via monotonic sequence numbers.
// Cache eviction is deterministic too — all MP clients run the same findPath
// sequence on the same logic frames, so LRU state is identical.

#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <vector>

#include "Lib/Basetype.h"
#include "Common/GameType.h"
#include "GameLogic/AIPathfindPrecomputed.h"	// PFLocoClass, PFJumpDir, PF_DIR_*

class Pathfinder;
class PathfindCell;

//-----------------------------------------------------------------------------
// Per-cell flow value. One byte is enough:
//   0..7   : PF_DIR_* index to step from this cell toward the goal
//   0xFE   : this IS the goal cell — stop here
//   0xFF   : unreachable from this cell under the field's locomotor class
//-----------------------------------------------------------------------------
enum : UnsignedByte
{
	PF_FLOW_GOAL        = 0xFE,
	PF_FLOW_UNREACHABLE = 0xFF
};

//-----------------------------------------------------------------------------
// One flow field, bound to a specific goal cell and locomotor class.
//-----------------------------------------------------------------------------
class PathfindFlowField
{
public:
	PathfindFlowField();

	/// Build by Dijkstra from (goalX, goalY) outward under `cls`'s passability.
	/// Takes ~10-50 ms on a 512² map. Call once per (goal, class); reuse.
	/// Safe to call from a worker thread as long as the pathfind grid is not
	/// being mutated (same contract as PathfindPrecomputed).
	void build( Pathfinder* owner, const IRegion2D& extent, PFLocoClass cls,
	            Int goalCellX, Int goalCellY );

	/// True once build() has completed successfully.
	Bool isReady() const { return m_ready.load( std::memory_order_acquire ); }

	PFLocoClass locoClass() const { return m_class; }
	Int goalX() const { return m_goalX; }
	Int goalY() const { return m_goalY; }

	/// Next-step direction at (cellX, cellY). Returns PF_FLOW_UNREACHABLE if
	/// the cell is outside the field's coverage or has no path to the goal.
	inline UnsignedByte getDir( Int cellX, Int cellY ) const
	{
		if ( cellX < m_extentLoX || cellX > m_extentHiX
		  || cellY < m_extentLoY || cellY > m_extentHiY )
			return PF_FLOW_UNREACHABLE;
		const size_t idx = static_cast<size_t>( cellX - m_extentLoX )
		                 * static_cast<size_t>( m_height )
		                 + static_cast<size_t>( cellY - m_extentLoY );
		return m_dir[idx];
	}

	size_t sizeBytes() const { return m_dir.capacity(); }

private:
	std::vector<UnsignedByte> m_dir;
	Int m_extentLoX, m_extentLoY, m_extentHiX, m_extentHiY;
	Int m_width, m_height;
	Int m_goalX, m_goalY;
	PFLocoClass m_class;
	std::atomic<bool> m_ready;
};

//-----------------------------------------------------------------------------
// LRU cache of flow fields. Requests for the same (goal, class) are served
// from the cache; novel goals evict the oldest entry and trigger a synchronous
// build (rare after warm-up).
//
// Cache state is deterministic across MP clients because findPath calls are
// dispatched from the lockstep logic queue in identical order.
//-----------------------------------------------------------------------------
class PathfindFlowFieldCache
{
public:
	PathfindFlowFieldCache();
	~PathfindFlowFieldCache();

	PathfindFlowFieldCache( const PathfindFlowFieldCache& ) = delete;
	PathfindFlowFieldCache& operator=( const PathfindFlowFieldCache& ) = delete;

	/// Bind to a pathfinder + max capacity. Released on allocate() again.
	void allocate( Pathfinder* owner, const IRegion2D& extent, size_t maxEntries );
	void release();

	/// Fetch (or synchronously build + cache) the flow field for this goal
	/// and locomotor class. Returns nullptr if the cache is not allocated or
	/// the goal cell is outside the map extent.
	PathfindFlowField* getOrBuild( PFLocoClass cls, Int goalCellX, Int goalCellY );

	/// Drop every cached field. Called whenever the pathfind grid mutates.
	void invalidateAll();

	/// Diagnostic — total cache footprint in bytes.
	size_t totalBytes() const;

private:
	struct Entry
	{
		PFLocoClass cls;
		Int goalX;
		Int goalY;
		std::unique_ptr<PathfindFlowField> field;
	};

	Pathfinder* m_owner;
	IRegion2D   m_extent;
	size_t      m_maxEntries;
	// std::list keeps iterators stable across inserts/erases, so the LRU
	// bookkeeping is O(1) without extra indirection.
	std::list<Entry> m_lru;
};
