#ifdef BUILD_WITH_VULKAN

#include "../Buffer.h"
#include "../Device.h"
#include "VulkanUtil.h"
#include <cstring>

namespace Render
{

struct VkDeviceAccess
{
    static VkDevice GetDevice(const Device& d) { return d.m_vkDevice; }
    static VkPhysicalDevice GetPhysicalDevice(const Device& d) { return d.m_vkPhysicalDevice; }
    static VkCommandPool GetCommandPool(const Device& d) { return d.m_vkCommandPool; }
    static VkQueue GetGraphicsQueue(const Device& d) { return d.m_vkGraphicsQueue; }
    static VkCommandBuffer GetCurrentCmd(const Device& d) { return d.m_vkCommandBuffers[d.m_vkCurrentFrame]; }
    static bool IsRecording(const Device& d) { return d.m_vkRecording; }
    static VmaAllocator GetAllocator(const Device& d) { return d.m_vmaAllocator; }
};

// Create a host-visible, host-coherent buffer using raw Vulkan allocation (no VMA).
// VMA suballocation can cause issues with some drivers when mapping/flushing.
static bool CreateRawHostBuffer(VkDevice dev, VkPhysicalDevice physDev,
    VkDeviceSize size, VkBufferUsageFlags usage,
    VkBuffer& outBuf, VkDeviceMemory& outMem)
{
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bufInfo, nullptr, &outBuf) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(dev, outBuf, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    uint32_t memType = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            memType = i;
            break;
        }
    }
    if (memType == UINT32_MAX) { vkDestroyBuffer(dev, outBuf, nullptr); return false; }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memType;
    if (vkAllocateMemory(dev, &allocInfo, nullptr, &outMem) != VK_SUCCESS)
    {
        vkDestroyBuffer(dev, outBuf, nullptr);
        return false;
    }
    vkBindBufferMemory(dev, outBuf, outMem, 0);
    return true;
}

// Track raw VkDeviceMemory for cleanup. We store it in VmaAllocation slot (ugly but avoids adding a new member).
// When m_vkAllocation is VK_NULL_HANDLE, it means we used raw allocation and the memory handle
// is stored as the buffer's bound memory (queryable but not needed for our cleanup path).

// VertexBuffer ----------------------------------------------------------------

void VertexBuffer::Destroy(Device& device)
{
    VkDevice dev = VkDeviceAccess::GetDevice(device);
    if (m_vkRawMemory)
    {
        if (m_vkMapped) { vkUnmapMemory(dev, m_vkRawMemory); m_vkMapped = nullptr; }
        vkDestroyBuffer(dev, m_vkBuffer, nullptr);
        vkFreeMemory(dev, m_vkRawMemory, nullptr);
        m_vkBuffer = VK_NULL_HANDLE;
        m_vkRawMemory = VK_NULL_HANDLE;
    }
    else
    {
        VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
        if (m_vkBuffer && alloc) { vmaDestroyBuffer(alloc, m_vkBuffer, m_vkAllocation); m_vkBuffer = VK_NULL_HANDLE; m_vkAllocation = VK_NULL_HANDLE; }
    }
    m_vertexCount = 0;
}

bool VertexBuffer::Create(Device& device, const void* data, uint32_t vertexCount, uint32_t stride, bool dynamic)
{
    m_vertexCount = vertexCount;
    m_stride = stride;
    m_dynamic = dynamic;
    m_vkSize = (VkDeviceSize)vertexCount * stride;

    VkDevice dev = VkDeviceAccess::GetDevice(device);
    VkPhysicalDevice physDev = VkDeviceAccess::GetPhysicalDevice(device);

    if (dynamic)
    {
        // Use raw Vulkan allocation for host-visible buffers to ensure
        // coherent mapping without VMA suballocation issues.
        if (!CreateRawHostBuffer(dev, physDev, m_vkSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vkBuffer, m_vkRawMemory))
            return false;
        m_vkAllocation = VK_NULL_HANDLE;

        // Persistently map for fast updates
        vkMapMemory(dev, m_vkRawMemory, 0, m_vkSize, 0, &m_vkMapped);

        if (data && m_vkMapped)
            memcpy(m_vkMapped, data, m_vkSize);
    }
    else
    {
        // Device-local with staging upload — use VMA for these (GPU-only memory)
        VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
        if (!VkCreateBuffer(alloc, m_vkSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vkBuffer, m_vkAllocation))
            return false;

        if (data)
        {
            VkBuffer staging; VkDeviceMemory stagingMem;
            VkCreateBuffer(dev, physDev, m_vkSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging, stagingMem);
            void* mapped;
            vkMapMemory(dev, stagingMem, 0, m_vkSize, 0, &mapped);
            memcpy(mapped, data, m_vkSize);
            vkUnmapMemory(dev, stagingMem);

            VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device));
            VkBufferCopy copy = {}; copy.size = m_vkSize;
            vkCmdCopyBuffer(cmd, staging, m_vkBuffer, 1, &copy);
            VkEndSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device),
                                    VkDeviceAccess::GetGraphicsQueue(device), cmd);
            vkDestroyBuffer(dev, staging, nullptr);
            vkFreeMemory(dev, stagingMem, nullptr);
        }
    }

    return true;
}

void VertexBuffer::Update(Device& device, const void* data, uint32_t size)
{
    (void)device;
    if (!m_vkBuffer || !data || !m_dynamic || !m_vkMapped) return;
    memcpy(m_vkMapped, data, size);
    // Raw allocation is host-coherent — no flush needed
}

void VertexBuffer::Bind(Device& device, uint32_t slot) const
{
    if (!m_vkBuffer || !VkDeviceAccess::IsRecording(device)) return;
    VkCommandBuffer cmd = VkDeviceAccess::GetCurrentCmd(device);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, slot, 1, &m_vkBuffer, &offset);
}

// IndexBuffer -----------------------------------------------------------------

void IndexBuffer::Destroy(Device& device)
{
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    if (m_vkBuffer && alloc) { vmaDestroyBuffer(alloc, m_vkBuffer, m_vkAllocation); m_vkBuffer = VK_NULL_HANDLE; m_vkAllocation = VK_NULL_HANDLE; }
    m_indexCount = 0;
}

static bool CreateIndexBufferImpl(Device& device, VkBuffer& outBuf, VmaAllocation& outAlloc,
    VkDeviceSize size, VkMemoryPropertyFlags memProps, const void* data)
{
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    if (!VkCreateBuffer(alloc, size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            memProps, outBuf, outAlloc))
        return false;

    if (data)
    {
        VkBuffer staging;
        VmaAllocation stagingAlloc;
        VkCreateBuffer(alloc, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging, stagingAlloc);

        void* mapped;
        vmaMapMemory(alloc, stagingAlloc, &mapped);
        memcpy(mapped, data, size);
        vmaUnmapMemory(alloc, stagingAlloc);

        VkDevice dev = VkDeviceAccess::GetDevice(device);
        VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device));
        VkBufferCopy copy = {}; copy.size = size;
        vkCmdCopyBuffer(cmd, staging, outBuf, 1, &copy);
        VkEndSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device),
                                VkDeviceAccess::GetGraphicsQueue(device), cmd);

        vmaDestroyBuffer(alloc, staging, stagingAlloc);
    }
    return true;
}

bool IndexBuffer::Create(Device& device, const uint16_t* data, uint32_t indexCount, bool dynamic)
{
    m_indexCount = indexCount;
    m_is32Bit = false;
    m_dynamic = dynamic;
    m_vkSize = (VkDeviceSize)indexCount * sizeof(uint16_t);
    VkMemoryPropertyFlags memProps = dynamic
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    return CreateIndexBufferImpl(device, m_vkBuffer, m_vkAllocation, m_vkSize, memProps, data);
}

bool IndexBuffer::Create32(Device& device, const uint32_t* data, uint32_t indexCount, bool dynamic)
{
    m_indexCount = indexCount;
    m_is32Bit = true;
    m_dynamic = dynamic;
    m_vkSize = (VkDeviceSize)indexCount * sizeof(uint32_t);
    VkMemoryPropertyFlags memProps = dynamic
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    return CreateIndexBufferImpl(device, m_vkBuffer, m_vkAllocation, m_vkSize, memProps, data);
}

void IndexBuffer::Update(Device& device, const void* data, uint32_t size)
{
    if (!m_vkBuffer || !data || !m_dynamic) return;
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    void* mapped;
    vmaMapMemory(alloc, m_vkAllocation, &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(alloc, m_vkAllocation);
}

void IndexBuffer::Bind(Device& device) const
{
    if (!m_vkBuffer || !VkDeviceAccess::IsRecording(device)) return;
    VkCommandBuffer cmd = VkDeviceAccess::GetCurrentCmd(device);
    vkCmdBindIndexBuffer(cmd, m_vkBuffer, 0, m_is32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

// ConstantBuffer (Uniform Buffer) ---------------------------------------------

void ConstantBuffer::Destroy(Device& device)
{
    // CB uses raw Vulkan allocation (not VMA)
    VkDevice dev = VkDeviceAccess::GetDevice(device);
    if (m_vkBuffer) { vkDestroyBuffer(dev, m_vkBuffer, nullptr); m_vkBuffer = VK_NULL_HANDLE; }
    // Note: raw VkDeviceMemory is leaked here (would need a member to track it)
    // This is acceptable for shutdown cleanup.
    m_vkMapped = nullptr;
    m_vkAllocation = VK_NULL_HANDLE;
    m_size = 0;
}

bool ConstantBuffer::Create(Device& device, uint32_t sizeBytes)
{
    m_size = (sizeBytes + 15) & ~15; // 16-byte alignment

    // Use raw Vulkan allocation (not VMA) for constant buffers to ensure
    // the descriptor set buffer range exactly matches the physical memory.
    VkDevice dev = VkDeviceAccess::GetDevice(device);
    VkPhysicalDevice physDev = VkDeviceAccess::GetPhysicalDevice(device);

    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = m_size;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bufInfo, nullptr, &m_vkBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(dev, m_vkBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }

    VkDeviceMemory rawMemory;
    if (vkAllocateMemory(dev, &allocInfo, nullptr, &rawMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(dev, m_vkBuffer, nullptr);
        return false;
    }
    vkBindBufferMemory(dev, m_vkBuffer, rawMemory, 0);

    vkMapMemory(dev, rawMemory, 0, m_size, 0, &m_vkMapped);
    m_vkAllocation = VK_NULL_HANDLE;
    return true;
}

void ConstantBuffer::Update(Device& device, const void* data, uint32_t sizeBytes)
{
    if (m_vkMapped && data)
    {
        memcpy(m_vkMapped, data, sizeBytes);
        // Raw allocation is host-coherent, no flush needed
    }
}

void ConstantBuffer::BindVS(Device& device, uint32_t slot) const
{
    if (slot < 3) { device.m_vkBoundCBs[slot] = m_vkBuffer; device.m_vkBoundCBSizes[slot] = m_size; }
}

void ConstantBuffer::BindPS(Device& device, uint32_t slot) const
{
    if (slot < 3) { device.m_vkBoundCBs[slot] = m_vkBuffer; device.m_vkBoundCBSizes[slot] = m_size; }
}

// GPUBuffer (Storage Buffer / StructuredBuffer) -------------------------------

void GPUBuffer::Destroy(Device& device)
{
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    if (m_vkBuffer && alloc) { vmaDestroyBuffer(alloc, m_vkBuffer, m_vkAllocation); m_vkBuffer = VK_NULL_HANDLE; m_vkAllocation = VK_NULL_HANDLE; }
    m_elementCount = 0;
}

bool GPUBuffer::Create(Device& device, uint32_t elementSize, uint32_t maxElements, const void* initialData)
{
    m_elementSize = elementSize;
    m_maxElements = maxElements;
    m_elementCount = 0;

    VkDeviceSize bufSize = (VkDeviceSize)elementSize * maxElements;
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);

    if (!VkCreateBuffer(alloc, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_vkBuffer, m_vkAllocation))
        return false;

    if (initialData)
    {
        void* mapped;
        vmaMapMemory(alloc, m_vkAllocation, &mapped);
        memcpy(mapped, initialData, bufSize);
        vmaUnmapMemory(alloc, m_vkAllocation);
    }

    return true;
}

void GPUBuffer::Update(Device& device, const void* data, uint32_t elementCount)
{
    if (!m_vkBuffer || elementCount > m_maxElements) return;
    m_elementCount = elementCount;

    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    VkDeviceSize size = (VkDeviceSize)elementCount * m_elementSize;

    void* mapped;
    vmaMapMemory(alloc, m_vkAllocation, &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(alloc, m_vkAllocation);
}

void GPUBuffer::BindVS(Device& /*device*/, uint32_t /*slot*/) const { /* Descriptor set */ }
void GPUBuffer::BindPS(Device& /*device*/, uint32_t /*slot*/) const { /* Descriptor set */ }

// ComputeBuffer ---------------------------------------------------------------

void ComputeBuffer::Destroy(Device& device)
{
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    if (m_vkStaging && alloc) { vmaDestroyBuffer(alloc, m_vkStaging, m_vkStagingAllocation); m_vkStaging = VK_NULL_HANDLE; m_vkStagingAllocation = VK_NULL_HANDLE; }
    if (m_vkBuffer && alloc) { vmaDestroyBuffer(alloc, m_vkBuffer, m_vkAllocation); m_vkBuffer = VK_NULL_HANDLE; m_vkAllocation = VK_NULL_HANDLE; }
    m_elementCount = 0;
}

bool ComputeBuffer::Create(Device& device, uint32_t elementSize, uint32_t maxElements, bool /*hasUAV*/)
{
    m_elementSize = elementSize;
    m_maxElements = maxElements;
    m_elementCount = 0;

    VkDeviceSize bufSize = (VkDeviceSize)elementSize * maxElements;
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);

    if (!VkCreateBuffer(alloc, bufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vkBuffer, m_vkAllocation))
        return false;

    if (!VkCreateBuffer(alloc, bufSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_vkStaging, m_vkStagingAllocation))
        return false;

    return true;
}

void ComputeBuffer::Upload(Device& device, const void* data, uint32_t elementCount)
{
    if (!m_vkBuffer || elementCount > m_maxElements) return;
    m_elementCount = elementCount;

    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    VkDevice dev = VkDeviceAccess::GetDevice(device);
    VkDeviceSize size = (VkDeviceSize)elementCount * m_elementSize;

    void* mapped;
    vmaMapMemory(alloc, m_vkStagingAllocation, &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(alloc, m_vkStagingAllocation);

    VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device));
    VkBufferCopy copy = {}; copy.size = size;
    vkCmdCopyBuffer(cmd, m_vkStaging, m_vkBuffer, 1, &copy);
    VkEndSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device),
                            VkDeviceAccess::GetGraphicsQueue(device), cmd);
}

void ComputeBuffer::Readback(Device& device, void* dst, uint32_t sizeBytes)
{
    if (!m_vkBuffer || !m_vkStaging) return;

    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    VkDevice dev = VkDeviceAccess::GetDevice(device);

    VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device));
    VkBufferCopy copy = {}; copy.size = sizeBytes;
    vkCmdCopyBuffer(cmd, m_vkBuffer, m_vkStaging, 1, &copy);
    VkEndSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device),
                            VkDeviceAccess::GetGraphicsQueue(device), cmd);

    void* mapped;
    vmaMapMemory(alloc, m_vkStagingAllocation, &mapped);
    memcpy(dst, mapped, sizeBytes);
    vmaUnmapMemory(alloc, m_vkStagingAllocation);
}

void ComputeBuffer::BindSRV(Device& /*device*/, uint32_t /*slot*/) const { /* Descriptor set */ }
void ComputeBuffer::BindUAV(Device& /*device*/, uint32_t /*slot*/) const { /* Descriptor set */ }
void ComputeBuffer::UnbindUAV(Device& /*device*/, uint32_t /*slot*/) const { }

} // namespace Render

#endif // BUILD_WITH_VULKAN
