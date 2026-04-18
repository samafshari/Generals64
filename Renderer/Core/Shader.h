#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <string>
#include <vector>

#ifdef BUILD_WITH_D3D11
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#ifdef BUILD_WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace Render
{

class Device;

class Shader
{
public:
    void Destroy(Device& device);
    bool LoadVS(Device& device, const void* bytecode, size_t size);
    bool LoadPS(Device& device, const void* bytecode, size_t size);
    bool CompileVS(Device& device, const char* source, const char* entryPoint, const char* profile = "vs_5_0");
    bool CompilePS(Device& device, const char* source, const char* entryPoint, const char* profile = "ps_5_0");

    bool CreateInputLayout(Device& device, const VertexAttribute* attributes, uint32_t count, uint32_t vertexStride);

    void Bind(Device& device) const;

#ifdef BUILD_WITH_D3D11
    ID3DBlob* GetVSBytecode() const { return m_vsBytecode.Get(); }
#endif

    friend struct VkDeviceAccess;
    friend class VulkanPipelineManager;
    friend class Device;

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3DBlob> m_vsBytecode;
#endif
#ifdef BUILD_WITH_VULKAN
    VkShaderModule m_vkVertModule = VK_NULL_HANDLE;
    VkShaderModule m_vkFragModule = VK_NULL_HANDLE;
    std::string m_vkVertEntry = "main";
    std::string m_vkFragEntry = "main";
    std::vector<VertexAttribute> m_vkVertexAttributes;
    uint32_t m_vkVertexStride = 0;
#endif
};

class RasterizerState
{
public:
    bool Create(Device& device, FillMode fill, CullMode cull, bool frontCCW = true, int depthBias = 0, bool scissorEnable = false, float slopeScaledDepthBias = 0.0f);
    void Bind(Device& device) const;

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11RasterizerState> m_state;
#endif
#ifdef BUILD_WITH_VULKAN
    // Vulkan bakes rasterizer state into VkPipeline. Store config for pipeline creation.
    friend class VulkanPipelineManager;
    FillMode m_vkFill = FillMode::Solid;
    CullMode m_vkCull = CullMode::Back;
    bool m_vkFrontCCW = true;
    int m_vkDepthBias = 0;
    float m_vkSlopeScaledDepthBias = 0.0f;
#endif
};

class BlendState
{
public:
    bool CreateOpaque(Device& device);
    bool CreateAlphaBlend(Device& device);
    bool CreateAdditive(Device& device);
    bool CreateMultiplicative(Device& device);
    bool CreateFromDesc(Device& device, const BlendDesc& desc);
    void Bind(Device& device) const;

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11BlendState> m_state;
#endif
#ifdef BUILD_WITH_VULKAN
    friend class VulkanPipelineManager;
    BlendDesc m_vkDesc;
#endif
};

class DepthStencilState
{
public:
    bool Create(Device& device, bool depthEnable, bool depthWrite, CompareFunc compareFunc = CompareFunc::LessEqual);
    void Bind(Device& device, uint8_t stencilRef = 0) const;

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11DepthStencilState> m_state;
#endif
#ifdef BUILD_WITH_VULKAN
    friend class VulkanPipelineManager;
    bool m_vkDepthEnable = true;
    bool m_vkDepthWrite = true;
    CompareFunc m_vkCompareFunc = CompareFunc::LessEqual;
#endif
};

class SamplerState
{
public:
    void Destroy(Device& device);
    bool Create(Device& device, Filter filter, AddressMode addressMode);
    bool CreateComparison(Device& device, Filter filter, AddressMode addressMode);
    void BindVS(Device& device, uint32_t slot) const;
    void BindPS(Device& device, uint32_t slot) const;

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11SamplerState> m_state;
#endif
#ifdef BUILD_WITH_VULKAN
    VkSampler m_vkSampler = VK_NULL_HANDLE;
#endif
};

class ComputeShader
{
public:
    bool Compile(Device& device, const char* source, const char* entryPoint, const char* profile = "cs_5_0");
    void Bind(Device& device) const;
    void Dispatch(Device& device, uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) const;

private:
#ifdef BUILD_WITH_D3D11
    ComPtr<ID3D11ComputeShader> m_shader;
#endif
#ifdef BUILD_WITH_VULKAN
    VkShaderModule m_vkModule = VK_NULL_HANDLE;
    VkPipeline m_vkPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_vkLayout = VK_NULL_HANDLE;
#endif
};

} // namespace Render
