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

#pragma once

#ifndef _W3D_VERTEX_BUFFER_MANAGER
#define _W3D_VERTEX_BUFFER_MANAGER

#include "Lib/BaseType.h"

#ifdef BUILD_WITH_D3D11
#include <d3d11.h>
#endif

// Slot allocator for shadow-volume vertex / index memory. Ports the
// original W3DBufferManager (originally backed by D3D8 managed pool VBs)
// to DX11 dynamic buffers. Slots are written once via Map(NO_OVERWRITE) and
// then rendered many times; releasing a slot returns its range to a free
// list, keyed by size and FVF type, so future allocations of the same size
// skip the bookkeeping path.

#define MAX_VB_SIZES				128		// number of distinct VB slot sizes supported (slot size = (i+1) * MIN_SLOT_SIZE)
#define MIN_SLOT_SIZE				32		// minimum slot size in vertices; must be power of 2
#define MIN_SLOT_SIZE_SHIFT			5		// log2(MIN_SLOT_SIZE) — used to index into the free list
#define MAX_VERTEX_BUFFERS_CREATED	32		// cap on underlying VBs per format
#define DEFAULT_VERTEX_BUFFER_SIZE	8192	// ~256 KB for 32-byte vertices
#define MAX_NUMBER_SLOTS			4096	// cap on total slot wrapper objects (vertex OR index)

#define MAX_IB_SIZES				128		// distinct IB slot sizes (covers up to 65536 indices)
#define MAX_INDEX_BUFFERS_CREATED	32
#define DEFAULT_INDEX_BUFFER_SIZE	32768

class W3DBufferManager
{
public:
	// List of vertex formats needed by the original game. DX11 has no FVF,
	// so the enum is kept only as a key into a stride table and so callers
	// can keep their addresses (the shadow system only uses VBM_FVF_XYZ and
	// VBM_FVF_XYZD).
	enum VBM_FVF_TYPES
	{
		VBM_FVF_XYZ,			// position
		VBM_FVF_XYZD,			// position, diffuse
		VBM_FVF_XYZUV,			// position, uv
		VBM_FVF_XYZDUV,			// position, diffuse, uv
		VBM_FVF_XYZUV2,			// position, uv1, uv2
		VBM_FVF_XYZDUV2,		// position, diffuse, uv1, uv2
		VBM_FVF_XYZN,			// position, normal
		VBM_FVF_XYZND,			// position, normal, diffuse
		VBM_FVF_XYZNUV,			// position, normal, uv
		VBM_FVF_XYZNDUV,		// position, normal, diffuse, uv
		VBM_FVF_XYZNUV2,		// position, normal, uv1, uv2
		VBM_FVF_XYZNDUV2,		// position, normal, diffuse, uv1, uv2
		VBM_FVF_XYZRHW,			// transformed position
		VBM_FVF_XYZRHWD,
		VBM_FVF_XYZRHWUV,
		VBM_FVF_XYZRHWDUV,
		VBM_FVF_XYZRHWUV2,
		VBM_FVF_XYZRHWDUV2,
		MAX_FVF
	};

	// Render-task node — shadow volumes thread their draw calls through
	// their owning VB so that all draws using the same buffer are grouped.
	// Kept as a bare linked-list node; users embed or derive from this.
	struct W3DRenderTask
	{
		W3DRenderTask* m_nextTask;
	};

	struct W3DVertexBuffer;
	struct W3DIndexBuffer;

	struct W3DVertexBufferSlot
	{
		Int					m_size;			// number of vertices reserved
		Int					m_start;			// first vertex of this slot inside m_VB
		W3DVertexBuffer*	m_VB;
		W3DVertexBufferSlot*	m_prevSameSize;	// free-list links, valid only when on the free list
		W3DVertexBufferSlot*	m_nextSameSize;
		W3DVertexBufferSlot*	m_prevSameVB;		// per-VB used-list links, valid while slot is live
		W3DVertexBufferSlot*	m_nextSameVB;
	};

	struct W3DVertexBuffer
	{
		VBM_FVF_TYPES			m_format;
		W3DVertexBufferSlot*	m_usedSlots;
		Int						m_startFreeIndex;	// first free vertex index after the last allocation
		Int						m_size;				// VB capacity in vertices
		W3DVertexBuffer*		m_nextVB;
#ifdef BUILD_WITH_D3D11
		ID3D11Buffer*			m_buffer;			// dynamic VB, CPU-write
#endif
		W3DRenderTask*			m_renderTaskList;	// set by shadow code, walked at draw time
	};

	struct W3DIndexBufferSlot
	{
		Int					m_size;			// number of indices reserved
		Int					m_start;			// first index of this slot inside m_IB
		W3DIndexBuffer*	m_IB;
		W3DIndexBufferSlot*	m_prevSameSize;
		W3DIndexBufferSlot*	m_nextSameSize;
		W3DIndexBufferSlot*	m_prevSameIB;
		W3DIndexBufferSlot*	m_nextSameIB;
	};

	struct W3DIndexBuffer
	{
		W3DIndexBufferSlot*	m_usedSlots;
		Int					m_startFreeIndex;
		Int					m_size;
		W3DIndexBuffer*		m_nextIB;
#ifdef BUILD_WITH_D3D11
		ID3D11Buffer*		m_buffer;			// dynamic IB (16-bit indices), CPU-write
#endif
	};

	W3DBufferManager(void);
	~W3DBufferManager(void);

	// Reserve a vertex / index slot of the requested size. Returns nullptr
	// if the pool is exhausted or the underlying device buffer fails to
	// allocate. Sizes are rounded up to the next MIN_SLOT_SIZE multiple.
	W3DVertexBufferSlot*	getSlot(VBM_FVF_TYPES fvfType, Int size);
	W3DIndexBufferSlot*		getSlot(Int size);

	// Return a slot to the free list. The underlying device memory is
	// recycled the next time a same-sized slot is requested.
	void releaseSlot(W3DVertexBufferSlot* vbSlot);
	void releaseSlot(W3DIndexBufferSlot* ibSlot);

	// Drop every slot (both live and free-list) back to the empty pool.
	// Per-frame housekeeping; the underlying VBs/IBs are kept.
	void freeAllSlots(void);
	// Destroy every underlying VB/IB as well. Called on full shutdown.
	void freeAllBuffers(void);

	void ReleaseResources(void);
	Bool ReAcquireResources(void);

	// Iterator — pass nullptr to get the first VB of the given format,
	// then pass each previous result. Used by the renderer to batch draws
	// by buffer.
	W3DVertexBuffer* getNextVertexBuffer(W3DVertexBuffer* pVb, VBM_FVF_TYPES type)
	{
		if (pVb == nullptr)
			return m_W3DVertexBuffers[type];
		return pVb->m_nextVB;
	}

	// Size of one vertex in bytes for the given format. Replaces the
	// original's D3DFVF lookup — DX11 has no FVF.
	static Int getStride(VBM_FVF_TYPES format);

#ifdef BUILD_WITH_D3D11
	// Map the owning VB / IB for a slot-sized write. `discardFullBuffer`
	// picks WRITE_DISCARD (fresh frame, starts with a stale GPU copy) vs.
	// WRITE_NO_OVERWRITE (subsequent slot writes in the same frame).
	// Returned pointer already indexes into the slot's start; the caller
	// writes `slot->m_size` vertices / indices and then calls the matching
	// unmap. Returns nullptr on Map failure.
	void* mapSlot(W3DVertexBufferSlot* slot, Bool discardFullBuffer);
	void* mapSlot(W3DIndexBufferSlot* slot, Bool discardFullBuffer);
	void unmapSlot(W3DVertexBufferSlot* slot);
	void unmapSlot(W3DIndexBufferSlot* slot);
#endif

protected:
	W3DVertexBufferSlot*	m_W3DVertexBufferSlots[MAX_FVF][MAX_VB_SIZES];	// free list per (format, size) bucket
	W3DVertexBuffer*		m_W3DVertexBuffers[MAX_FVF];						// live VB list per format
	W3DVertexBufferSlot		m_W3DVertexBufferEmptySlots[MAX_NUMBER_SLOTS];	// flat pool of uninitialized slot wrappers
	Int						m_numEmptySlotsAllocated;
	W3DVertexBuffer			m_W3DEmptyVertexBuffers[MAX_VERTEX_BUFFERS_CREATED];
	Int						m_numEmptyVertexBuffersAllocated;

	W3DIndexBufferSlot*		m_W3DIndexBufferSlots[MAX_IB_SIZES];			// free list per size bucket
	W3DIndexBuffer*			m_W3DIndexBuffers;
	W3DIndexBufferSlot		m_W3DIndexBufferEmptySlots[MAX_NUMBER_SLOTS];
	Int						m_numEmptyIndexSlotsAllocated;
	W3DIndexBuffer			m_W3DEmptyIndexBuffers[MAX_INDEX_BUFFERS_CREATED];
	Int						m_numEmptyIndexBuffersAllocated;

	W3DVertexBufferSlot*	allocateSlotStorage(VBM_FVF_TYPES fvfType, Int size);
	W3DIndexBufferSlot*		allocateSlotStorage(Int size);

#ifdef BUILD_WITH_D3D11
	Bool createDeviceVertexBuffer(W3DVertexBuffer* vb);
	Bool createDeviceIndexBuffer(W3DIndexBuffer* ib);
#endif
};

extern W3DBufferManager* TheW3DBufferManager;

#endif //_W3D_VERTEX_BUFFER_MANAGER
