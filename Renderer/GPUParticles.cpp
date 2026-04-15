#include "GPUParticles.h"
#include "Shaders/GPUParticleShaders.h"
#include "Core/ShaderCompiler.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>

#ifdef BUILD_WITH_D3D11
#include <d3dcompiler.h>
#endif

#ifdef BUILD_WITH_VULKAN
#include "Core/Vulkan/VulkanUtil.h"
#endif

namespace Render {

// === Platform-independent code ===

GPUParticleSystem& GPUParticleSystem::Instance()
{
    static GPUParticleSystem s_instance;
    return s_instance;
}

void GPUParticleSystem::Shutdown()
{
#ifdef BUILD_WITH_D3D11
    m_particleBuffer.Reset();
    m_particleUAV.Reset();
    m_particleSRV.Reset();
    m_updateCB.Reset();
    m_renderCB.Reset();
    m_indexBuffer.Reset();
    m_csUpdate.Reset();
    m_vsRender.Reset();
    m_psRender.Reset();
#endif
    m_ready = false;
}

void GPUParticleSystem::CreateNoiseTexture(Device& device)
{
    const int SIZE = 64;
    std::vector<uint32_t> pixels(SIZE * SIZE);
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < SIZE; ++x)
        {
            float n = sinf(x * 12.9898f + y * 78.233f) * 43758.5453f;
            n = n - floorf(n);
            uint8_t v = (uint8_t)(n * 255.0f);
            pixels[y * SIZE + x] = (255u << 24) | (v << 16) | (v << 8) | v;
        }
    m_noiseTexture.CreateFromRGBA(device, pixels.data(), SIZE, SIZE, true);
}

void GPUParticleSystem::Emit(const Float3& position, const Float3& velocity, int type)
{
    if (m_stagingCount >= 256) return;

    GPUParticle& p = m_stagingBuffer[m_stagingCount++];
    memset(&p, 0, sizeof(p));
    p.position = position;
    p.velocity = velocity;
    p.age = 0.0f;
    p.alive = 1;

    if (type == 0)
    {
        p.lifetime = 1.5f + (rand() % 60) * 0.01f;
        p.startSize = 6.0f + (rand() % 100) * 0.03f;
        p.size = p.startSize;
        p.growRate = 0.0f;
        p.alpha = 1.0f;
        p.age = 0.001f;
        p.color = { 0.8f, 0.78f, 0.75f };
    }
    else
    {
        p.lifetime = 3.0f + (rand() % 150) * 0.01f;
        p.startSize = 1.5f + (rand() % 30) * 0.01f;
        p.size = p.startSize;
        p.growRate = 0.0f;
        p.alpha = 1.0f;
        p.age = 0.001f;
        p.color = { 0.7f, 0.72f, 0.75f };
    }
    p.size = p.startSize;
    p.alpha = 1.0f;
}

void GPUParticleSystem::EmitBatch(const Float3* positions, const Float3* velocities,
                                   const int* types, int count)
{
    for (int i = 0; i < count; ++i)
        Emit(positions[i], velocities[i], types[i]);
}

// === D3D11 backend ===

#ifdef BUILD_WITH_D3D11

bool GPUParticleSystem::Init(Device& device)
{
    auto* dev = device.GetDevice();
    if (!dev) return false;

    // Create particle structured buffer (GPU read/write)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(GPUParticle) * MAX_PARTICLES;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(GPUParticle);

        std::vector<GPUParticle> initData(MAX_PARTICLES);
        memset(initData.data(), 0, sizeof(GPUParticle) * MAX_PARTICLES);
        D3D11_SUBRESOURCE_DATA srd = {};
        srd.pSysMem = initData.data();

        if (FAILED(dev->CreateBuffer(&desc, &srd, m_particleBuffer.GetAddressOf())))
            return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = MAX_PARTICLES;
        if (FAILED(dev->CreateUnorderedAccessView(m_particleBuffer.Get(), &uavDesc, m_particleUAV.GetAddressOf())))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = MAX_PARTICLES;
        if (FAILED(dev->CreateShaderResourceView(m_particleBuffer.Get(), &srvDesc, m_particleSRV.GetAddressOf())))
            return false;
    }

    // Constant buffers
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = (sizeof(UpdateConstants) + 15) & ~15;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&desc, nullptr, m_updateCB.GetAddressOf())))
            return false;

        desc.ByteWidth = (sizeof(RenderConstants) + 15) & ~15;
        if (FAILED(dev->CreateBuffer(&desc, nullptr, m_renderCB.GetAddressOf())))
            return false;
    }

    // Index buffer for quads
    {
        std::vector<uint32_t> indices(MAX_PARTICLES * 6);
        for (uint32_t i = 0; i < MAX_PARTICLES; ++i)
        {
            uint32_t base = i * 4;
            indices[i * 6 + 0] = base + 0;
            indices[i * 6 + 1] = base + 1;
            indices[i * 6 + 2] = base + 2;
            indices[i * 6 + 3] = base + 0;
            indices[i * 6 + 4] = base + 2;
            indices[i * 6 + 5] = base + 3;
        }
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = (uint32_t)(indices.size() * sizeof(uint32_t));
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA srd = {};
        srd.pSysMem = indices.data();
        if (FAILED(dev->CreateBuffer(&desc, &srd, m_indexBuffer.GetAddressOf())))
            return false;
    }

    // Compile shaders
    {
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        HRESULT hr = D3DCompile(g_shaderGPUParticleUpdate, strlen(g_shaderGPUParticleUpdate),
            "GPUParticleUpdate", nullptr, nullptr, "CSUpdate", "cs_5_0", 0, 0,
            blob.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) return false;
        if (FAILED(dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_csUpdate.GetAddressOf())))
            return false;
    }
    {
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        HRESULT hr = D3DCompile(g_shaderGPUParticleRender, strlen(g_shaderGPUParticleRender),
            "GPUParticleRender", nullptr, nullptr, "VSParticle", "vs_5_0", 0, 0,
            blob.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) return false;
        if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_vsRender.GetAddressOf())))
            return false;
    }
    {
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        HRESULT hr = D3DCompile(g_shaderGPUParticleRender, strlen(g_shaderGPUParticleRender),
            "GPUParticleRender", nullptr, nullptr, "PSParticle", "ps_5_0", 0, 0,
            blob.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) return false;
        if (FAILED(dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, m_psRender.GetAddressOf())))
            return false;
    }

    // Pipeline states
    {
        BlendDesc desc;
        desc.enable = true;
        desc.srcColor = BlendFactor::One;
        desc.destColor = BlendFactor::InvSrcAlpha;
        desc.colorOp = BlendOp::Add;
        desc.srcAlpha = BlendFactor::One;
        desc.destAlpha = BlendFactor::InvSrcAlpha;
        desc.alphaOp = BlendOp::Add;
        desc.writeMask = 0x0F;
        m_blendPremultiplied.CreateFromDesc(device, desc);
    }
    m_depthNoWrite.Create(device, true, false, CompareFunc::LessEqual);
    m_rasterNoCull.Create(device, FillMode::Solid, CullMode::None, true);
    m_sampler.Create(device, Filter::MinMagMipLinear, AddressMode::Wrap);

    CreateNoiseTexture(device);

    m_stagingCount = 0;
    m_nextEmitSlot = 0;
    m_ready = true;
    return true;
}

void GPUParticleSystem::FlushStagingToGPU(Device& device)
{
    if (m_stagingCount == 0) return;

    auto* ctx = device.GetContext();

    for (int i = 0; i < m_stagingCount; ++i)
    {
        uint32_t slot = (m_nextEmitSlot + i) % MAX_PARTICLES;
        D3D11_BOX box = {};
        box.left = slot * sizeof(GPUParticle);
        box.right = box.left + sizeof(GPUParticle);
        box.bottom = 1;
        box.back = 1;
        ctx->UpdateSubresource(m_particleBuffer.Get(), 0, &box,
            &m_stagingBuffer[i], sizeof(GPUParticle), 0);
    }

    m_nextEmitSlot = (m_nextEmitSlot + m_stagingCount) % MAX_PARTICLES;
    m_stagingCount = 0;
}

void GPUParticleSystem::Update(Device& device, float deltaTime, const Float3& windDir)
{
    if (!m_ready) return;

    FlushStagingToGPU(device);

    auto* ctx = device.GetContext();

    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        ctx->Map(m_updateCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        UpdateConstants* cb = (UpdateConstants*)mapped.pData;
        cb->deltaTime = deltaTime;
        cb->turbulenceStrength = 8.0f;
        cb->dragCoeff = 0.3f;
        cb->gravity = 2.0f;
        cb->maxParticles = MAX_PARTICLES;
        cb->windDirection = windDir;
        ctx->Unmap(m_updateCB.Get(), 0);
    }

    ctx->CSSetShader(m_csUpdate.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, m_updateCB.GetAddressOf());
    ctx->CSSetUnorderedAccessViews(0, 1, m_particleUAV.GetAddressOf(), nullptr);

    uint32_t groups = (MAX_PARTICLES + 255) / 256;
    ctx->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

void GPUParticleSystem::Render(Device& device, const Float4x4& viewProjection,
                                const Float3& cameraPos, const Float3& cameraRight,
                                const Float3& cameraUp, float screenW, float screenH)
{
    if (!m_ready) return;

    auto* ctx = device.GetContext();

    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        ctx->Map(m_renderCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        RenderConstants* cb = (RenderConstants*)mapped.pData;
        cb->cameraRight = { cameraRight.x, cameraRight.y, cameraRight.z, 0 };
        cb->cameraUp = { cameraUp.x, cameraUp.y, cameraUp.z, 0 };
        cb->screenW = screenW;
        cb->screenH = screenH;
        cb->nearPlane = 1.0f;
        cb->farPlane = 5000.0f;
        cb->maxParticles = MAX_PARTICLES;
        cb->softDepthScale = 10.0f;
        cb->noiseScale = 2.0f;
        cb->opacityMultiplier = 0.07f;
        ctx->Unmap(m_renderCB.Get(), 0);
    }

    m_blendPremultiplied.Bind(device);
    m_depthNoWrite.Bind(device);
    m_rasterNoCull.Bind(device);

    ctx->VSSetShader(m_vsRender.Get(), nullptr, 0);
    ctx->PSSetShader(m_psRender.Get(), nullptr, 0);
    ctx->IASetInputLayout(nullptr);

    ctx->VSSetConstantBuffers(1, 1, m_renderCB.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, m_renderCB.GetAddressOf());

    ctx->VSSetShaderResources(0, 1, m_particleSRV.GetAddressOf());

    if (m_noiseTexture.GetSRV())
    {
        ID3D11ShaderResourceView* srv = m_noiseTexture.GetSRV();
        ctx->PSSetShaderResources(2, 1, &srv);
    }
    m_sampler.BindPS(device, 0);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->DrawIndexed(MAX_PARTICLES * 6, 0, 0);

    ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
    ctx->VSSetShaderResources(0, 1, nullSRV);
    ctx->PSSetShaderResources(2, 1, nullSRV);
}

#endif // BUILD_WITH_D3D11

// === Vulkan backend ===

#ifdef BUILD_WITH_VULKAN

// Access Device internals
struct VkDeviceAccess
{
    static VkDevice GetDevice(const Device& d) { return d.m_vkDevice; }
    static VkPhysicalDevice GetPhysDevice(const Device& d) { return d.m_vkPhysicalDevice; }
    static VkCommandPool GetCommandPool(const Device& d) { return d.m_vkCommandPool; }
    static VkQueue GetQueue(const Device& d) { return d.m_vkGraphicsQueue; }
    static VkCommandBuffer GetCmd(const Device& d) { return d.m_vkCommandBuffers[d.m_vkCurrentFrame]; }
    static VmaAllocator GetAllocator(const Device& d) { return d.m_vmaAllocator; }
    static bool IsRecording(const Device& d) { return d.m_vkRecording; }
    static bool InRenderPass(const Device& d) { return d.m_vkInRenderPass; }
    static void SetInRenderPass(Device& d, bool v) { d.m_vkInRenderPass = v; }
    static void BindSSBO(Device& d, VkBuffer buf, VkDeviceSize size) { d.m_vkBoundSSBO = buf; d.m_vkBoundSSBOSize = size; }
};

bool GPUParticleSystem::Init(Device& device)
{
    VkDevice dev = VkDeviceAccess::GetDevice(device);

    // Create particle SSBO
    if (!m_vkParticleBuffer.Create(device, sizeof(GPUParticle), MAX_PARTICLES))
        return false;

    // Zero-initialize particles
    {
        std::vector<GPUParticle> initData(MAX_PARTICLES);
        memset(initData.data(), 0, sizeof(GPUParticle) * MAX_PARTICLES);
        m_vkParticleBuffer.Upload(device, initData.data(), MAX_PARTICLES);
    }

    // Constant buffers
    if (!m_vkUpdateCB.Create(device, sizeof(UpdateConstants)))
        return false;
    if (!m_vkRenderCB.Create(device, sizeof(RenderConstants)))
        return false;

    // Index buffer for quads
    {
        std::vector<uint32_t> indices(MAX_PARTICLES * 6);
        for (uint32_t i = 0; i < MAX_PARTICLES; ++i)
        {
            uint32_t base = i * 4;
            indices[i * 6 + 0] = base + 0;
            indices[i * 6 + 1] = base + 1;
            indices[i * 6 + 2] = base + 2;
            indices[i * 6 + 3] = base + 0;
            indices[i * 6 + 4] = base + 2;
            indices[i * 6 + 5] = base + 3;
        }
        if (!m_vkIndexBuffer.Create32(device, indices.data(), MAX_PARTICLES * 6))
            return false;
    }

    // Load compute shader SPIR-V
    {
        auto spv = LoadShaderBytecode("Shaders/spirv/GPUParticleUpdate_CSUpdate.spv");
        if (spv.empty()) { m_ready = false; return true; } // Silently disable if no SPIR-V

        VkShaderModuleCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spv.size();
        ci.pCode = (const uint32_t*)spv.data();
        if (vkCreateShaderModule(dev, &ci, nullptr, &m_vkComputeModule) != VK_SUCCESS)
            return false;
    }

    // Load render shaders
    {
        auto vsSpv = LoadShaderBytecode("Shaders/spirv/GPUParticleRender_VSParticle.spv");
        auto psSpv = LoadShaderBytecode("Shaders/spirv/GPUParticleRender_PSParticle.spv");
        if (vsSpv.empty() || psSpv.empty()) { m_ready = false; return true; }

        // Set entry point names before loading SPIR-V (DXC preserves HLSL names)
        m_vkRenderShader.CompileVS(device, nullptr, "VSParticle");
        m_vkRenderShader.CompilePS(device, nullptr, "PSParticle");
        if (!m_vkRenderShader.LoadVS(device, vsSpv.data(), vsSpv.size()))
            return false;
        if (!m_vkRenderShader.LoadPS(device, psSpv.data(), psSpv.size()))
            return false;
        // No input layout needed — VS generates vertices from SV_VertexID
    }

    // Create compute descriptor set layout
    // Binding 0: UpdateConstants (UBO)
    // Binding 30: particles (SSBO, read/write)
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 30;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_vkComputeDSLayout) != VK_SUCCESS)
            return false;
    }

    // Create compute pipeline layout
    {
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_vkComputeDSLayout;
        if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &m_vkComputeLayout) != VK_SUCCESS)
            return false;
    }

    // Create compute pipeline
    {
        VkComputePipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = m_vkComputeModule;
        pipelineInfo.stage.pName = "CSUpdate";
        pipelineInfo.layout = m_vkComputeLayout;
        if (vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_vkComputePipeline) != VK_SUCCESS)
            return false;
    }

    // Descriptor pool for compute
    {
        VkDescriptorPoolSize poolSizes[2] = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 4;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = 4;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 4;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_vkComputeDescPool) != VK_SUCCESS)
            return false;
    }

    // Pipeline states (shared with D3D11 path)
    {
        BlendDesc desc;
        desc.enable = true;
        desc.srcColor = BlendFactor::One;
        desc.destColor = BlendFactor::InvSrcAlpha;
        desc.colorOp = BlendOp::Add;
        desc.srcAlpha = BlendFactor::One;
        desc.destAlpha = BlendFactor::InvSrcAlpha;
        desc.alphaOp = BlendOp::Add;
        desc.writeMask = 0x0F;
        m_blendPremultiplied.CreateFromDesc(device, desc);
    }
    m_depthNoWrite.Create(device, true, false, CompareFunc::LessEqual);
    m_rasterNoCull.Create(device, FillMode::Solid, CullMode::None, true);
    m_sampler.Create(device, Filter::MinMagMipLinear, AddressMode::Wrap);

    CreateNoiseTexture(device);

    m_stagingCount = 0;
    m_nextEmitSlot = 0;

    m_ready = true;
    return true;
}

void GPUParticleSystem::FlushStagingToGPU(Device& device)
{
    if (m_stagingCount == 0) return;

    // Upload new particles to their FIFO slots in the SSBO via staging buffer
    VmaAllocator alloc = VkDeviceAccess::GetAllocator(device);
    VkDevice dev = VkDeviceAccess::GetDevice(device);

    for (int i = 0; i < m_stagingCount; ++i)
    {
        uint32_t slot = (m_nextEmitSlot + i) % MAX_PARTICLES;
        VkDeviceSize offset = slot * sizeof(GPUParticle);

        // Create a tiny staging buffer for this particle
        VkBuffer staging;
        VmaAllocation stagingAlloc;
        VkCreateBuffer(alloc, sizeof(GPUParticle), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging, stagingAlloc);

        void* mapped;
        vmaMapMemory(alloc, stagingAlloc, &mapped);
        memcpy(mapped, &m_stagingBuffer[i], sizeof(GPUParticle));
        vmaUnmapMemory(alloc, stagingAlloc);

        VkCommandBuffer cmd = VkBeginSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device));
        VkBufferCopy copy = {};
        copy.srcOffset = 0;
        copy.dstOffset = offset;
        copy.size = sizeof(GPUParticle);
        vkCmdCopyBuffer(cmd, staging, m_vkParticleBuffer.GetVkBuffer(), 1, &copy);
        VkEndSingleTimeCommands(dev, VkDeviceAccess::GetCommandPool(device),
                                VkDeviceAccess::GetQueue(device), cmd);

        vmaDestroyBuffer(alloc, staging, stagingAlloc);
    }

    m_nextEmitSlot = (m_nextEmitSlot + m_stagingCount) % MAX_PARTICLES;
    m_stagingCount = 0;
}

void GPUParticleSystem::Update(Device& device, float deltaTime, const Float3& windDir)
{
    if (!m_ready || !VkDeviceAccess::IsRecording(device)) return;

    FlushStagingToGPU(device);

    VkDevice dev = VkDeviceAccess::GetDevice(device);
    VkCommandBuffer cmd = VkDeviceAccess::GetCmd(device);

    // Update constants
    UpdateConstants uc;
    uc.deltaTime = deltaTime;
    uc.turbulenceStrength = 8.0f;
    uc.dragCoeff = 0.3f;
    uc.gravity = 2.0f;
    uc.maxParticles = MAX_PARTICLES;
    uc.windDirection = windDir;
    m_vkUpdateCB.Update(device, &uc, sizeof(uc));

    // Allocate compute descriptor set
    VkDescriptorSet ds;
    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_vkComputeDescPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_vkComputeDSLayout;
        if (vkAllocateDescriptorSets(dev, &allocInfo, &ds) != VK_SUCCESS)
            return;
    }

    // Write descriptors
    VkDescriptorBufferInfo uboBuf = {};
    uboBuf.buffer = m_vkUpdateCB.GetVkBuffer();
    uboBuf.range = sizeof(UpdateConstants);

    VkDescriptorBufferInfo ssboBuf = {};
    ssboBuf.buffer = m_vkParticleBuffer.GetVkBuffer();
    ssboBuf.range = sizeof(GPUParticle) * MAX_PARTICLES;

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &uboBuf;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ds;
    writes[1].dstBinding = 30;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &ssboBuf;

    vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);

    // End render pass if active (compute can't run inside a render pass)
    bool wasInRenderPass = VkDeviceAccess::InRenderPass(device);
    if (wasInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        VkDeviceAccess::SetInRenderPass(device, false);
    }

    // Memory barrier: ensure particle data is available for compute read/write
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.buffer = m_vkParticleBuffer.GetVkBuffer();
    barrier.size = VK_WHOLE_SIZE;
    barrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);

    // Dispatch compute
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_vkComputePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_vkComputeLayout, 0, 1, &ds, 0, nullptr);
    vkCmdDispatch(cmd, (MAX_PARTICLES + 255) / 256, 1, 1);

    // Barrier: compute write → vertex read
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);

    // Re-enter render pass
    if (wasInRenderPass)
        device.SetBackBuffer();

    // Free the descriptor set for reuse
    vkFreeDescriptorSets(dev, m_vkComputeDescPool, 1, &ds);
}

void GPUParticleSystem::Render(Device& device, const Float4x4& viewProjection,
                                const Float3& cameraPos, const Float3& cameraRight,
                                const Float3& cameraUp, float screenW, float screenH)
{
    if (!m_ready || !VkDeviceAccess::InRenderPass(device)) return;

    // Update render constants
    RenderConstants rc;
    rc.cameraRight = { cameraRight.x, cameraRight.y, cameraRight.z, 0 };
    rc.cameraUp = { cameraUp.x, cameraUp.y, cameraUp.z, 0 };
    rc.screenW = screenW;
    rc.screenH = screenH;
    rc.nearPlane = 1.0f;
    rc.farPlane = 5000.0f;
    rc.maxParticles = MAX_PARTICLES;
    rc.softDepthScale = 10.0f;
    rc.noiseScale = 2.0f;
    rc.opacityMultiplier = 0.07f;
    m_vkRenderCB.Update(device, &rc, sizeof(rc));

    // Bind pipeline states
    m_blendPremultiplied.Bind(device);
    m_depthNoWrite.Bind(device);
    m_rasterNoCull.Bind(device);
    m_vkRenderShader.Bind(device);

    // Bind constant buffers
    // b0 = FrameConstants (already bound by Renderer), b1 = RenderConstants
    m_vkRenderCB.BindVS(device, 1);
    m_vkRenderCB.BindPS(device, 1);

    // Bind noise texture at t2
    m_noiseTexture.BindPS(device, 2);
    m_sampler.BindPS(device, 0);

    // Bind particle SSBO at binding 30 (u0 in HLSL)
    VkDeviceAccess::BindSSBO(device, m_vkParticleBuffer.GetVkBuffer(),
        sizeof(GPUParticle) * MAX_PARTICLES);

    device.SetTopology(Topology::TriangleList);
    m_vkIndexBuffer.Bind(device);
    device.DrawIndexed(MAX_PARTICLES * 6, 0, 0);
}

#endif // BUILD_WITH_VULKAN

} // namespace Render
