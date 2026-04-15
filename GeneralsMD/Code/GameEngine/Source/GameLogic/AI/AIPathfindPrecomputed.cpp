/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// AIPathfindPrecomputed.cpp
// Deterministic JPS+ jump-table builder. See AIPathfindPrecomputed.h for
// the data contract and invariants.

#include "PreRTS.h"

#include "GameLogic/AIPathfindPrecomputed.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <thread>
#include <vector>

#include "Common/LivePerf.h"
#include "GameLogic/AIPathfind.h"

//-----------------------------------------------------------------------------
// Direction table. Order matches delta[] in Pathfinder::examineNeighboringCells.
//-----------------------------------------------------------------------------
const Int PF_DIR_DX[PF_DIR_COUNT] = {  1,  0, -1,  0,   1, -1, -1,  1 };
const Int PF_DIR_DY[PF_DIR_COUNT] = {  0,  1,  0, -1,   1,  1, -1, -1 };

//-----------------------------------------------------------------------------
// Perpendicular neighbours for straight-direction forced-neighbour detection.
// For each straight dir (E/N/W/S), the two "side" offsets that, when blocked,
// with an open corresponding diagonal produce a forced neighbour.
//   straightDir -> { sideA_dx, sideA_dy, diagA_dx, diagA_dy,
//                    sideB_dx, sideB_dy, diagB_dx, diagB_dy }
//-----------------------------------------------------------------------------
struct ForcedSides
{
	Int sideA_dx, sideA_dy, diagA_dx, diagA_dy;
	Int sideB_dx, sideB_dy, diagB_dx, diagB_dy;
};

static const ForcedSides s_forcedForDir[4] = {
	// E: sides N/S, diagonals NE/SE
	{  0,  1,  1,  1,    0, -1,  1, -1 },
	// N: sides E/W, diagonals NE/NW
	{  1,  0,  1,  1,   -1,  0, -1,  1 },
	// W: sides N/S, diagonals NW/SW
	{  0,  1, -1,  1,    0, -1, -1, -1 },
	// S: sides E/W, diagonals SE/SW
	{  1,  0,  1, -1,   -1,  0, -1, -1 },
};

//-----------------------------------------------------------------------------
// Classifier — mirrors the surface masks that PathfindZoneManager keys off of.
// Deterministic: same inputs => same class. Unknown combinations fall through
// to GROUND, which is a safe under-approximation (forces classic A* fallback
// if the table doesn't cover the unit's effective movement).
//-----------------------------------------------------------------------------
/*static*/ PFLocoClass PathfindPrecomputed::classifyLocomotorSet( LocomotorSurfaceTypeMask surfaces, Bool isCrusher )
{
	if ( isCrusher )
		return PFLocoClass::CRUSHER;
	if ( surfaces & LOCOMOTORSURFACE_WATER )
		return PFLocoClass::AMPHIBIOUS;
	if ( surfaces & LOCOMOTORSURFACE_CLIFF )
		return PFLocoClass::CLIFF_CLIMBER;
	if ( surfaces & LOCOMOTORSURFACE_RUBBLE )
		return PFLocoClass::RUBBLE_WALKER;
	return PFLocoClass::GROUND;
}

//-----------------------------------------------------------------------------
// Construction / destruction
//-----------------------------------------------------------------------------
PathfindPrecomputed::PathfindPrecomputed()
	: m_owner( nullptr )
	, m_extentLoX( 0 ), m_extentLoY( 0 )
	, m_extentHiX( -1 ), m_extentHiY( -1 )
	, m_width( 0 ), m_height( 0 )
	, m_zoneDistStride( 0 )
	, m_zoneDistReady( false )
{
	for ( Int i = 0; i < static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ); ++i )
		m_status[i].store( static_cast<Int>( PFBuildStatus::IDLE ), std::memory_order_relaxed );
}

PathfindPrecomputed::~PathfindPrecomputed()
{
	waitForAsync();
	release();
}

//-----------------------------------------------------------------------------
void PathfindPrecomputed::allocate( Pathfinder* owner, const IRegion2D& extent )
{
	waitForAsync();
	release();

	m_owner = owner;
	m_extentLoX = extent.lo.x;
	m_extentLoY = extent.lo.y;
	m_extentHiX = extent.hi.x;
	m_extentHiY = extent.hi.y;
	m_width  = m_extentHiX - m_extentLoX + 1;
	m_height = m_extentHiY - m_extentLoY + 1;

	if ( m_width <= 0 || m_height <= 0 )
		return;

	const size_t cells = static_cast<size_t>( m_width ) * static_cast<size_t>( m_height );
	const size_t values = cells * PF_DIR_COUNT;

	for ( Int i = 0; i < static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ); ++i )
	{
		m_jump[i].assign( values, static_cast<PFJumpVal>( 0 ) );
		m_status[i].store( static_cast<Int>( PFBuildStatus::BUILDING ), std::memory_order_relaxed );
	}

	// Zone-distance table sized to fit whatever zone IDs we'll see. The 14-bit
	// m_zone field caps at 16384; typical maps use 100-500 zones. Size lazily
	// at build time in buildZoneDistanceTable (we scan for the max used ID).
	m_zoneDist.clear();
	m_zoneDistStride = 0;
	m_zoneDistReady.store( false, std::memory_order_relaxed );
}

//-----------------------------------------------------------------------------
void PathfindPrecomputed::release()
{
	waitForAsync();

	for ( Int i = 0; i < static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ); ++i )
	{
		m_jump[i].clear();
		m_jump[i].shrink_to_fit();
		m_status[i].store( static_cast<Int>( PFBuildStatus::IDLE ), std::memory_order_relaxed );
	}
	m_zoneDist.clear();
	m_zoneDist.shrink_to_fit();
	m_zoneDistStride = 0;
	m_zoneDistReady.store( false, std::memory_order_relaxed );

	m_owner = nullptr;
	m_width = m_height = 0;
	m_extentLoX = m_extentLoY = 0;
	m_extentHiX = m_extentHiY = -1;
}

//-----------------------------------------------------------------------------
Bool PathfindPrecomputed::isReady( PFLocoClass cls ) const
{
	return m_status[static_cast<Int>( cls )].load( std::memory_order_acquire )
		== static_cast<Int>( PFBuildStatus::READY );
}

//-----------------------------------------------------------------------------
size_t PathfindPrecomputed::totalBytes() const
{
	size_t b = 0;
	for ( Int i = 0; i < static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ); ++i )
		b += m_jump[i].capacity() * sizeof( PFJumpVal );
	return b;
}

//-----------------------------------------------------------------------------
// Passability tests. These read Pathfinder::m_map via the owner, so they MUST
// run on the same thread that owns Pathfinder unless the caller guarantees the
// grid is not being mutated (true at map load and inside a Pathfinder method).
//-----------------------------------------------------------------------------
static inline LocomotorSurfaceTypeMask surfacesFor( PFLocoClass cls )
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

Bool PathfindPrecomputed::isUniform( Int cellX, Int cellY, PFLocoClass cls ) const
{
	if ( !inExtent( cellX, cellY ) )
		return false;
	if ( !m_owner )
		return false;

	PathfindCell* c = m_owner->getCell( LAYER_GROUND, cellX, cellY );
	if ( !c )
		return false;

	// Accept only CELL_CLEAR (and CELL_WATER for amphibious). Everything else —
	// cliff, rubble, obstacle, impassable, bridge impassable — is a "cost
	// island" that breaks the uniform-cost assumption behind jumps.
	const PathfindCell::CellType t = c->getType();
	if ( t != PathfindCell::CELL_CLEAR )
	{
		if ( !( cls == PFLocoClass::AMPHIBIOUS && t == PathfindCell::CELL_WATER ) )
			return false;
	}

	// Pinched cells carry a +COST_ORTHOGONAL penalty in examineNeighboringCells,
	// so treat them as non-uniform: jumps will stop at (not over) them.
	if ( c->getPinched() )
		return false;

	// A crusher ignores fence obstacles, but those are still CELL_OBSTACLE —
	// handled by the t != CELL_CLEAR check above.
	(void)cls;
	return true;
}

// Mirror of Pathfinder::validLocomotorSurfacesForCellType — kept here so the
// precomp doesn't need friend access to a protected static. If either copy
// changes, both must change; the rule is small enough that divergence would
// be caught immediately by path-diff determinism tests.
static LocomotorSurfaceTypeMask surfacesAllowedByCellType( PathfindCell::CellType t )
{
	switch ( t )
	{
		case PathfindCell::CELL_CLEAR:
			return LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_WATER:
			return LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_CLIFF:
			return LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_RUBBLE:
			return LOCOMOTORSURFACE_RUBBLE | LOCOMOTORSURFACE_AIR;
		case PathfindCell::CELL_OBSTACLE:
		case PathfindCell::CELL_BRIDGE_IMPASSABLE:
		case PathfindCell::CELL_IMPASSABLE:
			return LOCOMOTORSURFACE_AIR;
		default:
			return NO_SURFACES;
	}
}

Bool PathfindPrecomputed::isBlocked( Int cellX, Int cellY, PFLocoClass cls ) const
{
	if ( !inExtent( cellX, cellY ) )
		return true;	// off-map counts as blocked for forced-neighbour detection
	if ( !m_owner )
		return true;

	PathfindCell* c = m_owner->getCell( LAYER_GROUND, cellX, cellY );
	if ( !c )
		return true;

	const LocomotorSurfaceTypeMask surfaces = surfacesFor( cls );
	const LocomotorSurfaceTypeMask cellOK = surfacesAllowedByCellType( c->getType() );
	return ( surfaces & cellOK ) == 0;
}

//-----------------------------------------------------------------------------
// Straight-direction sweep (E / N / W / S). Walks cells in reverse of `dir`
// so that each cell's jump distance depends only on cells already processed.
// This is the canonical JPS+ precompute for 4-connected moves.
//-----------------------------------------------------------------------------
void PathfindPrecomputed::sweepStraight( PFLocoClass cls, Int dir )
{
	const Int dx = PF_DIR_DX[dir];
	const Int dy = PF_DIR_DY[dir];
	const ForcedSides& fs = s_forcedForDir[dir];
	PFJumpVal* const table = m_jump[static_cast<Int>( cls )].data();

	// Walk in the reverse of `dir`. Whichever axis is nonzero defines the
	// sweep axis; the other is the outer loop.
	const Int stepX = -dx;
	const Int stepY = -dy;

	// For each row (if sweeping along X) or column (along Y), iterate backwards.
	if ( dx != 0 )
	{
		for ( Int y = m_extentLoY; y <= m_extentHiY; ++y )
		{
			// Starting cell for the sweep is the far end of the row.
			const Int xStart = ( dx > 0 ) ? m_extentHiX : m_extentLoX;
			Int runOpen = 0;				// open cells ahead (for negative encoding)
			Bool runHasJumpPoint = false;	// once we see a jump point, propagate positive distance
			Int distToJump = 0;

			for ( Int x = xStart; x >= m_extentLoX && x <= m_extentHiX; x += stepX )
			{
				PFJumpVal v = 0;

				// Check the cell we'd step INTO (x + dx, y + dy = y).
				const Int nx = x + dx;
				const Int ny = y;

				if ( !inExtent( nx, ny ) || !isUniform( nx, ny, cls ) )
				{
					// Adjacent cell is blocked / non-uniform → blocked step.
					v = 0;
					runOpen = 0;
					runHasJumpPoint = false;
					distToJump = 0;
				}
				else
				{
					// The cell we're stepping into is uniform. Is it a jump point?
					// It is iff entering it from direction `dir` exposes a forced
					// neighbour: a blocked side and a clear diagonal on that side.
					const Bool sideABlocked = isBlocked( nx + fs.sideA_dx, ny + fs.sideA_dy, cls );
					const Bool diagAClear   = !isBlocked( nx + fs.diagA_dx, ny + fs.diagA_dy, cls );
					const Bool sideBBlocked = isBlocked( nx + fs.sideB_dx, ny + fs.sideB_dy, cls );
					const Bool diagBClear   = !isBlocked( nx + fs.diagB_dx, ny + fs.diagB_dy, cls );
					const Bool entryCellIsJP = ( sideABlocked && diagAClear ) || ( sideBBlocked && diagBClear );

					if ( entryCellIsJP )
					{
						v = 1;
						runHasJumpPoint = true;
						distToJump = 1;
						runOpen = 1;
					}
					else if ( runHasJumpPoint )
					{
						// Extend the distance-to-jump-point from the last one.
						distToJump += 1;
						v = static_cast<PFJumpVal>( distToJump );
						runOpen += 1;
					}
					else
					{
						// Open cell with no jump point ahead yet; encode as negative.
						runOpen += 1;
						v = static_cast<PFJumpVal>( -runOpen );
					}
				}

				table[cellIndex( x, y ) * PF_DIR_COUNT + dir] = v;
			}
		}
	}
	else // dy != 0
	{
		for ( Int x = m_extentLoX; x <= m_extentHiX; ++x )
		{
			const Int yStart = ( dy > 0 ) ? m_extentHiY : m_extentLoY;
			Int runOpen = 0;
			Bool runHasJumpPoint = false;
			Int distToJump = 0;

			for ( Int y = yStart; y >= m_extentLoY && y <= m_extentHiY; y += stepY )
			{
				PFJumpVal v = 0;

				const Int nx = x;
				const Int ny = y + dy;

				if ( !inExtent( nx, ny ) || !isUniform( nx, ny, cls ) )
				{
					v = 0;
					runOpen = 0;
					runHasJumpPoint = false;
					distToJump = 0;
				}
				else
				{
					const Bool sideABlocked = isBlocked( nx + fs.sideA_dx, ny + fs.sideA_dy, cls );
					const Bool diagAClear   = !isBlocked( nx + fs.diagA_dx, ny + fs.diagA_dy, cls );
					const Bool sideBBlocked = isBlocked( nx + fs.sideB_dx, ny + fs.sideB_dy, cls );
					const Bool diagBClear   = !isBlocked( nx + fs.diagB_dx, ny + fs.diagB_dy, cls );
					const Bool entryCellIsJP = ( sideABlocked && diagAClear ) || ( sideBBlocked && diagBClear );

					if ( entryCellIsJP )
					{
						v = 1;
						runHasJumpPoint = true;
						distToJump = 1;
						runOpen = 1;
					}
					else if ( runHasJumpPoint )
					{
						distToJump += 1;
						v = static_cast<PFJumpVal>( distToJump );
						runOpen += 1;
					}
					else
					{
						runOpen += 1;
						v = static_cast<PFJumpVal>( -runOpen );
					}
				}

				table[cellIndex( x, y ) * PF_DIR_COUNT + dir] = v;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Diagonal-direction sweep. Walks each diagonal line and records, for each
// cell, the distance along that diagonal to the nearest diagonal jump point.
// A cell c_s on the diagonal from c is a diagonal jump point iff one of its
// straight-component sweeps (already computed) gives a positive jump value —
// meaning moving straight from c_s would reach a forced neighbour, so a
// diagonal mover passing through c_s benefits from stopping there.
//
// Relies on the straight-direction tables being fully built. The caller
// (buildClass) guarantees that ordering.
//-----------------------------------------------------------------------------
void PathfindPrecomputed::sweepDiagonal( PFLocoClass cls, Int dir )
{
	const Int dx = PF_DIR_DX[dir];
	const Int dy = PF_DIR_DY[dir];
	PFJumpVal* const table = m_jump[static_cast<Int>( cls )].data();

	// The x- and y-component "straight" direction indices that make up the
	// diagonal. E.g., NE = E + N, so the components are PF_DIR_E and PF_DIR_N.
	Int dirX = -1, dirY = -1;
	for ( Int i = 0; i < 4; ++i )
	{
		if ( PF_DIR_DX[i] == dx && PF_DIR_DY[i] == 0 ) dirX = i;
		if ( PF_DIR_DX[i] == 0 && PF_DIR_DY[i] == dy ) dirY = i;
	}
	if ( dirX < 0 || dirY < 0 )
		return;	// unexpected direction

	// For each diagonal line, walk backwards (opposite of dir) so each cell's
	// result can inherit from the cell it steps into.
	const Int stepX = -dx;
	const Int stepY = -dy;

	// Iterate over every cell as a starting point, but the backward-walk
	// convention means we process the "end of each diagonal" first, then its
	// predecessors. Simpler: iterate all cells in the reverse raster order
	// that matches the sweep direction.
	const Int xStart = ( dx > 0 ) ? m_extentHiX : m_extentLoX;
	const Int xEnd   = ( dx > 0 ) ? m_extentLoX : m_extentHiX;
	const Int yStart = ( dy > 0 ) ? m_extentHiY : m_extentLoY;
	const Int yEnd   = ( dy > 0 ) ? m_extentLoY : m_extentHiY;

	const Int xDir = ( dx > 0 ) ? -1 : 1;
	const Int yDir = ( dy > 0 ) ? -1 : 1;

	for ( Int y = yStart; y != yEnd + yDir; y += yDir )
	{
		for ( Int x = xStart; x != xEnd + xDir; x += xDir )
		{
			PFJumpVal v = 0;

			const Int nx = x + dx;
			const Int ny = y + dy;

			if ( !inExtent( nx, ny ) || !isUniform( nx, ny, cls ) )
			{
				v = 0;
			}
			else
			{
				// Moving diagonally also requires the two orthogonal components
				// of the step (the "adjacent sides") to be passable, matching
				// the classic adjacency check in examineNeighboringCells.
				const Bool sideXPassable = isUniform( x + dx, y, cls );
				const Bool sideYPassable = isUniform( x, y + dy, cls );
				if ( !sideXPassable || !sideYPassable )
				{
					v = 0;
				}
				else
				{
					// Is the cell we'd step into a diagonal jump point? It is
					// iff stepping straight from (nx,ny) in either component
					// direction would hit a straight jump point. The straight
					// tables encode that: a positive value means a jump point
					// exists along that ray.
					const PFJumpVal jx = table[cellIndex( nx, ny ) * PF_DIR_COUNT + dirX];
					const PFJumpVal jy = table[cellIndex( nx, ny ) * PF_DIR_COUNT + dirY];
					const Bool entryIsJP = ( jx > 0 ) || ( jy > 0 );

					if ( entryIsJP )
					{
						v = 1;
					}
					else
					{
						// Inherit from the next cell along this diagonal.
						const PFJumpVal next = table[cellIndex( nx, ny ) * PF_DIR_COUNT + dir];
						if ( next > 0 )
							v = static_cast<PFJumpVal>( next + 1 );
						else if ( next < 0 )
							v = static_cast<PFJumpVal>( next - 1 );
						else
							v = -1;	// one-step open run; no JP ahead
					}
				}
			}

			table[cellIndex( x, y ) * PF_DIR_COUNT + dir] = v;
			// Unused loop-var silencer for early-continue compilers:
			(void)stepX; (void)stepY;
		}
	}
}

//-----------------------------------------------------------------------------
// Zone-distance table. Scans the grid once to find the max raw zone ID in use
// and the adjacency graph between raw zones (two zones are adjacent iff an
// orthogonal pair of cells with those zones exists). Then runs multi-source
// BFS from each zone to compute the min number of zone-boundary crossings to
// reach every other zone.
//
// The result is an admissible lower bound on cell cost: each crossing needs
// at least one cell step, so N crossings implies cost ≥ N × COST_ORTHOGONAL.
// Class-independent because it operates on raw zones — the classifier's
// effective-zone mapping handles per-unit passability at query time.
//-----------------------------------------------------------------------------
void PathfindPrecomputed::buildZoneDistanceTable()
{
	m_zoneDistReady.store( false, std::memory_order_release );
	m_zoneDist.clear();
	m_zoneDistStride = 0;

	if ( !m_owner || m_width <= 0 || m_height <= 0 )
		return;

	// Pass 1: find max zone ID in use.
	zoneStorageType maxZone = 0;
	for ( Int y = m_extentLoY; y <= m_extentHiY; ++y )
	{
		for ( Int x = m_extentLoX; x <= m_extentHiX; ++x )
		{
			PathfindCell* c = m_owner->getCell( LAYER_GROUND, x, y );
			if ( !c ) continue;
			const zoneStorageType z = c->getZone();
			if ( z > maxZone ) maxZone = z;
		}
	}

	if ( maxZone == 0 )
	{
		// Map has no classified zones; leave table empty.
		m_zoneDistStride = 0;
		return;
	}

	const Int numZones = static_cast<Int>( maxZone ) + 1;
	m_zoneDistStride = numZones;

	// Pass 2: build adjacency as sorted neighbour lists per zone.
	std::vector<std::vector<zoneStorageType>> adj( numZones );
	for ( Int y = m_extentLoY; y <= m_extentHiY; ++y )
	{
		for ( Int x = m_extentLoX; x <= m_extentHiX; ++x )
		{
			PathfindCell* c = m_owner->getCell( LAYER_GROUND, x, y );
			if ( !c ) continue;
			const zoneStorageType zHere = c->getZone();

			// Check E and N neighbours (W/S are implied by symmetry).
			const Int nbx[2] = { x + 1, x     };
			const Int nby[2] = { y,     y + 1 };
			for ( Int k = 0; k < 2; ++k )
			{
				if ( !inExtent( nbx[k], nby[k] ) ) continue;
				PathfindCell* n = m_owner->getCell( LAYER_GROUND, nbx[k], nby[k] );
				if ( !n ) continue;
				const zoneStorageType zN = n->getZone();
				if ( zN != zHere )
				{
					adj[zHere].push_back( zN );
					adj[zN].push_back( zHere );
				}
			}
		}
	}
	for ( Int z = 0; z < numZones; ++z )
	{
		std::sort( adj[z].begin(), adj[z].end() );
		adj[z].erase( std::unique( adj[z].begin(), adj[z].end() ), adj[z].end() );
	}

	// Pass 3: BFS from each zone, write distances. Store in a flat array so
	// the lookup in zoneDistanceCost is a single indexed load.
	const UnsignedShort INF = std::numeric_limits<UnsignedShort>::max();
	m_zoneDist.assign( static_cast<size_t>( numZones ) * numZones, INF );

	std::vector<UnsignedShort> dist( numZones );
	std::vector<zoneStorageType> frontier;
	std::vector<zoneStorageType> nextFrontier;
	frontier.reserve( 64 );
	nextFrontier.reserve( 64 );

	for ( Int src = 0; src < numZones; ++src )
	{
		std::fill( dist.begin(), dist.end(), INF );
		dist[src] = 0;
		frontier.clear();
		frontier.push_back( static_cast<zoneStorageType>( src ) );
		UnsignedShort level = 0;
		while ( !frontier.empty() )
		{
			const UnsignedShort nextLevel = static_cast<UnsignedShort>( level + 1 );
			nextFrontier.clear();
			for ( zoneStorageType z : frontier )
			{
				for ( zoneStorageType nb : adj[z] )
				{
					if ( dist[nb] == INF )
					{
						dist[nb] = nextLevel;
						nextFrontier.push_back( nb );
					}
				}
			}
			frontier.swap( nextFrontier );
			level = nextLevel;
		}
		UnsignedShort* row = m_zoneDist.data() + static_cast<size_t>( src ) * m_zoneDistStride;
		for ( Int dst = 0; dst < numZones; ++dst )
			row[dst] = dist[dst];
	}

	m_zoneDistReady.store( true, std::memory_order_release );
}

//-----------------------------------------------------------------------------
UnsignedInt PathfindPrecomputed::zoneDistanceCost( zoneStorageType zoneA, zoneStorageType zoneB ) const
{
	if ( !m_zoneDistReady.load( std::memory_order_acquire ) )
		return 0;
	if ( zoneA == zoneB )
		return 0;

	const Int a = static_cast<Int>( zoneA );
	const Int b = static_cast<Int>( zoneB );
	if ( a >= m_zoneDistStride || b >= m_zoneDistStride )
		return 0;

	const UnsignedShort hops = m_zoneDist[static_cast<size_t>( a ) * m_zoneDistStride + b];
	if ( hops == std::numeric_limits<UnsignedShort>::max() )
		return 0;	// disconnected — fall back to octile heuristic

	// COST_ORTHOGONAL lives in AIPathfind.cpp as an anonymous-scope const, so
	// duplicate the value here. If the game ever changes it, both copies must
	// update in lockstep (verified by the per-step cell-count match).
	const UnsignedInt COST_ORTHOGONAL_LOCAL = 10;
	return static_cast<UnsignedInt>( hops ) * COST_ORTHOGONAL_LOCAL;
}

//-----------------------------------------------------------------------------
void PathfindPrecomputed::buildClass( PFLocoClass cls )
{
	m_status[static_cast<Int>( cls )].store(
		static_cast<Int>( PFBuildStatus::BUILDING ), std::memory_order_relaxed );

	// 4 straight directions first (E, N, W, S) — these are the ones we use.
	sweepStraight( cls, PF_DIR_E );
	sweepStraight( cls, PF_DIR_N );
	sweepStraight( cls, PF_DIR_W );
	sweepStraight( cls, PF_DIR_S );

	// Diagonal stubs reserved for phase 2.
	sweepDiagonal( cls, PF_DIR_NE );
	sweepDiagonal( cls, PF_DIR_NW );
	sweepDiagonal( cls, PF_DIR_SW );
	sweepDiagonal( cls, PF_DIR_SE );

	// Release-order store so any reader observing READY will see a fully
	// populated table. Readers use an acquire-load.
	m_status[static_cast<Int>( cls )].store(
		static_cast<Int>( PFBuildStatus::READY ), std::memory_order_release );
}

//-----------------------------------------------------------------------------
void PathfindPrecomputed::buildAll()
{
	LIVE_PERF_SCOPE( "Pathfinder::precomputeBuild" );
	if ( !m_owner || m_width <= 0 || m_height <= 0 )
		return;

	// Fan out: one worker per class for jump tables, one for the zone-distance
	// table. All write disjoint memory; read-only access to the pathfind grid
	// is safe during map-load. Determinism: thread scheduling only affects
	// completion order, never output values.
	const Int classCount = static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT );
	std::vector<std::thread> workers;
	workers.reserve( classCount + 1 );
	for ( Int i = 0; i < classCount; ++i )
	{
		const PFLocoClass cls = static_cast<PFLocoClass>( i );
		workers.emplace_back( [this, cls]() { this->buildClass( cls ); } );
	}
	workers.emplace_back( [this]() { this->buildZoneDistanceTable(); } );
	for ( std::thread& t : workers )
		t.join();
}

//-----------------------------------------------------------------------------
// Async rebuild. Spawned after the engine's zone recalc so the tables
// re-reflect new obstacles; main thread keeps pathfinding (with classic
// fallback) until the workers publish READY. Detached-style ownership: the
// handles live in m_asyncThreads until waitForAsync() joins them. We don't
// overlap rebuilds — a fresh call first joins any in-flight workers.
//-----------------------------------------------------------------------------
void PathfindPrecomputed::rebuildAsync()
{
	waitForAsync();
	if ( !m_owner || m_width <= 0 || m_height <= 0 )
		return;

	// Flip status to BUILDING so consumers see fresh tables as "not ready" and
	// fall back to classic A* until the worker is done.
	for ( Int i = 0; i < static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ); ++i )
		m_status[i].store( static_cast<Int>( PFBuildStatus::BUILDING ), std::memory_order_relaxed );
	m_zoneDistReady.store( false, std::memory_order_relaxed );

	m_asyncThreads.reserve( static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ) + 1 );
	for ( Int i = 0; i < static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ); ++i )
	{
		const PFLocoClass cls = static_cast<PFLocoClass>( i );
		std::thread* t = new std::thread( [this, cls]() { this->buildClass( cls ); } );
		m_asyncThreads.push_back( t );
	}
	{
		std::thread* t = new std::thread( [this]() { this->buildZoneDistanceTable(); } );
		m_asyncThreads.push_back( t );
	}
}

//-----------------------------------------------------------------------------
void PathfindPrecomputed::waitForAsync()
{
	for ( void* p : m_asyncThreads )
	{
		std::thread* t = static_cast<std::thread*>( p );
		if ( t )
		{
			if ( t->joinable() )
				t->join();
			delete t;
		}
	}
	m_asyncThreads.clear();
}

//-----------------------------------------------------------------------------
void PathfindPrecomputed::invalidateAll()
{
	for ( Int i = 0; i < static_cast<Int>( PFLocoClass::PF_LOCO_CLASS_COUNT ); ++i )
		m_status[i].store( static_cast<Int>( PFBuildStatus::BUILDING ), std::memory_order_relaxed );
}
