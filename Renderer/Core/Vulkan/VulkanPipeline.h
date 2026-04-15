#pragma once

#ifdef BUILD_WITH_VULKAN

#include <vulkan/vulkan.h>
#include "../RHITypes.h"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Render
{

class Device;
class Shader;
class RasterizerState;
class BlendState;
class DepthStencilState;
class Texture;
class ConstantBuffer;

// Manages Vulkan descriptor set layouts, descriptor pools, and the
// graphics pipeline cache. Pipelines are created lazily from the
// current combination of shader + rasterizer + blend + depth state.
//
// Descriptor layout matches the HLSL register scheme:
//   Binding 0:  FrameConstants   (uniform buffer, b0)
//   Binding 1:  ObjectConstants  (uniform buffer, b1)
//   Binding 2:  MeshDecalConstants (uniform buffer, b2)
//   Binding 3:  diffuseTexture   (combined image sampler, t0)
//   Binding 4:  bumpTexture      (combined image sampler, t1)
//   Binding 5:  depthTexture     (combined image sampler, t2)
//   Binding 6:  shroudTexture    (combined image sampler, t3)
//   Binding 7:  shadowMap        (combined image sampler, t4)
class VulkanPipelineManager
{
public:
    bool Init(VkDevice device, VkPhysicalDevice physDevice, VkRenderPass renderPass);
    void Shutdown(VkDevice device);

    // Get or create a VkPipeline for the current state combination.
    // renderPass may differ from m_renderPass for off-screen targets.
    VkPipeline GetOrCreatePipeline(
        VkDevice device,
        const Shader* shader,
        const RasterizerState* raster,
        const BlendState* blend,
        const DepthStencilState* depth,
        Topology topology,
        VkRenderPass renderPass = VK_NULL_HANDLE);

    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_descriptorSetLayout; }

    // Allocate a descriptor set from the pool for this frame.
    VkDescriptorSet AllocateDescriptorSet(VkDevice device);

    // Reset the per-frame descriptor pool (call at start of frame).
    void ResetDescriptorPool(VkDevice device);

private:
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    static constexpr uint32_t MAX_FRAMES = 2;
    VkDescriptorPool m_descriptorPools[MAX_FRAMES] = {};
    uint32_t m_currentPoolIndex = 0;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    // Pipeline cache keyed by combined state hash
    struct PipelineKey
    {
        const Shader* shader;
        const RasterizerState* raster;
        const BlendState* blend;
        const DepthStencilState* depth;
        Topology topology;
        VkRenderPass renderPass;

        bool operator==(const PipelineKey& o) const
        {
            return shader == o.shader && raster == o.raster &&
                   blend == o.blend && depth == o.depth &&
                   topology == o.topology && renderPass == o.renderPass;
        }
    };

    struct PipelineKeyHash
    {
        size_t operator()(const PipelineKey& k) const
        {
            size_t h = (size_t)k.shader;
            h ^= (size_t)k.raster << 1;
            h ^= (size_t)k.blend << 2;
            h ^= (size_t)k.depth << 3;
            h ^= (size_t)k.topology << 4;
            h ^= (size_t)k.renderPass << 5;
            return h;
        }
    };

    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> m_pipelineCache;

    VkPipeline CreateGraphicsPipeline(
        VkDevice device,
        const Shader* shader,
        const RasterizerState* raster,
        const BlendState* blend,
        const DepthStencilState* depth,
        Topology topology,
        VkRenderPass renderPass);
};

} // namespace Render

#endif // BUILD_WITH_VULKAN
