#pragma once
#include "Core/Device.h"
#include "Core/Shader.h"
#include "Core/Buffer.h"
#include "Core/Texture.h"
#include "Math/RenderMath.h"
#include <cstdint>

namespace Render {

// GPU-simulated volumetric smoke particle system.
// Uses a fixed-size FIFO ring buffer — oldest particles are overwritten
// when the pool is full. Particles persist independently of their source
// object and fade out naturally based on their lifetime.
class GPUParticleSystem
{
public:
    static GPUParticleSystem& Instance();

    bool Init(Device& device);
    void Shutdown();

    // Emit a smoke particle at the given world position
    // type: 0 = missile exhaust (bright, expanding), 1 = contrail (thin, wispy)
    void Emit(const Float3& position, const Float3& velocity, int type);

    // Batch emit for efficiency
    void EmitBatch(const Float3* positions, const Float3* velocities,
                   const int* types, int count);

    // Update all particles (call once per frame before rendering)
    void Update(Device& device, float deltaTime, const Float3& windDir);

    // Render all alive particles
    void Render(Device& device, const Float4x4& viewProjection,
                const Float3& cameraPos, const Float3& cameraRight,
                const Float3& cameraUp, float screenW, float screenH);

    bool IsReady() const { return m_ready; }

private:
    GPUParticleSystem() = default;

    struct GPUParticle
    {
        Float3 position;
        float  age;
        Float3 velocity;
        float  lifetime;
        float  size;
        float  startSize;
        float  growRate;
        float  alpha;
        Float3 color;
        uint32_t alive;
    };

    struct UpdateConstants
    {
        float deltaTime;
        float turbulenceStrength;
        float dragCoeff;
        float gravity;
        uint32_t maxParticles;
        Float3 windDirection;
    };

    struct RenderConstants
    {
        Float4 cameraRight;
        Float4 cameraUp;
        float screenW, screenH;
        float nearPlane, farPlane;
        uint32_t maxParticles;
        float softDepthScale;
        float noiseScale;
        float opacityMultiplier;
    };

    static constexpr uint32_t MAX_PARTICLES = 16384;

    // CPU-side staging for new emissions (flushed to GPU in Update)
    GPUParticle m_stagingBuffer[256];
    int m_stagingCount = 0;
    uint32_t m_nextEmitSlot = 0; // FIFO write head

#ifdef BUILD_WITH_D3D11
    // GPU resources (D3D11-specific)
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_particleBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_particleUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_particleSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_updateCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_renderCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;

    // Shaders
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csUpdate;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vsRender;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psRender;
#endif

#ifdef BUILD_WITH_VULKAN
    // Vulkan GPU resources
    ComputeBuffer m_vkParticleBuffer;     // SSBO for particle data
    ConstantBuffer m_vkUpdateCB;          // UpdateConstants uniform
    ConstantBuffer m_vkRenderCB;          // RenderConstants uniform
    IndexBuffer m_vkIndexBuffer;          // Quad index buffer
    Shader m_vkRenderShader;              // VS+PS for rendering

    // Compute pipeline (separate from graphics pipeline cache)
    VkShaderModule m_vkComputeModule = VK_NULL_HANDLE;
    VkPipeline m_vkComputePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_vkComputeLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_vkComputeDSLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_vkComputeDescPool = VK_NULL_HANDLE;
#endif

    // Blend/depth states
    BlendState m_blendPremultiplied;
    DepthStencilState m_depthNoWrite;
    RasterizerState m_rasterNoCull;

    // Noise texture for volumetric look
    Texture m_noiseTexture;
    SamplerState m_sampler;

    bool m_ready = false;

    void CreateNoiseTexture(Device& device);
    void FlushStagingToGPU(Device& device);
};

} // namespace Render
