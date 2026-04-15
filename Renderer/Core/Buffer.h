#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <vector>

#ifdef BUILD_WITH_D3D11
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#ifdef BUILD_WITH_VULKAN
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#endif

namespace Render
{

class Device;

class VertexBuffer
{
public:
    void Destroy(Device& device);
    bool Create(Device& device, const void* data, uint32_t vertexCount, uint32_t stride, bool dynamic = false);
    void Update(Device& device, const void* data, uint32_t size);
    void Bind(Device& device, uint32_t slot = 0) const;

    uint32_t GetVertexCount() const { return m_vertexCount; }
    uint32_t GetStride() const { return m_stride; }
#ifdef BUILD_WITH_VULKAN
    VkBuffer GetVkBuffer() const { return m_vkBuffer; }
#endif

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11Buffer> m_buffer;
#endif
#ifdef BUILD_WITH_VULKAN
    VkBuffer m_vkBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vkAllocation = VK_NULL_HANDLE;
    VkDeviceSize m_vkSize = 0;
    void* m_vkMapped = nullptr;         // Persistently mapped for dynamic buffers
    VkDeviceMemory m_vkRawMemory = VK_NULL_HANDLE; // Raw allocation (when m_vkAllocation is null)
#endif
    uint32_t m_vertexCount = 0;
    uint32_t m_stride = 0;
    bool m_dynamic = false;
};

class IndexBuffer
{
public:
    void Destroy(Device& device);
    bool Create(Device& device, const uint16_t* data, uint32_t indexCount, bool dynamic = false);
    bool Create32(Device& device, const uint32_t* data, uint32_t indexCount, bool dynamic = false);
    void Update(Device& device, const void* data, uint32_t size);
    void Bind(Device& device) const;

    uint32_t GetIndexCount() const { return m_indexCount; }
    bool Is32Bit() const { return m_is32Bit; }

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11Buffer> m_buffer;
#endif
#ifdef BUILD_WITH_VULKAN
    VkBuffer m_vkBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vkAllocation = VK_NULL_HANDLE;
    VkDeviceSize m_vkSize = 0;
    void* m_vkMapped = nullptr;
    VkDeviceMemory m_vkRawMemory = VK_NULL_HANDLE;
#endif
    uint32_t m_indexCount = 0;
    bool m_is32Bit = false;
    bool m_dynamic = false;
};

class ConstantBuffer
{
public:
    void Destroy(Device& device);
    bool Create(Device& device, uint32_t sizeBytes);
    void Update(Device& device, const void* data, uint32_t sizeBytes);
    void BindVS(Device& device, uint32_t slot) const;
    void BindPS(Device& device, uint32_t slot) const;

#ifdef BUILD_WITH_VULKAN
    VkBuffer GetVkBuffer() const { return m_vkBuffer; }
    uint32_t GetSize() const { return m_size; }
#endif

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11Buffer> m_buffer;
#endif
#ifdef BUILD_WITH_VULKAN
    VkBuffer m_vkBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vkAllocation = VK_NULL_HANDLE;
    void* m_vkMapped = nullptr; // persistently mapped for dynamic updates
#endif
    uint32_t m_size = 0;
};

// GPU-visible structured buffer for instancing (StructuredBuffer<T> in HLSL)
class GPUBuffer
{
public:
    void Destroy(Device& device);
    bool Create(Device& device, uint32_t elementSize, uint32_t maxElements, const void* initialData = nullptr);
    void Update(Device& device, const void* data, uint32_t elementCount);
    void BindVS(Device& device, uint32_t slot) const;
    void BindPS(Device& device, uint32_t slot) const;

    uint32_t GetElementCount() const { return m_elementCount; }

#ifdef BUILD_WITH_D3D11
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
#endif

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11Buffer> m_buffer;
    ComPtr<ID3D11ShaderResourceView> m_srv;
#endif
#ifdef BUILD_WITH_VULKAN
    VkBuffer m_vkBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vkAllocation = VK_NULL_HANDLE;
#endif
    uint32_t m_elementSize = 0;
    uint32_t m_maxElements = 0;
    uint32_t m_elementCount = 0;
};

// GPU compute buffer with UAV support for read/write from compute shaders
class ComputeBuffer
{
public:
    void Destroy(Device& device);
    bool Create(Device& device, uint32_t elementSize, uint32_t maxElements, bool hasUAV = true);
    void Upload(Device& device, const void* data, uint32_t elementCount);
    void Readback(Device& device, void* dst, uint32_t sizeBytes);

    void BindSRV(Device& device, uint32_t slot) const;
    void BindUAV(Device& device, uint32_t slot) const;
    void UnbindUAV(Device& device, uint32_t slot) const;

    uint32_t GetElementCount() const { return m_elementCount; }

#ifdef BUILD_WITH_D3D11
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    ID3D11UnorderedAccessView* GetUAV() const { return m_uav.Get(); }
#endif
#ifdef BUILD_WITH_VULKAN
    VkBuffer GetVkBuffer() const { return m_vkBuffer; }
#endif

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11Buffer> m_buffer;
    ComPtr<ID3D11Buffer> m_staging;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11UnorderedAccessView> m_uav;
#endif
#ifdef BUILD_WITH_VULKAN
    VkBuffer m_vkBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vkAllocation = VK_NULL_HANDLE;
    VkBuffer m_vkStaging = VK_NULL_HANDLE;
    VmaAllocation m_vkStagingAllocation = VK_NULL_HANDLE;
#endif
    uint32_t m_elementSize = 0;
    uint32_t m_maxElements = 0;
    uint32_t m_elementCount = 0;
};

} // namespace Render
