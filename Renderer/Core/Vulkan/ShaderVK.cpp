// D3D11 wins when both backends are enabled — the Shader/state member
// bodies in Renderer/Core/Shader.cpp are authoritative in that case.
// Pure-Vulkan builds (USE_D3D11=OFF) activate this TU.
#if defined(BUILD_WITH_VULKAN) && !defined(BUILD_WITH_D3D11)

#include "../Shader.h"
#include "../Device.h"
#include "VulkanUtil.h"
#include <cstring>
#include <cstdio>
#include <vector>

namespace Render
{

// Access Device internals via friend struct
struct VkDeviceAccess
{
    static VkDevice GetDevice(const Device& d) { return d.m_vkDevice; }
    static VkPhysicalDevice GetPhysicalDevice(const Device& d) { return d.m_vkPhysicalDevice; }
    static VkCommandPool GetCommandPool(const Device& d) { return d.m_vkCommandPool; }
    static VkQueue GetGraphicsQueue(const Device& d) { return d.m_vkGraphicsQueue; }
    static VkCommandBuffer GetCurrentCmd(const Device& d) { return d.m_vkCommandBuffers[d.m_vkCurrentFrame]; }
};

static VkDevice GetDev(Device& d) { return VkDeviceAccess::GetDevice(d); }
static VkPhysicalDevice GetPhysDev(Device& d) { return VkDeviceAccess::GetPhysicalDevice(d); }

static VkShaderModule CreateShaderModule(VkDevice dev, const void* code, size_t size)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = (const uint32_t*)code;

    VkShaderModule module;
    if (vkCreateShaderModule(dev, &createInfo, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

// --- Shader ---

void Shader::Destroy(Device& device)
{
    VkDevice dev = GetDev(device);
    if (m_vkVertModule) { vkDestroyShaderModule(dev, m_vkVertModule, nullptr); m_vkVertModule = VK_NULL_HANDLE; }
    if (m_vkFragModule) { vkDestroyShaderModule(dev, m_vkFragModule, nullptr); m_vkFragModule = VK_NULL_HANDLE; }
}

bool Shader::LoadVS(Device& device, const void* bytecode, size_t size)
{
    m_vkVertModule = CreateShaderModule(GetDev(device), bytecode, size);
    return m_vkVertModule != VK_NULL_HANDLE;
}

bool Shader::LoadPS(Device& device, const void* bytecode, size_t size)
{
    m_vkFragModule = CreateShaderModule(GetDev(device), bytecode, size);
    return m_vkFragModule != VK_NULL_HANDLE;
}

bool Shader::CompileVS(Device& /*device*/, const char* /*source*/, const char* entryPoint, const char* /*profile*/)
{
    // Store the entry point name for when LoadVS is called with SPIR-V.
    // DXC preserves the original HLSL entry point name in SPIR-V (not "main").
    if (entryPoint) m_vkVertEntry = entryPoint;
    return false; // Runtime HLSL compilation not supported on Vulkan
}

bool Shader::CompilePS(Device& /*device*/, const char* /*source*/, const char* entryPoint, const char* /*profile*/)
{
    if (entryPoint) m_vkFragEntry = entryPoint;
    return false;
}

bool Shader::CreateInputLayout(Device& /*device*/, const VertexAttribute* attributes, uint32_t count, uint32_t vertexStride)
{
    // Store vertex layout for VkPipeline creation later
    m_vkVertexAttributes.assign(attributes, attributes + count);
    m_vkVertexStride = vertexStride;
    return true;
}

void Shader::Bind(Device& device) const
{
    // Record this shader as the current one. The actual VkPipeline
    // will be looked up and bound at draw time.
    device.m_vkCurrentShader = this;
    device.m_vkStateDirty = true;
}

// --- RasterizerState ---

bool RasterizerState::Create(Device& /*device*/, FillMode fill, CullMode cull, bool frontCCW, int depthBias, bool /*scissorEnable*/, float slopeScaledDepthBias)
{
    m_vkFill = fill;
    m_vkCull = cull;
    m_vkFrontCCW = frontCCW;
    m_vkDepthBias = depthBias;
    m_vkSlopeScaledDepthBias = slopeScaledDepthBias;
    return true;
}

void RasterizerState::Bind(Device& device) const
{
    device.m_vkCurrentRaster = this;
    device.m_vkStateDirty = true;
}

// --- BlendState ---

bool BlendState::CreateOpaque(Device& /*device*/)
{
    m_vkDesc = {};
    m_vkDesc.enable = false;
    m_vkDesc.writeMask = 0x0F;
    return true;
}

bool BlendState::CreateAlphaBlend(Device& /*device*/)
{
    m_vkDesc.enable = true;
    m_vkDesc.srcColor = BlendFactor::SrcAlpha;
    m_vkDesc.destColor = BlendFactor::InvSrcAlpha;
    m_vkDesc.colorOp = BlendOp::Add;
    m_vkDesc.srcAlpha = BlendFactor::One;
    m_vkDesc.destAlpha = BlendFactor::InvSrcAlpha;
    m_vkDesc.alphaOp = BlendOp::Add;
    m_vkDesc.writeMask = 0x0F;
    return true;
}

bool BlendState::CreateAdditive(Device& /*device*/)
{
    m_vkDesc.enable = true;
    m_vkDesc.srcColor = BlendFactor::SrcAlpha;
    m_vkDesc.destColor = BlendFactor::One;
    m_vkDesc.colorOp = BlendOp::Add;
    m_vkDesc.srcAlpha = BlendFactor::One;
    m_vkDesc.destAlpha = BlendFactor::One;
    m_vkDesc.alphaOp = BlendOp::Add;
    m_vkDesc.writeMask = 0x0F;
    return true;
}

bool BlendState::CreateMultiplicative(Device& /*device*/)
{
    m_vkDesc.enable = true;
    m_vkDesc.srcColor = BlendFactor::Zero;
    m_vkDesc.destColor = BlendFactor::SrcColor;
    m_vkDesc.colorOp = BlendOp::Add;
    m_vkDesc.srcAlpha = BlendFactor::Zero;
    m_vkDesc.destAlpha = BlendFactor::SrcAlpha;
    m_vkDesc.alphaOp = BlendOp::Add;
    m_vkDesc.writeMask = 0x0F;
    return true;
}

bool BlendState::CreateFromDesc(Device& /*device*/, const BlendDesc& desc)
{
    m_vkDesc = desc;
    return true;
}

void BlendState::Bind(Device& device) const
{
    device.m_vkCurrentBlend = this;
    device.m_vkStateDirty = true;
}

// --- DepthStencilState ---

bool DepthStencilState::Create(Device& /*device*/, bool depthEnable, bool depthWrite, CompareFunc compareFunc)
{
    m_vkDepthEnable = depthEnable;
    m_vkDepthWrite = depthWrite;
    m_vkCompareFunc = compareFunc;
    return true;
}

void DepthStencilState::Bind(Device& device, uint8_t /*stencilRef*/) const
{
    device.m_vkCurrentDepth = this;
    device.m_vkStateDirty = true;
}

// --- SamplerState ---

static VkFilter ToVkFilter(Filter f)
{
    switch (f) {
    case Filter::MinMagMipPoint: return VK_FILTER_NEAREST;
    default:                     return VK_FILTER_LINEAR;
    }
}

static VkSamplerAddressMode ToVkAddressMode(AddressMode m)
{
    switch (m) {
    case AddressMode::Clamp:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case AddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    default:                  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

void SamplerState::Destroy(Device& device)
{
    VkDevice dev = GetDev(device);
    if (m_vkSampler) { vkDestroySampler(dev, m_vkSampler, nullptr); m_vkSampler = VK_NULL_HANDLE; }
}

bool SamplerState::Create(Device& device, Filter filter, AddressMode addressMode)
{
    VkSamplerCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = ToVkFilter(filter);
    info.minFilter = ToVkFilter(filter);
    info.mipmapMode = (filter == Filter::MinMagMipPoint) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = ToVkAddressMode(addressMode);
    info.addressModeV = ToVkAddressMode(addressMode);
    info.addressModeW = ToVkAddressMode(addressMode);
    info.maxAnisotropy = (filter == Filter::Anisotropic) ? 16.0f : 1.0f;
    info.anisotropyEnable = (filter == Filter::Anisotropic) ? VK_TRUE : VK_FALSE;
    info.maxLod = VK_LOD_CLAMP_NONE;

    return vkCreateSampler(GetDev(device), &info, nullptr, &m_vkSampler) == VK_SUCCESS;
}

bool SamplerState::CreateComparison(Device& device, Filter filter, AddressMode addressMode)
{
    VkSamplerCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = ToVkFilter(filter);
    info.minFilter = ToVkFilter(filter);
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = ToVkAddressMode(addressMode);
    info.addressModeV = ToVkAddressMode(addressMode);
    info.addressModeW = ToVkAddressMode(addressMode);
    info.compareEnable = VK_TRUE;
    info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    info.maxLod = VK_LOD_CLAMP_NONE;

    return vkCreateSampler(GetDev(device), &info, nullptr, &m_vkSampler) == VK_SUCCESS;
}

void SamplerState::BindVS(Device& /*device*/, uint32_t /*slot*/) const
{
    // Samplers are bound via descriptor sets in Vulkan.
}

void SamplerState::BindPS(Device& /*device*/, uint32_t /*slot*/) const
{
    // Samplers are bound via descriptor sets in Vulkan.
}

// --- ComputeShader ---

bool ComputeShader::Compile(Device& /*device*/, const char* /*source*/, const char* /*entryPoint*/, const char* /*profile*/)
{
    fprintf(stderr, "[Vulkan] ComputeShader::Compile: runtime compilation not supported. Use pre-compiled SPIR-V.\n");
    return false;
}

void ComputeShader::Bind(Device& /*device*/) const
{
    // Compute pipeline binding — will use m_vkPipeline when created.
}

void ComputeShader::Dispatch(Device& device, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) const
{
    VkCommandBuffer cmd = VkDeviceAccess::GetCurrentCmd(device);
    if (m_vkPipeline)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_vkPipeline);
        vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
    }
}

} // namespace Render

#endif // BUILD_WITH_VULKAN
