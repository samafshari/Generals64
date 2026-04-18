// D3D11 wins when both backends are enabled — the Texture::* member bodies
// in Renderer/Core/Texture.cpp are authoritative in that case. Pure-Vulkan
// builds (USE_D3D11=OFF) activate this TU.
#if defined(BUILD_WITH_VULKAN) && !defined(BUILD_WITH_D3D11)

#include "../Texture.h"
#include "../Device.h"
#include "VulkanUtil.h"
#include <cstring>
#include <cstdlib>

namespace Render
{

// Access Device internals via friend struct
struct VkDeviceAccess
{
    static VkDevice GetDevice(const Device& d) { return d.m_vkDevice; }
    static VkPhysicalDevice GetPhysicalDevice(const Device& d) { return d.m_vkPhysicalDevice; }
    static VkCommandPool GetCommandPool(const Device& d) { return d.m_vkCommandPool; }
    static VkQueue GetGraphicsQueue(const Device& d) { return d.m_vkGraphicsQueue; }
    static VmaAllocator GetAllocator(const Device& d) { return d.m_vmaAllocator; }
};

static VkDevice GetDev(Device& d) { return VkDeviceAccess::GetDevice(d); }
static VkPhysicalDevice GetPhysDev(Device& d) { return VkDeviceAccess::GetPhysicalDevice(d); }
static VkCommandPool GetCmdPool(Device& d) { return VkDeviceAccess::GetCommandPool(d); }
static VkQueue GetQueue(Device& d) { return VkDeviceAccess::GetGraphicsQueue(d); }
static VmaAllocator GetAlloc(Device& d) { return VkDeviceAccess::GetAllocator(d); }

// --- Destroy ---

void Texture::Destroy(Device& device)
{
    VkDevice dev = GetDev(device);
    VmaAllocator alloc = GetAlloc(device);
    if (dev == VK_NULL_HANDLE) return;

    if (m_vkFramebuffer) { vkDestroyFramebuffer(dev, m_vkFramebuffer, nullptr); m_vkFramebuffer = VK_NULL_HANDLE; }
    if (m_vkRenderPass) { vkDestroyRenderPass(dev, m_vkRenderPass, nullptr); m_vkRenderPass = VK_NULL_HANDLE; }
    if (m_vkSampler) { vkDestroySampler(dev, m_vkSampler, nullptr); m_vkSampler = VK_NULL_HANDLE; }
    if (m_vkImageView) { vkDestroyImageView(dev, m_vkImageView, nullptr); m_vkImageView = VK_NULL_HANDLE; }
    if (m_vkOwnsImage && m_vkImage && alloc) { vmaDestroyImage(alloc, m_vkImage, m_vkAllocation); m_vkImage = VK_NULL_HANDLE; m_vkAllocation = VK_NULL_HANDLE; }
    m_vkLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_width = 0;
    m_height = 0;
}

// --- CreateFromRGBA ---

bool Texture::CreateFromRGBA(Device& device, const void* pixels, uint32_t width, uint32_t height, bool generateMips)
{
    m_width = width;
    m_height = height;
    m_vkFormat = VK_FORMAT_R8G8B8A8_UNORM;

    uint32_t mipLevels = generateMips ? (uint32_t)floor(log2(std::max(width, height))) + 1 : 1;

    VkDevice dev = GetDev(device);

    VkDeviceSize imageSize = (VkDeviceSize)width * height * 4;

    // Create staging buffer
    VmaAllocator alloc = GetAlloc(device);
    VkBuffer staging;
    VmaAllocation stagingAlloc;
    VkCreateBuffer(alloc, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging, stagingAlloc);

    void* mapped;
    vmaMapMemory(alloc, stagingAlloc, &mapped);
    memcpy(mapped, pixels, imageSize);
    vmaUnmapMemory(alloc, stagingAlloc);

    // Create image
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (generateMips)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // needed for vkCmdBlitImage mip generation

    if (!VkCreateImage(GetAlloc(device), width, height, m_vkFormat,
        VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vkImage, m_vkAllocation, mipLevels))
    {
        vmaDestroyBuffer(alloc, staging, stagingAlloc);
        return false;
    }

    // Transition, copy, transition
    VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, GetCmdPool(device));

    VkTransitionImageLayout(cmd, m_vkImage, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
    VkCopyBufferToImage(cmd, staging, m_vkImage, width, height);

    if (generateMips && mipLevels > 1)
    {
        // Generate mipmaps using vkCmdBlitImage
        int32_t mipW = (int32_t)width, mipH = (int32_t)height;
        for (uint32_t i = 1; i < mipLevels; i++)
        {
            // Transition level i-1 to TRANSFER_SRC
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = m_vkImage;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkImageBlit blit = {};
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
            blit.srcOffsets[1] = { mipW, mipH, 1 };
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
            blit.dstOffsets[1] = { mipW > 1 ? mipW / 2 : 1, mipH > 1 ? mipH / 2 : 1, 1 };

            vkCmdBlitImage(cmd, m_vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            // Transition level i-1 to SHADER_READ_ONLY
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mipW > 1) mipW /= 2;
            if (mipH > 1) mipH /= 2;
        }

        // Transition last mip to SHADER_READ_ONLY
        VkImageMemoryBarrier lastBarrier = {};
        lastBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        lastBarrier.image = m_vkImage;
        lastBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lastBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lastBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 1 };
        lastBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        lastBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lastBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        lastBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &lastBarrier);
    }
    else
    {
        VkTransitionImageLayout(cmd, m_vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
    }

    VkEndSingleTimeCommands(dev, GetCmdPool(device), GetQueue(device), cmd);
    m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Cleanup staging
    vmaDestroyBuffer(alloc, staging, stagingAlloc);

    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_vkFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_vkImageView) != VK_SUCCESS)
        return false;

    // Create sampler
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = (float)mipLevels;
    samplerInfo.maxAnisotropy = 1.0f;
    return vkCreateSampler(dev, &samplerInfo, nullptr, &m_vkSampler) == VK_SUCCESS;
}

// --- CreateFromPixels (generic format) ---

bool Texture::CreateFromPixels(Device& device, const void* pixels, uint32_t width, uint32_t height, PixelFormat format, uint32_t bytesPerPixel)
{
    m_width = width;
    m_height = height;
    m_vkFormat = ToVkFormat((int)format);

    VkDevice dev = GetDev(device);
    VmaAllocator alloc = GetAlloc(device);
    VkDeviceSize imageSize = (VkDeviceSize)width * height * bytesPerPixel;

    VkBuffer staging;
    VmaAllocation stagingAlloc;
    VkCreateBuffer(alloc, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging, stagingAlloc);

    void* mapped;
    vmaMapMemory(alloc, stagingAlloc, &mapped);
    memcpy(mapped, pixels, imageSize);
    vmaUnmapMemory(alloc, stagingAlloc);

    if (!VkCreateImage(GetAlloc(device), width, height, m_vkFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vkImage, m_vkAllocation))
    {
        vmaDestroyBuffer(alloc, staging, stagingAlloc);
        return false;
    }

    VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, GetCmdPool(device));
    VkTransitionImageLayout(cmd, m_vkImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCopyBufferToImage(cmd, staging, m_vkImage, width, height);
    VkTransitionImageLayout(cmd, m_vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkEndSingleTimeCommands(dev, GetCmdPool(device), GetQueue(device), cmd);
    m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vmaDestroyBuffer(alloc, staging, stagingAlloc);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_vkFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_vkImageView) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    return vkCreateSampler(dev, &samplerInfo, nullptr, &m_vkSampler) == VK_SUCCESS;
}

// --- CreateDynamic ---

bool Texture::CreateDynamic(Device& device, uint32_t width, uint32_t height, PixelFormat format)
{
    m_width = width;
    m_height = height;
    m_vkFormat = ToVkFormat((int)format);

    VkDevice dev = GetDev(device);


    // Use host-visible linear tiling for CPU-writable dynamic textures
    if (!VkCreateImage(GetAlloc(device), width, height, m_vkFormat,
        VK_IMAGE_TILING_LINEAR,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_vkImage, m_vkAllocation))
        return false;

    // Transition to general layout for host access
    VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, GetCmdPool(device));
    VkTransitionImageLayout(cmd, m_vkImage, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VkEndSingleTimeCommands(dev, GetCmdPool(device), GetQueue(device), cmd);
    m_vkLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_vkFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_vkImageView) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    return vkCreateSampler(dev, &samplerInfo, nullptr, &m_vkSampler) == VK_SUCCESS;
}

// --- UpdateFromRGBA ---

bool Texture::UpdateFromRGBA(Device& device, const void* pixels, uint32_t width, uint32_t height)
{
    if (!m_vkImage) return false;

    VkDevice dev = GetDev(device);
    VmaAllocator alloc = GetAlloc(device);

    // Map image memory directly (linear tiling, host-visible)
    void* mapped;
    if (vmaMapMemory(alloc, m_vkAllocation, &mapped) != VK_SUCCESS)
        return false;

    // Get subresource layout for proper row pitch
    VkImageSubresource subRes = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(dev, m_vkImage, &subRes, &layout);

    VkDeviceSize size = (VkDeviceSize)width * height * 4;
    if (layout.rowPitch == (VkDeviceSize)width * 4)
    {
        memcpy(mapped, pixels, size);
    }
    else
    {
        const uint8_t* src = (const uint8_t*)pixels;
        uint8_t* dst = (uint8_t*)mapped;
        for (uint32_t row = 0; row < height; row++)
        {
            memcpy(dst + row * layout.rowPitch, src + row * width * 4, width * 4);
        }
    }

    vmaUnmapMemory(alloc, m_vkAllocation);
    return true;
}

// --- CreateFromDDS ---

bool Texture::CreateFromDDS(Device& device, const void* data, size_t size)
{
    // DDS parsing is shared with D3D11 — reuses the same header parsing.
    // For compressed formats (BC2/BC3), Vulkan uses VK_FORMAT_BC*_UNORM_BLOCK.
    // DXT1 is decompressed to RGBA8 in the D3D11 path; do the same here.

    if (size < 128 || memcmp(data, "DDS ", 4) != 0)
        return false;

    struct DDSHeader
    {
        uint32_t magic, size, flags, height, width;
        uint32_t pitchOrLinearSize, depth, mipMapCount;
        uint32_t reserved1[11];
        uint32_t pfSize, pfFlags, pfFourCC, pfRGBBitCount;
        uint32_t pfRBitMask, pfGBitMask, pfBBitMask, pfABitMask;
        uint32_t caps, caps2, caps3, caps4, reserved2;
    };

    const DDSHeader* header = (const DDSHeader*)data;
    m_width = header->width;
    m_height = header->height;

    const uint32_t DDPF_FOURCC = 0x4;
    const bool isFourCC = (header->pfFlags & DDPF_FOURCC) != 0;

    // Decompress DXT1 (BC1) / DXT3 (BC2) / DXT5 (BC3) on the CPU to RGBA8
    // and then let CreateFromRGBA handle the upload + mip generation.
    // This matches the D3D11 fallback path and keeps the texture descriptor
    // format uniform (VK_FORMAT_R8G8B8A8_UNORM) across all game textures —
    // without it every non-DXT1 texture silently loads as default white,
    // which made terrain render as white × cloud-shadow noise.
    const bool isDXT1 = isFourCC && header->pfFourCC == '1TXD';
    const bool isDXT3 = isFourCC && header->pfFourCC == '3TXD';
    const bool isDXT5 = isFourCC && header->pfFourCC == '5TXD';

    if (isDXT1 || isDXT3 || isDXT5)
    {
        const uint32_t w = header->width;
        const uint32_t h = header->height;
        const uint32_t blockSize = isDXT1 ? 8u : 16u;
        const uint8_t* src = (const uint8_t*)data + 128;
        std::vector<uint32_t> pixels(w * h);

        const uint32_t bw = (w + 3) / 4;
        const uint32_t bh = (h + 3) / 4;
        for (uint32_t by = 0; by < bh; ++by)
        {
            for (uint32_t bx = 0; bx < bw; ++bx)
            {
                const uint8_t* block = src + (by * bw + bx) * blockSize;

                // BC3/DXT5 alpha block is the first 8 bytes: 2 anchors + 48 bits of 3-bit indices.
                // BC2/DXT3 alpha block is the first 8 bytes: 16 explicit 4-bit alpha values.
                // BC1/DXT1 has no alpha block; the color block decides alpha based on c0 vs c1.
                uint8_t alphaPalette[8] = {};
                uint64_t alphaLookup = 0;
                if (isDXT5)
                {
                    alphaPalette[0] = block[0];
                    alphaPalette[1] = block[1];
                    if (alphaPalette[0] > alphaPalette[1])
                    {
                        // 8-stop palette: fully interpolated.
                        for (int i = 1; i <= 6; ++i)
                            alphaPalette[i + 1] = (uint8_t)(((7 - i) * alphaPalette[0] + i * alphaPalette[1]) / 7);
                    }
                    else
                    {
                        // 6-stop palette + 0 and 255.
                        for (int i = 1; i <= 4; ++i)
                            alphaPalette[i + 1] = (uint8_t)(((5 - i) * alphaPalette[0] + i * alphaPalette[1]) / 5);
                        alphaPalette[6] = 0;
                        alphaPalette[7] = 255;
                    }
                    for (int i = 0; i < 6; ++i)
                        alphaLookup |= (uint64_t)block[2 + i] << (i * 8);
                }
                uint64_t explicitAlpha = 0;
                if (isDXT3)
                {
                    for (int i = 0; i < 8; ++i)
                        explicitAlpha |= (uint64_t)block[i] << (i * 8);
                }

                // Color block starts at offset 0 (DXT1) or 8 (DXT3/DXT5).
                const uint8_t* color = isDXT1 ? block : block + 8;
                const uint16_t c0 = color[0] | (color[1] << 8);
                const uint16_t c1 = color[2] | (color[3] << 8);

                uint8_t r[4], g[4], b[4];
                r[0] = ((c0 >> 11) & 0x1F) * 255 / 31;
                g[0] = ((c0 >>  5) & 0x3F) * 255 / 63;
                b[0] = ( c0        & 0x1F) * 255 / 31;
                r[1] = ((c1 >> 11) & 0x1F) * 255 / 31;
                g[1] = ((c1 >>  5) & 0x3F) * 255 / 63;
                b[1] = ( c1        & 0x1F) * 255 / 31;

                // DXT1 only: if c0<=c1, color index 3 is fully transparent.
                // DXT3/DXT5 always use the 4-stop interpolated palette.
                const bool dxt1Punch = isDXT1 && (c0 <= c1);
                if (!dxt1Punch)
                {
                    r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
                    r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
                }
                else
                {
                    r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
                    r[3] = g[3] = b[3] = 0;
                }

                const uint32_t colorLookup = color[4] | (color[5] << 8) | (color[6] << 16) | (color[7] << 24);
                for (int i = 0; i < 16; ++i)
                {
                    const uint32_t px = bx * 4 + (i % 4);
                    const uint32_t py = by * 4 + (i / 4);
                    if (px >= w || py >= h) continue;

                    const int idx = (colorLookup >> (i * 2)) & 3;
                    uint8_t pa;
                    if (isDXT1)
                        pa = (dxt1Punch && idx == 3) ? 0 : 255;
                    else if (isDXT3)
                    {
                        const uint8_t a4 = (explicitAlpha >> (i * 4)) & 0xF;
                        pa = (uint8_t)((a4 << 4) | a4); // 4-bit → 8-bit
                    }
                    else // DXT5
                    {
                        const int ai = (int)((alphaLookup >> (i * 3)) & 7);
                        pa = alphaPalette[ai];
                    }

                    pixels[py * w + px] = r[idx] | (g[idx] << 8) | (b[idx] << 16) | (pa << 24);
                }
            }
        }

        return CreateFromRGBA(device, pixels.data(), w, h, true);
    }

    // Uncompressed DDS with an explicit pixel format — upload the pixel data
    // directly. Only the common 32-bit A8R8G8B8 / X8R8G8B8 layout is handled;
    // rarer formats (A4R4G4B4, A1R5G5B5, L8A8, etc.) fall through as failure.
    if (!isFourCC && header->pfRGBBitCount == 32)
    {
        const uint32_t w = header->width, h = header->height;
        const uint8_t* src = (const uint8_t*)data + 128;
        std::vector<uint32_t> pixels(w * h);
        // DDS stores pixels with the R/G/B/A masks from the header; Generals'
        // files use the standard A8R8G8B8 (A=0xFF000000, R=0x00FF0000, ...)
        // which decodes cleanly via bitwise extraction — no endianness dance.
        const uint32_t rMask = header->pfRBitMask;
        const uint32_t gMask = header->pfGBitMask;
        const uint32_t bMask = header->pfBBitMask;
        const uint32_t aMask = header->pfABitMask;
        auto firstBit = [](uint32_t m) { int n = 0; while (m && !(m & 1)) { m >>= 1; n++; } return n; };
        const int rShift = firstBit(rMask);
        const int gShift = firstBit(gMask);
        const int bShift = firstBit(bMask);
        const int aShift = firstBit(aMask);
        for (uint32_t i = 0; i < w * h; ++i)
        {
            const uint32_t v = ((const uint32_t*)src)[i];
            const uint8_t r = (uint8_t)((v & rMask) >> rShift);
            const uint8_t g = (uint8_t)((v & gMask) >> gShift);
            const uint8_t b = (uint8_t)((v & bMask) >> bShift);
            const uint8_t a = aMask ? (uint8_t)((v & aMask) >> aShift) : 0xFF;
            pixels[i] = r | (g << 8) | (b << 16) | (a << 24);
        }
        return CreateFromRGBA(device, pixels.data(), w, h, true);
    }

    return false;
}

// --- CreateRenderTarget ---

bool Texture::CreateRenderTarget(Device& device, uint32_t width, uint32_t height, PixelFormat format)
{
    if (m_vkImage && m_width == width && m_height == height)
        return true; // already created at this size

    m_width = width;
    m_height = height;
    m_vkFormat = ToVkFormat((int)format);

    VkDevice dev = GetDev(device);


    if (!VkCreateImage(GetAlloc(device), width, height, m_vkFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vkImage, m_vkAllocation))
        return false;

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_vkFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_vkImageView) != VK_SUCCESS)
        return false;

    // Transition to SHADER_READ_ONLY so it can be sampled immediately.
    // When used as a render target, the render pass will handle transitions.
    VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, GetCmdPool(device));
    VkTransitionImageLayout(cmd, m_vkImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkEndSingleTimeCommands(dev, GetCmdPool(device), GetQueue(device), cmd);
    m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return true;
}

// --- CreateDepthTarget ---

bool Texture::CreateDepthTarget(Device& device, uint32_t width, uint32_t height)
{
    if (m_vkImage && m_width == width && m_height == height)
        return true;

    m_width = width;
    m_height = height;
    m_vkFormat = VK_FORMAT_D32_SFLOAT;
    m_vkIsDepthTarget = true;

    VkDevice dev = GetDev(device);


    if (!VkCreateImage(GetAlloc(device), width, height, m_vkFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vkImage, m_vkAllocation))
        return false;

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_vkFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_vkImageView) != VK_SUCCESS)
        return false;

    // Transition to shader-readable layout so it can be sampled as a depth texture
    VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, GetCmdPool(device));
    VkTransitionImageLayout(cmd, m_vkImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    VkEndSingleTimeCommands(dev, GetCmdPool(device), GetQueue(device), cmd);
    m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return true;
}

// --- Bind ---

bool Texture::IsValid() const { return m_vkImageView != VK_NULL_HANDLE; }

void Texture::BindPS(Device& device, uint32_t slot) const
{
    // Record this texture at the given slot for the next draw call.
    // The descriptor set will be written in Device::BindCurrentPipeline().
    if (slot < 5)
        device.m_vkBoundTextures[slot] = this;
}

void Texture::BindVS(Device& device, uint32_t slot) const
{
    // VS textures share the same descriptor set slots as PS textures.
    if (slot < 5)
        device.m_vkBoundTextures[slot] = this;
}

} // namespace Render

#endif // BUILD_WITH_VULKAN
