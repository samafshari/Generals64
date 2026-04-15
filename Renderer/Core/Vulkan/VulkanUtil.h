#pragma once

#ifdef BUILD_WITH_VULKAN

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

namespace Render
{

class Device;

// --- Memory allocation helpers (VMA-backed) ---

// Create a VkBuffer with VMA sub-allocation.
bool VkCreateBuffer(VmaAllocator allocator,
                    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps,
                    VkBuffer& outBuffer, VmaAllocation& outAllocation);

// Create a VkImage with VMA sub-allocation.
bool VkCreateImage(VmaAllocator allocator,
                   uint32_t width, uint32_t height, VkFormat format,
                   VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps,
                   VkImage& outImage, VmaAllocation& outAllocation,
                   uint32_t mipLevels = 1);

// Legacy overloads that use raw VkDeviceMemory (for device-level resources created before VMA init)
uint32_t VkFindMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
bool VkCreateBuffer(VkDevice device, VkPhysicalDevice physDevice,
                    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps,
                    VkBuffer& outBuffer, VkDeviceMemory& outMemory);
bool VkCreateImage(VkDevice device, VkPhysicalDevice physDevice,
                   uint32_t width, uint32_t height, VkFormat format,
                   VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps,
                   VkImage& outImage, VkDeviceMemory& outMemory,
                   uint32_t mipLevels = 1);

// --- Single-time command buffer helpers ---

VkCommandBuffer VkBeginSingleTimeCommands(VkDevice device, VkCommandPool pool);
void VkEndSingleTimeCommands(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd);

// --- Image layout transitions ---

void VkTransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             uint32_t mipLevels = 1);

// Copy buffer data to an image (for texture uploads).
void VkCopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image,
                         uint32_t width, uint32_t height);

// Copy one image to another (same dimensions).
void VkCopyImageToImage(VkCommandBuffer cmd, VkImage src, VkImage dst,
                        uint32_t width, uint32_t height,
                        VkImageLayout srcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VkImageLayout dstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

// --- Format conversion ---

VkFormat ToVkFormat(int rhiPixelFormat); // PixelFormat enum -> VkFormat

} // namespace Render

#endif // BUILD_WITH_VULKAN
