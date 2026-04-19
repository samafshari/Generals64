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

// Slot allocator behind the shadow-volume renderer. The original D3D8
// version handed out slots inside managed-pool VBs; here those are DX11
// dynamic buffers (CPU_ACCESS_WRITE) mapped per-slot with WRITE_NO_OVERWRITE
// so partial writes don't stomp sibling slots the GPU is still reading.

#include "always.h"
#include "Common/Debug.h"
#include "W3DDevice/GameClient/W3DBufferManager.h"

#ifdef BUILD_WITH_D3D11
#include "Renderer.h"
#include "Core/Device.h"
#endif

W3DBufferManager* TheW3DBufferManager = nullptr;

// Byte stride for each vertex format. Drives ID3D11Buffer ByteWidth on
// creation and slot-offset arithmetic on Map. Matches the original D3DFVF
// layout bit-for-bit so slot data laid out with the original formulas just
// works.
static const Int kFvfStrideTable[W3DBufferManager::MAX_FVF] =
{
	12,		// VBM_FVF_XYZ			  — 3f pos
	16,		// VBM_FVF_XYZD			  — 3f pos + DWORD diffuse
	20,		// VBM_FVF_XYZUV		  — 3f pos + 2f uv
	24,		// VBM_FVF_XYZDUV		  — 3f pos + DWORD diffuse + 2f uv
	28,		// VBM_FVF_XYZUV2		  — 3f pos + 2f uv + 2f uv
	32,		// VBM_FVF_XYZDUV2
	24,		// VBM_FVF_XYZN			  — 3f pos + 3f normal
	28,		// VBM_FVF_XYZND
	32,		// VBM_FVF_XYZNUV
	36,		// VBM_FVF_XYZNDUV
	40,		// VBM_FVF_XYZNUV2
	44,		// VBM_FVF_XYZNDUV2
	16,		// VBM_FVF_XYZRHW		  — 4f transformed
	20,		// VBM_FVF_XYZRHWD
	24,		// VBM_FVF_XYZRHWUV
	28,		// VBM_FVF_XYZRHWDUV
	32,		// VBM_FVF_XYZRHWUV2
	36,		// VBM_FVF_XYZRHWDUV2
};

Int W3DBufferManager::getStride(VBM_FVF_TYPES format)
{
	return kFvfStrideTable[format];
}

W3DBufferManager::W3DBufferManager(void)
	: m_numEmptySlotsAllocated(0)
	, m_numEmptyVertexBuffersAllocated(0)
	, m_W3DIndexBuffers(nullptr)
	, m_numEmptyIndexSlotsAllocated(0)
	, m_numEmptyIndexBuffersAllocated(0)
{
	for (Int i = 0; i < MAX_FVF; i++)
		m_W3DVertexBuffers[i] = nullptr;
	for (Int i = 0; i < MAX_FVF; i++)
		for (Int j = 0; j < MAX_VB_SIZES; j++)
			m_W3DVertexBufferSlots[i][j] = nullptr;

	for (Int j = 0; j < MAX_IB_SIZES; j++)
		m_W3DIndexBufferSlots[j] = nullptr;
}

W3DBufferManager::~W3DBufferManager(void)
{
	freeAllSlots();
	freeAllBuffers();
}

void W3DBufferManager::freeAllSlots(void)
{
	// Walk each free-list bucket and count slots dropped. Unlike the
	// original we don't touch the per-VB `m_usedSlots` chain — by the time
	// freeAllSlots is called, release must already have unhooked live
	// slots. The ASSERTCRASH below guards that invariant.
	for (Int i = 0; i < MAX_FVF; i++)
	{
		for (Int j = 0; j < MAX_VB_SIZES; j++)
		{
			W3DVertexBufferSlot* vbSlot = m_W3DVertexBufferSlots[i][j];
			while (vbSlot)
			{
				W3DVertexBufferSlot* next = vbSlot->m_nextSameSize;
				vbSlot = next;
				m_numEmptySlotsAllocated--;
			}
			m_W3DVertexBufferSlots[i][j] = nullptr;
		}
	}

	for (Int j = 0; j < MAX_IB_SIZES; j++)
	{
		W3DIndexBufferSlot* ibSlot = m_W3DIndexBufferSlots[j];
		while (ibSlot)
		{
			W3DIndexBufferSlot* next = ibSlot->m_nextSameSize;
			ibSlot = next;
			m_numEmptyIndexSlotsAllocated--;
		}
		m_W3DIndexBufferSlots[j] = nullptr;
	}

	// Reset all live VB/IB bookkeeping so subsequent allocations refill
	// them from the start. The underlying GPU buffers stay intact.
	for (Int i = 0; i < MAX_FVF; i++)
	{
		for (W3DVertexBuffer* vb = m_W3DVertexBuffers[i]; vb; vb = vb->m_nextVB)
		{
			vb->m_usedSlots = nullptr;
			vb->m_startFreeIndex = 0;
			vb->m_renderTaskList = nullptr;
		}
	}
	for (W3DIndexBuffer* ib = m_W3DIndexBuffers; ib; ib = ib->m_nextIB)
	{
		ib->m_usedSlots = nullptr;
		ib->m_startFreeIndex = 0;
	}

	m_numEmptySlotsAllocated = 0;
	m_numEmptyIndexSlotsAllocated = 0;
}

void W3DBufferManager::freeAllBuffers(void)
{
	freeAllSlots();

#ifdef BUILD_WITH_D3D11
	for (Int i = 0; i < MAX_FVF; i++)
	{
		W3DVertexBuffer* vb = m_W3DVertexBuffers[i];
		while (vb)
		{
			if (vb->m_buffer)
			{
				vb->m_buffer->Release();
				vb->m_buffer = nullptr;
			}
			vb = vb->m_nextVB;
		}
	}

	W3DIndexBuffer* ib = m_W3DIndexBuffers;
	while (ib)
	{
		if (ib->m_buffer)
		{
			ib->m_buffer->Release();
			ib->m_buffer = nullptr;
		}
		ib = ib->m_nextIB;
	}
#endif

	for (Int i = 0; i < MAX_FVF; i++)
		m_W3DVertexBuffers[i] = nullptr;
	m_W3DIndexBuffers = nullptr;
	m_numEmptyVertexBuffersAllocated = 0;
	m_numEmptyIndexBuffersAllocated = 0;
}

void W3DBufferManager::ReleaseResources(void)
{
#ifdef BUILD_WITH_D3D11
	// Just drop the GPU-side buffers; wrappers + slot bookkeeping stay so
	// ReAcquireResources can rebuild without touching live shadows.
	for (Int i = 0; i < MAX_FVF; i++)
	{
		W3DVertexBuffer* vb = m_W3DVertexBuffers[i];
		while (vb)
		{
			if (vb->m_buffer)
			{
				vb->m_buffer->Release();
				vb->m_buffer = nullptr;
			}
			vb = vb->m_nextVB;
		}
	}

	W3DIndexBuffer* ib = m_W3DIndexBuffers;
	while (ib)
	{
		if (ib->m_buffer)
		{
			ib->m_buffer->Release();
			ib->m_buffer = nullptr;
		}
		ib = ib->m_nextIB;
	}
#endif
}

Bool W3DBufferManager::ReAcquireResources(void)
{
#ifdef BUILD_WITH_D3D11
	for (Int i = 0; i < MAX_FVF; i++)
	{
		for (W3DVertexBuffer* vb = m_W3DVertexBuffers[i]; vb; vb = vb->m_nextVB)
		{
			if (vb->m_buffer != nullptr)
				continue;
			if (!createDeviceVertexBuffer(vb))
				return false;
		}
	}

	for (W3DIndexBuffer* ib = m_W3DIndexBuffers; ib; ib = ib->m_nextIB)
	{
		if (ib->m_buffer != nullptr)
			continue;
		if (!createDeviceIndexBuffer(ib))
			return false;
	}
#endif
	return true;
}

#ifdef BUILD_WITH_D3D11
Bool W3DBufferManager::createDeviceVertexBuffer(W3DVertexBuffer* vb)
{
	ID3D11Device* dev = Render::Renderer::Instance().GetDevice().GetDevice();
	if (!dev)
		return false;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = (UINT)(vb->m_size * kFvfStrideTable[vb->m_format]);
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;

	HRESULT hr = dev->CreateBuffer(&desc, nullptr, &vb->m_buffer);
	if (FAILED(hr) || !vb->m_buffer)
	{
		DEBUG_CRASH(("W3DBufferManager: CreateBuffer (VB) failed hr=0x%08x size=%d", hr, desc.ByteWidth));
		return false;
	}
	return true;
}

Bool W3DBufferManager::createDeviceIndexBuffer(W3DIndexBuffer* ib)
{
	ID3D11Device* dev = Render::Renderer::Instance().GetDevice().GetDevice();
	if (!dev)
		return false;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = (UINT)(ib->m_size * sizeof(UINT16));
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;

	HRESULT hr = dev->CreateBuffer(&desc, nullptr, &ib->m_buffer);
	if (FAILED(hr) || !ib->m_buffer)
	{
		DEBUG_CRASH(("W3DBufferManager: CreateBuffer (IB) failed hr=0x%08x size=%d", hr, desc.ByteWidth));
		return false;
	}
	return true;
}
#endif

W3DBufferManager::W3DVertexBufferSlot* W3DBufferManager::getSlot(VBM_FVF_TYPES fvfType, Int size)
{
	// Round up to MIN_SLOT_SIZE so same-size reuse buckets tolerate minor
	// caller-size variations and reduce fragmentation.
	size = (size + (MIN_SLOT_SIZE - 1)) & (~(MIN_SLOT_SIZE - 1));
	Int sizeIndex = (size >> MIN_SLOT_SIZE_SHIFT) - 1;

	DEBUG_ASSERTCRASH(sizeIndex < MAX_VB_SIZES && size, ("Allocating too large vertex buffer slot"));
	// Hard bail in release — past-end reads on the size bucket array yield
	// garbage pointers that then dereference to an access violation.
	if (sizeIndex < 0 || sizeIndex >= MAX_VB_SIZES || size <= 0)
		return nullptr;

	// Fast path: hand back a previously-released slot of the exact size.
	W3DVertexBufferSlot* vbSlot = m_W3DVertexBufferSlots[fvfType][sizeIndex];
	if (vbSlot)
	{
		m_W3DVertexBufferSlots[fvfType][sizeIndex] = vbSlot->m_nextSameSize;
		if (vbSlot->m_nextSameSize)
			vbSlot->m_nextSameSize->m_prevSameSize = nullptr;
		return vbSlot;
	}

	// Otherwise carve a new one off an existing VB (or create one).
	return allocateSlotStorage(fvfType, size);
}

void W3DBufferManager::releaseSlot(W3DVertexBufferSlot* vbSlot)
{
	if (!vbSlot)
		return;

	Int sizeIndex = (vbSlot->m_size >> MIN_SLOT_SIZE_SHIFT) - 1;

	vbSlot->m_nextSameSize = m_W3DVertexBufferSlots[vbSlot->m_VB->m_format][sizeIndex];
	if (vbSlot->m_nextSameSize)
		vbSlot->m_nextSameSize->m_prevSameSize = vbSlot;
	vbSlot->m_prevSameSize = nullptr;
	m_W3DVertexBufferSlots[vbSlot->m_VB->m_format][sizeIndex] = vbSlot;
}

W3DBufferManager::W3DVertexBufferSlot* W3DBufferManager::allocateSlotStorage(VBM_FVF_TYPES fvfType, Int size)
{
	DEBUG_ASSERTCRASH(m_numEmptySlotsAllocated < MAX_NUMBER_SLOTS, ("No more VB Slots"));

	// Try each live VB of the requested format and reserve at the tail
	// if there's space.
	W3DVertexBuffer* pVB = m_W3DVertexBuffers[fvfType];
	while (pVB)
	{
		if ((pVB->m_size - pVB->m_startFreeIndex) >= size)
		{
			if (m_numEmptySlotsAllocated < MAX_NUMBER_SLOTS)
			{
				W3DVertexBufferSlot* vbSlot = &m_W3DVertexBufferEmptySlots[m_numEmptySlotsAllocated];
				vbSlot->m_size = size;
				vbSlot->m_start = pVB->m_startFreeIndex;
				vbSlot->m_VB = pVB;
				vbSlot->m_nextSameVB = pVB->m_usedSlots;
				vbSlot->m_prevSameVB = nullptr;
				if (pVB->m_usedSlots)
					pVB->m_usedSlots->m_prevSameVB = vbSlot;
				vbSlot->m_prevSameSize = vbSlot->m_nextSameSize = nullptr;
				pVB->m_usedSlots = vbSlot;
				pVB->m_startFreeIndex += size;
				m_numEmptySlotsAllocated++;
				return vbSlot;
			}
		}
		pVB = pVB->m_nextVB;
	}

	// No VB has room — stand up a new one and put this slot at index 0.
	DEBUG_ASSERTCRASH(m_numEmptyVertexBuffersAllocated < MAX_VERTEX_BUFFERS_CREATED, ("Reached Max VB"));
	if (m_numEmptyVertexBuffersAllocated >= MAX_VERTEX_BUFFERS_CREATED)
		return nullptr;

	W3DVertexBuffer* oldHead = m_W3DVertexBuffers[fvfType];
	pVB = &m_W3DEmptyVertexBuffers[m_numEmptyVertexBuffersAllocated];
	m_numEmptyVertexBuffersAllocated++;

	Int vbSize = __max(DEFAULT_VERTEX_BUFFER_SIZE, size);

	pVB->m_format = fvfType;
	pVB->m_size = vbSize;
	pVB->m_startFreeIndex = size;
	pVB->m_nextVB = oldHead;
	pVB->m_renderTaskList = nullptr;
	pVB->m_usedSlots = nullptr;
#ifdef BUILD_WITH_D3D11
	pVB->m_buffer = nullptr;
	if (!createDeviceVertexBuffer(pVB))
	{
		m_numEmptyVertexBuffersAllocated--;
		return nullptr;
	}
#endif
	m_W3DVertexBuffers[fvfType] = pVB;

	W3DVertexBufferSlot* vbSlot = &m_W3DVertexBufferEmptySlots[m_numEmptySlotsAllocated];
	m_numEmptySlotsAllocated++;
	vbSlot->m_size = size;
	vbSlot->m_start = 0;
	vbSlot->m_VB = pVB;
	vbSlot->m_prevSameVB = vbSlot->m_nextSameVB = nullptr;
	vbSlot->m_prevSameSize = vbSlot->m_nextSameSize = nullptr;
	pVB->m_usedSlots = vbSlot;
	return vbSlot;
}

// ---- Index buffer allocator — mirrors the VB path exactly, one format. ----

W3DBufferManager::W3DIndexBufferSlot* W3DBufferManager::getSlot(Int size)
{
	size = (size + (MIN_SLOT_SIZE - 1)) & (~(MIN_SLOT_SIZE - 1));
	Int sizeIndex = (size >> MIN_SLOT_SIZE_SHIFT) - 1;

	DEBUG_ASSERTCRASH(sizeIndex < MAX_IB_SIZES && size, ("Allocating too large index buffer slot"));
	// Hard bail in release — past-end reads on the size bucket array yield
	// garbage pointers that then dereference to an access violation.
	if (sizeIndex < 0 || sizeIndex >= MAX_IB_SIZES || size <= 0)
		return nullptr;

	W3DIndexBufferSlot* ibSlot = m_W3DIndexBufferSlots[sizeIndex];
	if (ibSlot)
	{
		m_W3DIndexBufferSlots[sizeIndex] = ibSlot->m_nextSameSize;
		if (ibSlot->m_nextSameSize)
			ibSlot->m_nextSameSize->m_prevSameSize = nullptr;
		return ibSlot;
	}

	return allocateSlotStorage(size);
}

void W3DBufferManager::releaseSlot(W3DIndexBufferSlot* ibSlot)
{
	if (!ibSlot)
		return;

	Int sizeIndex = (ibSlot->m_size >> MIN_SLOT_SIZE_SHIFT) - 1;

	ibSlot->m_nextSameSize = m_W3DIndexBufferSlots[sizeIndex];
	if (ibSlot->m_nextSameSize)
		ibSlot->m_nextSameSize->m_prevSameSize = ibSlot;
	ibSlot->m_prevSameSize = nullptr;
	m_W3DIndexBufferSlots[sizeIndex] = ibSlot;
}

W3DBufferManager::W3DIndexBufferSlot* W3DBufferManager::allocateSlotStorage(Int size)
{
	DEBUG_ASSERTCRASH(m_numEmptyIndexSlotsAllocated < MAX_NUMBER_SLOTS, ("No more IB Slots"));

	W3DIndexBuffer* pIB = m_W3DIndexBuffers;
	while (pIB)
	{
		if ((pIB->m_size - pIB->m_startFreeIndex) >= size)
		{
			if (m_numEmptyIndexSlotsAllocated < MAX_NUMBER_SLOTS)
			{
				W3DIndexBufferSlot* ibSlot = &m_W3DIndexBufferEmptySlots[m_numEmptyIndexSlotsAllocated];
				ibSlot->m_size = size;
				ibSlot->m_start = pIB->m_startFreeIndex;
				ibSlot->m_IB = pIB;
				ibSlot->m_nextSameIB = pIB->m_usedSlots;
				ibSlot->m_prevSameIB = nullptr;
				if (pIB->m_usedSlots)
					pIB->m_usedSlots->m_prevSameIB = ibSlot;
				ibSlot->m_prevSameSize = ibSlot->m_nextSameSize = nullptr;
				pIB->m_usedSlots = ibSlot;
				pIB->m_startFreeIndex += size;
				m_numEmptyIndexSlotsAllocated++;
				return ibSlot;
			}
		}
		pIB = pIB->m_nextIB;
	}

	DEBUG_ASSERTCRASH(m_numEmptyIndexBuffersAllocated < MAX_INDEX_BUFFERS_CREATED, ("Reached Max IB"));
	if (m_numEmptyIndexBuffersAllocated >= MAX_INDEX_BUFFERS_CREATED)
		return nullptr;

	W3DIndexBuffer* oldHead = m_W3DIndexBuffers;
	pIB = &m_W3DEmptyIndexBuffers[m_numEmptyIndexBuffersAllocated];
	m_numEmptyIndexBuffersAllocated++;

	Int ibSize = __max(DEFAULT_INDEX_BUFFER_SIZE, size);

	pIB->m_size = ibSize;
	pIB->m_startFreeIndex = size;
	pIB->m_nextIB = oldHead;
	pIB->m_usedSlots = nullptr;
#ifdef BUILD_WITH_D3D11
	pIB->m_buffer = nullptr;
	if (!createDeviceIndexBuffer(pIB))
	{
		m_numEmptyIndexBuffersAllocated--;
		return nullptr;
	}
#endif
	m_W3DIndexBuffers = pIB;

	W3DIndexBufferSlot* ibSlot = &m_W3DIndexBufferEmptySlots[m_numEmptyIndexSlotsAllocated];
	m_numEmptyIndexSlotsAllocated++;
	ibSlot->m_size = size;
	ibSlot->m_start = 0;
	ibSlot->m_IB = pIB;
	ibSlot->m_prevSameIB = ibSlot->m_nextSameIB = nullptr;
	ibSlot->m_prevSameSize = ibSlot->m_nextSameSize = nullptr;
	pIB->m_usedSlots = ibSlot;
	return ibSlot;
}

#ifdef BUILD_WITH_D3D11
// Map helpers. DISCARD is used by the first slot write in a frame (fresh
// buffer contents); NO_OVERWRITE is used for every subsequent slot in the
// same buffer so the GPU's in-flight reads of prior slots aren't stomped.
void* W3DBufferManager::mapSlot(W3DVertexBufferSlot* slot, Bool discardFullBuffer)
{
	if (!slot || !slot->m_VB || !slot->m_VB->m_buffer)
		return nullptr;

	ID3D11DeviceContext* ctx = Render::Renderer::Instance().GetDevice().GetContext();
	if (!ctx)
		return nullptr;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	D3D11_MAP mapType = discardFullBuffer ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
	HRESULT hr = ctx->Map(slot->m_VB->m_buffer, 0, mapType, 0, &mapped);
	if (FAILED(hr) || !mapped.pData)
		return nullptr;

	return (Byte*)mapped.pData + slot->m_start * kFvfStrideTable[slot->m_VB->m_format];
}

void* W3DBufferManager::mapSlot(W3DIndexBufferSlot* slot, Bool discardFullBuffer)
{
	if (!slot || !slot->m_IB || !slot->m_IB->m_buffer)
		return nullptr;

	ID3D11DeviceContext* ctx = Render::Renderer::Instance().GetDevice().GetContext();
	if (!ctx)
		return nullptr;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	D3D11_MAP mapType = discardFullBuffer ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
	HRESULT hr = ctx->Map(slot->m_IB->m_buffer, 0, mapType, 0, &mapped);
	if (FAILED(hr) || !mapped.pData)
		return nullptr;

	return (Byte*)mapped.pData + slot->m_start * sizeof(UINT16);
}

void W3DBufferManager::unmapSlot(W3DVertexBufferSlot* slot)
{
	if (!slot || !slot->m_VB || !slot->m_VB->m_buffer)
		return;

	ID3D11DeviceContext* ctx = Render::Renderer::Instance().GetDevice().GetContext();
	if (!ctx)
		return;

	ctx->Unmap(slot->m_VB->m_buffer, 0);
}

void W3DBufferManager::unmapSlot(W3DIndexBufferSlot* slot)
{
	if (!slot || !slot->m_IB || !slot->m_IB->m_buffer)
		return;

	ID3D11DeviceContext* ctx = Render::Renderer::Instance().GetDevice().GetContext();
	if (!ctx)
		return;

	ctx->Unmap(slot->m_IB->m_buffer, 0);
}
#endif
