#pragma once

#include "RHITypes.h"
#include <vector>

#ifdef BUILD_WITH_D3D11
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#ifdef BUILD_WITH_VULKAN
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
namespace Render { class VulkanPipelineManager; }
#endif

namespace Render
{

class Texture;

struct DeviceConfig
{
    void* nativeWindowHandle = nullptr; // HWND on Windows, NSWindow* on macOS
    int width = 0;
    int height = 0;
    bool vsync = true;
    bool debug = false;
    int msaaSamples = 1;
};

class Device
{
public:
    bool Init(const DeviceConfig& config);
    void Shutdown();

    void BeginFrame(float clearR, float clearG, float clearB, float clearA = 1.0f);
    void EndFrame();
    void Resize(int width, int height);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsInitialized() const { return m_initialized; }

    // --- RHI draw commands (backend-agnostic) ---

    void SetViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f);
    void SetTopology(Topology topology);
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0);
    void Draw(uint32_t vertexCount, uint32_t startVertex = 0);
    void DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertex = 0, uint32_t startInstance = 0);
    void ClearInputLayout();

    // --- RHI render target management ---

    // Bind a Texture as the color render target (no depth).
    void SetRenderTarget(Texture& colorRT);
    // Bind a Texture as depth-only render target (null color).
    void SetDepthOnlyRenderTarget(Texture& depthRT);
    // Restore the default backbuffer + main depth stencil.
    void SetBackBuffer();
    // Restore backbuffer with read-only depth (for sampling depth in PS).
    void SetBackBufferReadOnlyDepth();
    // Clear a render target texture to a color.
    void ClearRenderTarget(Texture& rt, float r, float g, float b, float a);
    void ClearDepthStencil(Texture& depthRT);

    // --- RHI texture copies ---

    void CopyBackBufferToTexture(Texture& dst);
    void CopyTextureToBackBuffer(Texture& src);
    void CopyTexture(Texture& dst, Texture& src);

    // --- RHI SRV binding ---

    // Unbind pixel shader SRVs at the given slot range.
    void UnbindPSSRVs(uint32_t startSlot, uint32_t count = 1);
    // Unbind vertex shader SRVs at the given slot range.
    void UnbindVSSRVs(uint32_t startSlot, uint32_t count = 1);
    // Unbind vertex shader sampler at a slot.
    void UnbindVSSamplers(uint32_t startSlot, uint32_t count = 1);
    // Unbind vertex shader constant buffer at a slot.
    void UnbindVSConstantBuffers(uint32_t startSlot, uint32_t count = 1);
    // Bind multiple textures to consecutive PS SRV slots. Null entries are allowed.
    void BindPSTextures(uint32_t startSlot, const Texture* const* textures, uint32_t count);
    // Bind the device's depth buffer as a PS SRV at the given slot.
    void BindDepthTexturePS(uint32_t slot);

    // Capture the current backbuffer to a BMP file
    bool CaptureScreenshot(const char* filename);

    // --- Game viewport redirect ---
    //
    // When a redirect RTV is set, all the SetBackBuffer-style calls
    // bind THAT target instead of the real swap-chain backbuffer,
    // and BeginFrame clears it as well. The inspector uses this to
    // make the engine render into an off-screen texture which gets
    // displayed inside a draggable ImGui "Game" window — so panels
    // can be docked around the game without occluding pixels.
    //
    // Pass nullptr to clear the redirect (engine renders directly to
    // the backbuffer again, current default behavior).
    void SetRedirectRTV(ID3D11RenderTargetView* rtv);
    bool IsRedirectActive() const { return m_redirectRTV != nullptr; }

    // Variants that ALWAYS bind the real swap-chain backbuffer,
    // regardless of redirect state. Used by the inspector to draw
    // ImGui directly to the visible swap chain after the engine has
    // rendered into the redirect target.
    void SetBackBufferDirect();
    void ClearBackBufferDirect(float r, float g, float b, float a);

    // --- D3D11-specific accessors (used only by Core/ internals) ---
#ifdef BUILD_WITH_D3D11
    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    ID3D11RenderTargetView* GetBackBufferRTV() const { return m_backBufferRTV.Get(); }
    ID3D11DepthStencilView* GetDepthStencilView() const { return m_depthStencilView.Get(); }
    ID3D11DepthStencilView* GetDepthStencilViewReadOnly() const { return m_depthStencilViewRO.Get(); }
    ID3D11ShaderResourceView* GetDepthSRV() const { return m_depthSRV.Get(); }
    IDXGISwapChain1* GetSwapChain() const { return m_swapChain.Get(); }
#endif

    // Vulkan backend internals need access to device handles and state tracking
    friend struct VkDeviceAccess;
    friend class Shader;
    friend class RasterizerState;
    friend class BlendState;
    friend class DepthStencilState;
    friend class Texture;
    friend class ConstantBuffer;

private:
#ifdef BUILD_WITH_D3D11
    bool CreateSwapChain();
    bool CreateBackBufferViews();
    void ReleaseBackBufferViews();

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backBufferRTV;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11DepthStencilView> m_depthStencilViewRO;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;
    ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
    void* m_nativeWindow = nullptr;

    // Optional render-target redirect (game viewport mode). When
    // non-null, all SetBackBuffer / BeginFrame calls bind this RTV
    // instead of the real backbuffer. Owned by the renderer (Texture)
    // — Device just holds a non-owning pointer.
    ID3D11RenderTargetView* m_redirectRTV = nullptr;
#endif

#ifdef BUILD_WITH_VULKAN
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    bool CreateVkInstance(bool debug);
    bool CreateVkSurface(void* nativeWindowHandle);
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapChainVK();
    bool CreateDepthResources();
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateCommandPool();
    bool CreateSyncObjects();
    void CleanupSwapChainVK();

    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_vkDebugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkQueue m_vkGraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_vkPresentQueue = VK_NULL_HANDLE;
    uint32_t m_vkGraphicsFamily = 0;
    uint32_t m_vkPresentFamily = 0;

    VkSurfaceKHR m_vkSurface = VK_NULL_HANDLE;
    VkSwapchainKHR m_vkSwapChain = VK_NULL_HANDLE;
    VkFormat m_vkSwapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D m_vkSwapChainExtent = {};
    std::vector<VkImage> m_vkSwapChainImages;
    std::vector<VkImageView> m_vkSwapChainImageViews;

    VkImage m_vkDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_vkDepthMemory = VK_NULL_HANDLE;
    VkImageView m_vkDepthView = VK_NULL_HANDLE;      // Depth+stencil view for attachment
    VkImageView m_vkDepthSRView = VK_NULL_HANDLE;    // Depth-only view for sampling in PS

    // Depth texture binding: when BindDepthTexturePS is called, this view is used
    // at the specified slot in the next descriptor set write.
    VkImageView m_vkBoundDepthView = VK_NULL_HANDLE;
    uint32_t m_vkBoundDepthSlot = UINT32_MAX;

    VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;          // loadOp=CLEAR (first begin)
    VkRenderPass m_vkRenderPassLoad = VK_NULL_HANDLE;      // loadOp=LOAD (re-entry after off-screen)
    VkRenderPass m_vkRenderPassReadOnlyDepth = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_vkFramebuffersReadOnlyDepth;
    std::vector<VkFramebuffer> m_vkFramebuffers;

    VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_vkCommandBuffers[MAX_FRAMES_IN_FLIGHT] = {};
    VkSemaphore m_vkImageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT] = {};
    VkSemaphore m_vkRenderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT] = {};
    VkFence m_vkInFlightFences[MAX_FRAMES_IN_FLIGHT] = {};
    uint32_t m_vkCurrentFrame = 0;
    uint32_t m_vkImageIndex = 0;

    VulkanPipelineManager* m_vkPipelineManager = nullptr;
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

    // Current pipeline state tracking — updated by Bind() calls,
    // used by Draw/DrawIndexed to look up the correct VkPipeline.
    const class Shader* m_vkCurrentShader = nullptr;
    const class RasterizerState* m_vkCurrentRaster = nullptr;
    const class BlendState* m_vkCurrentBlend = nullptr;
    const class DepthStencilState* m_vkCurrentDepth = nullptr;
    Topology m_vkCurrentTopology = Topology::TriangleList;
    bool m_vkStateDirty = true;
    VkPipeline m_vkBoundPipeline = VK_NULL_HANDLE;

    // Current resource bindings — constant buffers and textures.
    // Written to a descriptor set at draw time.
    VkBuffer m_vkBoundCBs[3] = {};       // b0, b1, b2
    uint32_t m_vkBoundCBSizes[3] = {};
    const class Texture* m_vkBoundTextures[5] = {}; // t0-t4
    VkBuffer m_vkBoundSSBO = VK_NULL_HANDLE;       // Storage buffer at binding 30
    VkDeviceSize m_vkBoundSSBOSize = 0;

    bool m_vkInRenderPass = false; // Track if a render pass is active
    bool m_vkRecording = false;    // Track if command buffer is in recording state
    VkRenderPass m_vkActiveRenderPass = VK_NULL_HANDLE; // Which render pass is currently active
    Texture* m_vkActiveRT = nullptr; // Currently bound off-screen render target (null = backbuffer)

    VkImageLayout m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Track the current layout of the acquired swapchain image.
    // After render pass ends, this is PRESENT_SRC_KHR (render pass finalLayout).
    // During render pass, it's COLOR_ATTACHMENT_OPTIMAL.
    VkImageLayout m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Per-swapchain-image fences to track which frame-in-flight fence
    // is associated with each swapchain image (prevents semaphore reuse).
    std::vector<VkFence> m_vkImagesInFlight;

    // Default 1x1 white texture + sampler for filling unused descriptor slots
    VkImage m_vkDefaultImage = VK_NULL_HANDLE;
    VkDeviceMemory m_vkDefaultMemory = VK_NULL_HANDLE;
    VkImageView m_vkDefaultImageView = VK_NULL_HANDLE;
    VkSampler m_vkDefaultSampler = VK_NULL_HANDLE;

    // Default 1x1 depth texture for shadow map slots that use comparison samplers
    VkImage m_vkDefaultDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_vkDefaultDepthMemory = VK_NULL_HANDLE;
    VkImageView m_vkDefaultDepthView = VK_NULL_HANDLE;

    bool CreateDefaultResources();
    void BindCurrentPipeline(); // Look up + bind pipeline + descriptor set
#endif

    int m_width = 0;
    int m_height = 0;
    bool m_vsync = true;
    bool m_initialized = false;
    bool m_tearingSupported = false; // DXGI 1.5 PRESENT_ALLOW_TEARING — defeats DWM vsync-pacing in windowed mode
};

} // namespace Render
