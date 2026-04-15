#pragma once

#include "RHITypes.h"
#include <cstdint>

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

class Texture
{
public:
    void Destroy(Device& device);

    bool CreateFromRGBA(Device& device, const void* pixels, uint32_t width, uint32_t height, bool generateMips = true);
    bool CreateFromPixels(Device& device, const void* pixels, uint32_t width, uint32_t height, PixelFormat format, uint32_t bytesPerPixel);
    bool CreateDynamic(Device& device, uint32_t width, uint32_t height, PixelFormat format = PixelFormat::RGBA8_UNORM);
    bool UpdateFromRGBA(Device& device, const void* pixels, uint32_t width, uint32_t height);
    bool CreateFromDDS(Device& device, const void* data, size_t size);
    bool CreateRenderTarget(Device& device, uint32_t width, uint32_t height, PixelFormat format = PixelFormat::RGBA8_UNORM);
    bool CreateDepthTarget(Device& device, uint32_t width, uint32_t height);

    void BindPS(Device& device, uint32_t slot) const;
    void BindVS(Device& device, uint32_t slot) const;

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    bool HasAlpha() const { return m_hasAlpha; }
    bool IsValid() const;  // Returns true if the texture has been created

#ifdef BUILD_WITH_D3D11
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    ID3D11RenderTargetView* GetRTV() const { return m_rtv.Get(); }
    ID3D11DepthStencilView* GetDSV() const { return m_dsv.Get(); }
#endif

    // Vulkan backend internals need access
    friend struct VkDeviceAccess;
    friend class Device;
    friend class VulkanPipelineManager;

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11DepthStencilView> m_dsv;
#endif
#ifdef BUILD_WITH_VULKAN
    VkImage m_vkImage = VK_NULL_HANDLE;
    VmaAllocation m_vkAllocation = VK_NULL_HANDLE;
    VkImageView m_vkImageView = VK_NULL_HANDLE;
    VkSampler m_vkSampler = VK_NULL_HANDLE;
    VkFormat m_vkFormat = VK_FORMAT_UNDEFINED;
    VkImageLayout m_vkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool m_vkOwnsImage = true; // false for swapchain images

    // Off-screen render target support — lazily created when used as RT
    VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_vkFramebuffer = VK_NULL_HANDLE;
    bool m_vkIsDepthTarget = false;
#endif
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_hasAlpha = true;
};

} // namespace Render
