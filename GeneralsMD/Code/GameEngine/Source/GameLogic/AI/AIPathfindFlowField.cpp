/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// AIPathfindFlowField.cpp
// Dijkstra-from-goal flow-field builder + LRU cache. See header for contract.

#include "PreRTS.h"

#include "GameLogic/AIPathfindFlowField.h"

#include <algorithm>
#include <vector>

#include "Common/LivePerf.h"
#include "GameLogic/AIPathfind.h"

//-----------------------------------------------------------------------------
// Cost constants must match those in AIPathfind.cpp. The Dijkstra pass produces
// per-cell costs that are used during build for relaxation; only the direction
// byte survives, so any drift from the main file would be silently stale.
//-----------------------------------------------------------------------------
static const Int FF_COST_ORTHOGONAL = 10;
static const Int FF_COST_DIAGONAL   = 14;

// Per-class passability — mirror of the rule used by PathfindPrecomputed.
// Kept local so the flow-field module has no friend coupling to Pathfinder.
static LocomotorSurfaceTypeMask surfacesForFlowClass( PFLocoClass cls )
{
	switch ( cls )
	{
		case PFLocoClass::GROUND:        return LOCOMOTORSURFACE_GROUND;
		case PFLocoClass::AMPHIBIOUS:    return LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_WATER;
		case PFLocoClass::CLIFF_CLIMBER: return LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_CLIFF;
		case PFLocoClass::RUBBLE_WALKER: return LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_RUBBLE;
		case PFLocoClass::CRUSHER:       return LOCOMOTORSURFACE_GROUND;
		default:                         return LOCOMOTORSURFACE_GROUND;
	}
}

static LocomotorSurfaceTypeMask ffSurfacesAllowedByCellType( PathfindCell::CellType t )
{
	switch ( t )
	{
		case PathfindCell::CELL_CLEAR:   return LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_WATER:   return LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_CLIFF:   return LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_RUBBLE:  return LOCOMOTORSURFACE_RUBBLE | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_OBSTACLE:
		case PathfindCell::CELL_BRIDGE_IMPASSABLE:
		case PathfindCell::CELL_IMPASSABLE:
			return LOCOMOTORSURFACE_AIR;
		default:                         return NO_SURFACES;
	}
}

//-----------------------------------------------------------------------------
// Direction table — matches PF_DIR_* encoding in AIPathfindPrecomputed.h.
// Recomputed locally so the Dijkstra loop is a single TU.
//-----------------------------------------------------------------------------
static const Int FF_DX[8] = {  1,  0, -1,  0,   1, -1, -1,  1 };
static const Int FF_DY[8] = {  0,  1,  0, -1,   1,  1, -1, -1 };
// Step costs per direction (0..3 straight, 4..7 diagonal).
static const Int FF_STEP[8] = { 10, 10, 10, 10,  14, 14, 14, 14 };

// "Opposite direction" lookup — if goal relaxed cell N via direction D from N
// to the parent, then the unit at N should step in direction D. But Dijkstra
// propagates costs OUTWARD from goal, so when relaxing neighbour M from parent
// P, M's flow direction is "M toward P" which is the REVERSE of dir(P→M).
static const UnsignedByte FF_OPPOSITE[8] = {
	2 /* E ↔ W */, 3 /* N ↔ S */, 0 /* W ↔ E */, 1 /* S ↔ N */,
	6 /* NE↔SW */, 7 /* NW↔SE */, 4 /* SW↔NE */, 5 /* SE↔NW */
};

//-----------------------------------------------------------------------------
// PathfindFlowField
//-----------------------------------------------------------------------------
PathfindFlowField::PathfindFlowField()
	: m_extentLoX( 0 ), m_extentLoY( 0 ), m_extentHiX( -1 ), m_extentHiY( -1 )
	, m_width( 0 ), m_height( 0 )
	, m_goalX( 0 ), m_goalY( 0 )
	, m_class( PFLocoClass::GROUND )
	, m_ready( false )
{
}

//-----------------------------------------------------------------------------
void PathfindFlowField::build( Pathfinder* owner, const IRegion2D& extent,
                               PFLocoClass cls, Int goalCellX, Int goalCellY )
{
	LIVE_PERF_SCOPE( "Pathfinder::flowFieldBuild" );
	m_ready.store( false, std::memory_order_release );

	m_extentLoX = extent.lo.x;
	m_extentLoY = extent.lo.y;
	m_extentHiX = extent.hi.x;
	m_extentHiY = extent.hi.y;
	m_width  = m_extentHiX - m_extentLoX + 1;
	m_height = m_extentHiY - m_extentLoY + 1;
	m_goalX = goalCellX;
	m_goalY = goalCellY;
	m_class = cls;

	if ( !owner || m_width <= 0 || m_height <= 0 )
		return;
	if ( goalCellX < m_extentLoX || goalCellX > m_extentHiX
	  || goalCellY < m_extentLoY || goalCellY > m_extentHiY )
		return;

	const size_t cellCount = static_cast<size_t>( m_width ) * static_cast<size_t>( m_height );
	m_dir.assign( cellCount, PF_FLOW_UNREACHABLE );

	// Per-cell best-known cost (uint32 to avoid saturation during build).
	std::vector<UnsignedInt> cost( cellCount, 0xFFFFFFFFu );

	const LocomotorSurfaceTypeMask surfaces = surfacesForFlowClass( cls );

	auto cellIndex = [&]( Int x, Int y ) -> size_t
	{
		return static_cast<size_t>( x - m_extentLoX )
		     * static_cast<size_t>( m_height )
		     + static_cast<size_t>( y - m_extentLoY );
	};

	auto isPassable = [&]( Int x, Int y ) -> Bool
	{
		if ( x < m_extentLoX || x > m_extentHiX || y < m_extentLoY || y > m_extentHiY )
			return false;
		PathfindCell* c = owner->getCell( LAYER_GROUND, x, y );
		if ( !c )
			return false;
		const LocomotorSurfaceTypeMask cellOK = ffSurfacesAllowedByCellType( c->getType() );
		return ( surfaces & cellOK ) != 0;
	};

	// Reject unreachable-to-self goal early.
	if ( !isPassable( goalCellX, goalCellY ) )
		return;

	// Binary min-heap keyed by (cost, seq). `seq` is a monotonically increasing
	// insertion counter that guarantees deterministic tie-breaking regardless
	// of thread scheduling — same input produces same flow on every client.
	struct Node { UnsignedInt cost; UnsignedInt seq; Int x; Int y; };
	struct NodeCmp
	{
		bool operator()( const Node& a, const Node& b ) const
		{
			if ( a.cost != b.cost ) return a.cost > b.cost;	// min-heap on cost
			return a.seq > b.seq;	// FIFO tiebreak
		}
	};

	std::vector<Node> heap;
	heap.reserve( 1024 );
	UnsignedInt seq = 0;

	const size_t goalIdx = cellIndex( goalCellX, goalCellY );
	cost[goalIdx] = 0;
	m_dir[goalIdx] = PF_FLOW_GOAL;
	heap.push_back( { 0, seq++, goalCellX, goalCellY } );
	std::push_heap( heap.begin(), heap.end(), NodeCmp() );

	while ( !heap.empty() )
	{
		std::pop_heap( heap.begin(), heap.end(), NodeCmp() );
		const Node cur = heap.back();
		heap.pop_back();

		const size_t ci = cellIndex( cur.x, cur.y );
		if ( cur.cost > cost[ci] )
			continue;	// stale entry

		for ( Int d = 0; d < 8; ++d )
		{
			const Int nx = cur.x + FF_DX[d];
			const Int ny = cur.y + FF_DY[d];
			if ( !isPassable( nx, ny ) )
				continue;

			// Diagonal moves require both adjacent orthogonal cells to be
			// passable too, matching the classic A* adjacency rule. This keeps
			// flow paths consistent with classic paths through pinch points.
			if ( d >= 4 )
			{
				if ( !isPassable( cur.x + FF_DX[d], cur.y ) )
					continue;
				if ( !isPassable( cur.x, cur.y + FF_DY[d] ) )
					continue;
			}

			const UnsignedInt ncost = cur.cost + static_cast<UnsignedInt>( FF_STEP[d] );
			const size_t ni = cellIndex( nx, ny );
			if ( ncost < cost[ni] )
			{
				cost[ni] = ncost;
				// The neighbour's flow direction points BACK toward the parent
				// (toward the goal). Dir `d` takes parent→neighbour, so the
				// neighbour's outbound step is the opposite.
				m_dir[ni] = FF_OPPOSITE[d];
				heap.push_back( { ncost, seq++, nx, ny } );
				std::push_heap( heap.begin(), heap.end(), NodeCmp() );
			}
		}
	}

	m_ready.store( true, std::memory_order_release );
}


//-----------------------------------------------------------------------------
// PathfindFlowFieldCache
//-----------------------------------------------------------------------------
PathfindFlowFieldCache::PathfindFlowFieldCache()
	: m_owner( nullptr )
	, m_maxEntries( 0 )
{
	m_extent.lo.x = m_extent.lo.y = 0;
	m_extent.hi.x = m_extent.hi.y = -1;
}

PathfindFlowFieldCache::~PathfindFlowFieldCache()
{
	release();
}

//-----------------------------------------------------------------------------
void PathfindFlowFieldCache::allocate( Pathfinder* owner, const IRegion2D& extent, size_t maxEntries )
{
	release();
	m_owner = owner;
	m_extent = extent;
	m_maxEntries = maxEntries;
}

//-----------------------------------------------------------------------------
void PathfindFlowFieldCache::release()
{
	m_lru.clear();
	m_owner = nullptr;
	m_maxEntries = 0;
	m_extent.lo.x = m_extent.lo.y = 0;
	m_extent.hi.x = m_extent.hi.y = -1;
}

//-----------------------------------------------------------------------------
PathfindFlowField* PathfindFlowFieldCache::getOrBuild( PFLocoClass cls, Int goalCellX, Int goalCellY )
{
	if ( !m_owner || m_maxEntries == 0 )
		return nullptr;
	if ( goalCellX < m_extent.lo.x || goalCellX > m_extent.hi.x
	  || goalCellY < m_extent.lo.y || goalCellY > m_extent.hi.y )
		return nullptr;

	// LRU probe — linear scan over at most m_maxEntries (~32) entries. O(1)
	// for practical cache sizes. std::list::splice moves the hit to the front.
	for ( auto it = m_lru.begin(); it != m_lru.end(); ++it )
	{
		if ( it->cls == cls && it->goalX == goalCellX && it->goalY == goalCellY )
		{
			m_lru.splice( m_lru.begin(), m_lru, it );
			return m_lru.front().field.get();
		}
	}

	// Miss — evict the LRU entry if at capacity, then build a fresh field.
	if ( m_lru.size() >= m_maxEntries )
		m_lru.pop_back();

	m_lru.emplace_front();
	Entry& e = m_lru.front();
	e.cls = cls;
	e.goalX = goalCellX;
	e.goalY = goalCellY;
	e.field.reset( new PathfindFlowField() );
	e.field->build( m_owner, m_extent, cls, goalCellX, goalCellY );

	if ( !e.field->isReady() )
	{
		// Build failed (unreachable goal) — drop the entry so a retry can
		// attempt again rather than caching a negative result forever.
		m_lru.pop_front();
		return nullptr;
	}

	return e.field.get();
}

//-----------------------------------------------------------------------------
void PathfindFlowFieldCache::invalidateAll()
{
	m_lru.clear();
}

//-----------------------------------------------------------------------------
size_t PathfindFlowFieldCache::totalBytes() const
{
	size_t b = 0;
	for ( const auto& e : m_lru )
		if ( e.field ) b += e.field->sizeBytes();
	return b;
}
