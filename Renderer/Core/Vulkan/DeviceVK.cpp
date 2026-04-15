#ifdef BUILD_WITH_VULKAN

#include "../Device.h"
#include "../Texture.h"
#include "../ShaderCompiler.h"
#include "VulkanUtil.h"
#include "VulkanPipeline.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

// Debug logging that appears in VS Output window
static void VkLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, "%s", buf);
#ifdef _WIN32
    OutputDebugStringA(buf);
#endif
}

namespace Render
{

// Validation layer callback
static VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "[Vulkan] %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

static int s_vkFrameDrawCount = 0;
static int s_vkFrameNumber = 0;

// --- Init / Shutdown ---

bool Device::Init(const DeviceConfig& config)
{
    m_vsync = config.vsync;
    m_width = config.width > 0 ? config.width : 800;
    m_height = config.height > 0 ? config.height : 600;

    if (!CreateVkInstance(config.debug)) return false;
    if (!CreateVkSurface(config.nativeWindowHandle)) return false;
    if (!PickPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;

    // Create VMA allocator — must be done right after device creation, before any resource allocation
    {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = m_vkPhysicalDevice;
        allocatorInfo.device = m_vkDevice;
        allocatorInfo.instance = m_vkInstance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
        if (vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator) != VK_SUCCESS)
            return false;
    }

    if (!CreateSwapChainVK()) return false;
    if (!CreateDepthResources()) return false;
    if (!CreateRenderPass()) return false;
    if (!CreateFramebuffers()) return false;
    if (!CreateCommandPool()) return false;
    if (!CreateSyncObjects()) return false;

    // Initialize pipeline manager (descriptor layouts, pool, pipeline cache)
    m_vkPipelineManager = new VulkanPipelineManager();
    if (!m_vkPipelineManager->Init(m_vkDevice, m_vkPhysicalDevice, m_vkRenderPass))
        return false;

    if (!CreateDefaultResources())
        return false;

    m_initialized = true;
    return true;
}

bool Device::CreateDefaultResources()
{
    // Create a 1x1 white texture for filling unused descriptor image slots.
    // Without this, drawing with uninitialized image descriptors crashes.
    uint32_t whitePixel = 0xFFFFFFFF;

    VkCreateImage(m_vkDevice, m_vkPhysicalDevice, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vkDefaultImage, m_vkDefaultMemory);

    // Upload pixel via staging
    VkBuffer staging; VkDeviceMemory stagingMem;
    VkCreateBuffer(m_vkDevice, m_vkPhysicalDevice, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging, stagingMem);
    void* mapped;
    vkMapMemory(m_vkDevice, stagingMem, 0, 4, 0, &mapped);
    memcpy(mapped, &whitePixel, 4);
    vkUnmapMemory(m_vkDevice, stagingMem);

    VkCommandBuffer cmd = VkBeginSingleTimeCommands(m_vkDevice, m_vkCommandPool);
    VkTransitionImageLayout(cmd, m_vkDefaultImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCopyBufferToImage(cmd, staging, m_vkDefaultImage, 1, 1);
    VkTransitionImageLayout(cmd, m_vkDefaultImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkEndSingleTimeCommands(m_vkDevice, m_vkCommandPool, m_vkGraphicsQueue, cmd);

    vkDestroyBuffer(m_vkDevice, staging, nullptr);
    vkFreeMemory(m_vkDevice, stagingMem, nullptr);

    // Image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_vkDefaultImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCreateImageView(m_vkDevice, &viewInfo, nullptr, &m_vkDefaultImageView);

    // Default sampler
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(m_vkDevice, &samplerInfo, nullptr, &m_vkDefaultSampler);

    // Create a 1x1 depth texture for shadow map slots that use comparison samplers.
    // The default RGBA8 texture doesn't support SAMPLED_IMAGE_DEPTH_COMPARISON_BIT.
    VkCreateImage(m_vkDevice, m_vkPhysicalDevice, 1, 1, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vkDefaultDepthImage, m_vkDefaultDepthMemory);

    // Transition depth image to shader-readable layout
    {
        VkCommandBuffer dcmd = VkBeginSingleTimeCommands(m_vkDevice, m_vkCommandPool);
        VkTransitionImageLayout(dcmd, m_vkDefaultDepthImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT);
        VkEndSingleTimeCommands(m_vkDevice, m_vkCommandPool, m_vkGraphicsQueue, dcmd);
    }

    VkImageViewCreateInfo depthViewInfo = {};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_vkDefaultDepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    vkCreateImageView(m_vkDevice, &depthViewInfo, nullptr, &m_vkDefaultDepthView);

    return m_vkDefaultImageView && m_vkDefaultSampler && m_vkDefaultDepthView;
}

void Device::Shutdown()
{
    if (m_vkDevice)
        vkDeviceWaitIdle(m_vkDevice);

    // Destroy default resources
    if (m_vkDefaultDepthView) { vkDestroyImageView(m_vkDevice, m_vkDefaultDepthView, nullptr); m_vkDefaultDepthView = VK_NULL_HANDLE; }
    if (m_vkDefaultDepthImage) { vkDestroyImage(m_vkDevice, m_vkDefaultDepthImage, nullptr); m_vkDefaultDepthImage = VK_NULL_HANDLE; }
    if (m_vkDefaultDepthMemory) { vkFreeMemory(m_vkDevice, m_vkDefaultDepthMemory, nullptr); m_vkDefaultDepthMemory = VK_NULL_HANDLE; }
    if (m_vkDefaultSampler) { vkDestroySampler(m_vkDevice, m_vkDefaultSampler, nullptr); m_vkDefaultSampler = VK_NULL_HANDLE; }
    if (m_vkDefaultImageView) { vkDestroyImageView(m_vkDevice, m_vkDefaultImageView, nullptr); m_vkDefaultImageView = VK_NULL_HANDLE; }
    if (m_vkDefaultImage) { vkDestroyImage(m_vkDevice, m_vkDefaultImage, nullptr); m_vkDefaultImage = VK_NULL_HANDLE; }
    if (m_vkDefaultMemory) { vkFreeMemory(m_vkDevice, m_vkDefaultMemory, nullptr); m_vkDefaultMemory = VK_NULL_HANDLE; }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (m_vkRenderFinishedSemaphores[i]) vkDestroySemaphore(m_vkDevice, m_vkRenderFinishedSemaphores[i], nullptr);
        if (m_vkImageAvailableSemaphores[i]) vkDestroySemaphore(m_vkDevice, m_vkImageAvailableSemaphores[i], nullptr);
        if (m_vkInFlightFences[i]) vkDestroyFence(m_vkDevice, m_vkInFlightFences[i], nullptr);
    }

    if (m_vkCommandPool) vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);

    CleanupSwapChainVK();

    if (m_vkPipelineManager)
    {
        m_vkPipelineManager->Shutdown(m_vkDevice);
        delete m_vkPipelineManager;
        m_vkPipelineManager = nullptr;
    }

    if (m_vkRenderPassReadOnlyDepth) vkDestroyRenderPass(m_vkDevice, m_vkRenderPassReadOnlyDepth, nullptr);
    if (m_vkRenderPassLoad) vkDestroyRenderPass(m_vkDevice, m_vkRenderPassLoad, nullptr);
    if (m_vkRenderPass) vkDestroyRenderPass(m_vkDevice, m_vkRenderPass, nullptr);
    if (m_vmaAllocator) { vmaDestroyAllocator(m_vmaAllocator); m_vmaAllocator = VK_NULL_HANDLE; }
    if (m_vkDevice) vkDestroyDevice(m_vkDevice, nullptr);
    if (m_vkSurface) vkDestroySurfaceKHR(m_vkInstance, m_vkSurface, nullptr);

    if (m_vkDebugMessenger)
    {
        auto destroyFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_vkInstance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyFunc)
            destroyFunc(m_vkInstance, m_vkDebugMessenger, nullptr);
    }

    if (m_vkInstance) vkDestroyInstance(m_vkInstance, nullptr);

    m_initialized = false;
}

// --- Instance creation ---

bool Device::CreateVkInstance(bool debug)
{
    // Disable the Vulkan profiles layer (from Vulkan SDK Configurator) which
    // emulates lower-capability devices and can interfere with rendering.
#ifdef _WIN32
    _putenv_s("VK_LOADER_LAYERS_DISABLE", "VK_LAYER_KHRONOS_profiles");
#else
    setenv("VK_LOADER_LAYERS_DISABLE", "VK_LAYER_KHRONOS_profiles", 1);
#endif

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Generals Zero Hour";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "WW3D";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    // Get required extensions from SDL
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
    if (debug)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    if (debug)
        layers.push_back("VK_LAYER_KHRONOS_validation");

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = (uint32_t)layers.size();
    createInfo.ppEnabledLayerNames = layers.data();

#ifdef __APPLE__
    // MoltenVK requires portability enumeration
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();
#endif

    if (vkCreateInstance(&createInfo, nullptr, &m_vkInstance) != VK_SUCCESS)
        return false;

    // Set up debug messenger
    if (debug)
    {
        VkDebugUtilsMessengerCreateInfoEXT dbgInfo = {};
        dbgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgInfo.pfnUserCallback = VkDebugCallback;

        auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_vkInstance, "vkCreateDebugUtilsMessengerEXT");
        if (createFunc)
            createFunc(m_vkInstance, &dbgInfo, nullptr, &m_vkDebugMessenger);
    }

    return true;
}

bool Device::CreateVkSurface(void* nativeWindowHandle)
{
    // SDL3 handles all platform-specific surface creation
    SDL_Window* window = nullptr;

    // Find the SDL window from the native handle — or use the Platform singleton
    // On the SDL path, the native handle was extracted from SDLPlatform, so we
    // need the SDL_Window to create the Vulkan surface.
    // The caller should pass the SDL_Window pointer via DeviceConfig in the future.
    // For now, iterate SDL windows to find it.
    // Actually, SDLPlatform stores the window — we can get it from there.

    // Import the SDLPlatform singleton
    extern SDL_Window* GetSDLWindowForVulkan();
    window = GetSDLWindowForVulkan();

    if (!window)
        return false;

    return SDL_Vulkan_CreateSurface(window, m_vkInstance, nullptr, &m_vkSurface);
}

bool Device::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, devices.data());

    // Pick the first device that has graphics + present queue families
    for (auto& dev : devices)
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, families.data());

        bool foundGraphics = false, foundPresent = false;
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_vkGraphicsFamily = i;
                foundGraphics = true;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_vkSurface, &presentSupport);
            if (presentSupport)
            {
                m_vkPresentFamily = i;
                foundPresent = true;
            }

            if (foundGraphics && foundPresent)
                break;
        }

        if (foundGraphics && foundPresent)
        {
            m_vkPhysicalDevice = dev;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            fprintf(stderr, "[Vulkan] Using GPU: %s\n", props.deviceName);
            return true;
        }
    }

    return false;
}

bool Device::CreateLogicalDevice()
{
    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;

    VkDeviceQueueCreateInfo graphicsQueueInfo = {};
    graphicsQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphicsQueueInfo.queueFamilyIndex = m_vkGraphicsFamily;
    graphicsQueueInfo.queueCount = 1;
    graphicsQueueInfo.pQueuePriorities = &queuePriority;
    queueInfos.push_back(graphicsQueueInfo);

    if (m_vkPresentFamily != m_vkGraphicsFamily)
    {
        VkDeviceQueueCreateInfo presentQueueInfo = graphicsQueueInfo;
        presentQueueInfo.queueFamilyIndex = m_vkPresentFamily;
        queueInfos.push_back(presentQueueInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures = {};

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    // Enable VK_KHR_portability_subset if the device supports it
    // (required by MoltenVK on macOS, and by Vulkan SDK profiles layer)
    {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &extCount, exts.data());
        for (const auto& ext : exts)
        {
            if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0)
            {
                deviceExtensions.push_back("VK_KHR_portability_subset");
                break;
            }
        }
    }

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = (uint32_t)queueInfos.size();
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(m_vkPhysicalDevice, &createInfo, nullptr, &m_vkDevice) != VK_SUCCESS)
        return false;

    vkGetDeviceQueue(m_vkDevice, m_vkGraphicsFamily, 0, &m_vkGraphicsQueue);
    vkGetDeviceQueue(m_vkDevice, m_vkPresentFamily, 0, &m_vkPresentQueue);

    return true;
}

// --- Swap chain ---

bool Device::CreateSwapChainVK()
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vkPhysicalDevice, m_vkSurface, &caps);

    // Choose extent
    if (caps.currentExtent.width != UINT32_MAX)
    {
        m_vkSwapChainExtent = caps.currentExtent;
    }
    else
    {
        m_vkSwapChainExtent.width = std::clamp((uint32_t)m_width, caps.minImageExtent.width, caps.maxImageExtent.width);
        m_vkSwapChainExtent.height = std::clamp((uint32_t)m_height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    m_vkSwapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;

    VkSwapchainCreateInfoKHR swapInfo = {};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = m_vkSurface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = m_vkSwapChainFormat;
    swapInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapInfo.imageExtent = m_vkSwapChainExtent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = m_vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    swapInfo.clipped = VK_TRUE;

    uint32_t queueFamilyIndices[] = { m_vkGraphicsFamily, m_vkPresentFamily };
    if (m_vkGraphicsFamily != m_vkPresentFamily)
    {
        swapInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapInfo.queueFamilyIndexCount = 2;
        swapInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(m_vkDevice, &swapInfo, nullptr, &m_vkSwapChain) != VK_SUCCESS)
        return false;

    // Get swap chain images
    vkGetSwapchainImagesKHR(m_vkDevice, m_vkSwapChain, &imageCount, nullptr);
    m_vkSwapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_vkDevice, m_vkSwapChain, &imageCount, m_vkSwapChainImages.data());

    // Create image views
    m_vkSwapChainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_vkSwapChainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_vkSwapChainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_vkDevice, &viewInfo, nullptr, &m_vkSwapChainImageViews[i]) != VK_SUCCESS)
            return false;
    }

    m_width = m_vkSwapChainExtent.width;
    m_height = m_vkSwapChainExtent.height;

    // Initialize per-swapchain-image fence tracking (all null = no frame owns this image yet)
    m_vkImagesInFlight.resize(imageCount, VK_NULL_HANDLE);

    return true;
}

bool Device::CreateDepthResources()
{
    VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { m_vkSwapChainExtent.width, m_vkSwapChainExtent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_vkDevice, &imageInfo, nullptr, &m_vkDepthImage) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_vkDevice, m_vkDepthImage, &memReqs);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = VkFindMemoryType(m_vkPhysicalDevice, memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_vkDevice, &allocInfo, nullptr, &m_vkDepthMemory) != VK_SUCCESS)
        return false;
    vkBindImageMemory(m_vkDevice, m_vkDepthImage, m_vkDepthMemory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_vkDepthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_vkDevice, &viewInfo, nullptr, &m_vkDepthView) != VK_SUCCESS)
        return false;

    // Create a depth-only image view for sampling in fragment shaders (water foam, etc.)
    VkImageViewCreateInfo srvInfo = viewInfo;
    srvInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // Depth only, no stencil
    return vkCreateImageView(m_vkDevice, &srvInfo, nullptr, &m_vkDepthSRView) == VK_SUCCESS;
}

bool Device::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_vkSwapChainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D24_UNORM_S8_UINT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_vkDevice, &renderPassInfo, nullptr, &m_vkRenderPass) != VK_SUCCESS)
        return false;

    // Re-entry variant: preserve the existing swapchain contents when returning
    // from off-screen or read-only passes back to the backbuffer.
    {
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentDescription atts[] = { colorAttachment, depthAttachment };
        renderPassInfo.pAttachments = atts;
        if (vkCreateRenderPass(m_vkDevice, &renderPassInfo, nullptr, &m_vkRenderPassLoad) != VK_SUCCESS)
            return false;
    }

    // Read-only depth variant: same as main pass but depth is LOAD/STORE_DONT_CARE
    // and uses DEPTH_STENCIL_READ_ONLY layout so it can be sampled in PS.
    {
        VkAttachmentDescription colorAtt = {};
        colorAtt.format = m_vkSwapChainFormat;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAtt = {};
        depthAtt.format = VK_FORMAT_D24_UNORM_S8_UINT;
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

        VkSubpassDescription sp = {};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &colorRef;
        sp.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep = {};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription atts[] = { colorAtt, depthAtt };
        VkRenderPassCreateInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments = atts;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &sp;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        if (vkCreateRenderPass(m_vkDevice, &rpInfo, nullptr, &m_vkRenderPassReadOnlyDepth) != VK_SUCCESS)
            return false;
    }

    return true;
}

bool Device::CreateFramebuffers()
{
    m_vkFramebuffers.resize(m_vkSwapChainImageViews.size());
    for (size_t i = 0; i < m_vkSwapChainImageViews.size(); i++)
    {
        VkImageView attachments[] = { m_vkSwapChainImageViews[i], m_vkDepthView };

        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_vkRenderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_vkSwapChainExtent.width;
        fbInfo.height = m_vkSwapChainExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_vkDevice, &fbInfo, nullptr, &m_vkFramebuffers[i]) != VK_SUCCESS)
            return false;
    }

    // Framebuffers for read-only depth pass (uses same image views, different render pass)
    m_vkFramebuffersReadOnlyDepth.resize(m_vkSwapChainImageViews.size());
    for (size_t i = 0; i < m_vkSwapChainImageViews.size(); i++)
    {
        VkImageView attachments[] = { m_vkSwapChainImageViews[i], m_vkDepthView };

        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_vkRenderPassReadOnlyDepth;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_vkSwapChainExtent.width;
        fbInfo.height = m_vkSwapChainExtent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_vkDevice, &fbInfo, nullptr, &m_vkFramebuffersReadOnlyDepth[i]) != VK_SUCCESS)
            return false;
    }

    return true;
}

bool Device::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_vkGraphicsFamily;

    if (vkCreateCommandPool(m_vkDevice, &poolInfo, nullptr, &m_vkCommandPool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_vkCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    return vkAllocateCommandBuffers(m_vkDevice, &allocInfo, m_vkCommandBuffers) == VK_SUCCESS;
}

bool Device::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(m_vkDevice, &semInfo, nullptr, &m_vkImageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_vkDevice, &semInfo, nullptr, &m_vkRenderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_vkDevice, &fenceInfo, nullptr, &m_vkInFlightFences[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

void Device::CleanupSwapChainVK()
{
    for (auto fb : m_vkFramebuffersReadOnlyDepth)
        vkDestroyFramebuffer(m_vkDevice, fb, nullptr);
    m_vkFramebuffersReadOnlyDepth.clear();

    for (auto fb : m_vkFramebuffers)
        vkDestroyFramebuffer(m_vkDevice, fb, nullptr);
    m_vkFramebuffers.clear();

    if (m_vkDepthSRView) { vkDestroyImageView(m_vkDevice, m_vkDepthSRView, nullptr); m_vkDepthSRView = VK_NULL_HANDLE; }
    if (m_vkDepthView) { vkDestroyImageView(m_vkDevice, m_vkDepthView, nullptr); m_vkDepthView = VK_NULL_HANDLE; }
    if (m_vkDepthImage) { vkDestroyImage(m_vkDevice, m_vkDepthImage, nullptr); m_vkDepthImage = VK_NULL_HANDLE; }
    if (m_vkDepthMemory) { vkFreeMemory(m_vkDevice, m_vkDepthMemory, nullptr); m_vkDepthMemory = VK_NULL_HANDLE; }

    for (auto view : m_vkSwapChainImageViews)
        vkDestroyImageView(m_vkDevice, view, nullptr);
    m_vkSwapChainImageViews.clear();

    if (m_vkSwapChain) { vkDestroySwapchainKHR(m_vkDevice, m_vkSwapChain, nullptr); m_vkSwapChain = VK_NULL_HANDLE; }
}

// --- Frame cycle ---

void Device::BeginFrame(float clearR, float clearG, float clearB, float clearA)
{
    // Wait for this frame slot's previous work to finish
    vkWaitForFences(m_vkDevice, 1, &m_vkInFlightFences[m_vkCurrentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(m_vkDevice, 1, &m_vkInFlightFences[m_vkCurrentFrame]);

    // Reset command buffer BEFORE descriptor pool — resetting the pool invalidates
    // descriptor sets, which invalidates any command buffer that references them.
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    vkResetCommandBuffer(cmd, 0);

    // Now safe to reset the descriptor pool
    if (m_vkPipelineManager)
        m_vkPipelineManager->ResetDescriptorPool(m_vkDevice);

    VkResult result = vkAcquireNextImageKHR(m_vkDevice, m_vkSwapChain, UINT64_MAX,
        m_vkImageAvailableSemaphores[m_vkCurrentFrame], VK_NULL_HANDLE, &m_vkImageIndex);

    if (s_vkFrameNumber < 5)
        VkLog("[Vulkan] BeginFrame #%d: acquire=%d imageIdx=%u extent=%ux%u\n",
              s_vkFrameNumber, result, m_vkImageIndex,
              m_vkSwapChainExtent.width, m_vkSwapChainExtent.height);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        VkLog("[Vulkan] BeginFrame: swapchain out of date, resizing\n");
        Resize(m_width, m_height);
        return;
    }

    // If a different frame-in-flight was using this swapchain image, wait for it
    if (m_vkImagesInFlight[m_vkImageIndex] != VK_NULL_HANDLE &&
        m_vkImagesInFlight[m_vkImageIndex] != m_vkInFlightFences[m_vkCurrentFrame])
        vkWaitForFences(m_vkDevice, 1, &m_vkImagesInFlight[m_vkImageIndex], VK_TRUE, UINT64_MAX);
    m_vkImagesInFlight[m_vkImageIndex] = m_vkInFlightFences[m_vkCurrentFrame];

    // Reset pipeline state and resource bindings for new frame
    m_vkBoundPipeline = VK_NULL_HANDLE;
    m_vkStateDirty = true;
    m_vkCurrentShader = nullptr;
    memset(m_vkBoundCBs, 0, sizeof(m_vkBoundCBs));
    memset(m_vkBoundCBSizes, 0, sizeof(m_vkBoundCBSizes));
    memset(m_vkBoundTextures, 0, sizeof(m_vkBoundTextures));
    m_vkBoundDepthView = VK_NULL_HANDLE;
    m_vkBoundDepthSlot = UINT32_MAX;
    m_vkBoundSSBO = VK_NULL_HANDLE;
    m_vkBoundSSBOSize = 0;

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);
    m_vkRecording = true;

    VkClearValue clearValues[2] = {};
    clearValues[0].color = {{ clearR, clearG, clearB, clearA }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_vkRenderPass;
    rpInfo.framebuffer = m_vkFramebuffers[m_vkImageIndex];
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = m_vkSwapChainExtent;
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_vkInRenderPass = true;
    m_vkActiveRenderPass = m_vkRenderPass;
    m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // DEBUG: Clear to CYAN at start of frame. If game draws work, they'll overwrite this.
    // If screen stays cyan, game draws produce zero output.
    // If screen is BLACK, the render pass clear itself doesn't work.
    // If screen is MAGENTA, the render pass clear works but vkCmdClearAttachments doesn't.
    {
        VkClearAttachment ca = {};
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ca.clearValue.color = {{ 0, 1, 1, 1 }};
        VkClearRect cr = {};
        cr.rect = { {0,0}, m_vkSwapChainExtent };
        cr.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
    }
}

void Device::EndFrame()
{
    s_vkFrameNumber++;
    s_vkFrameDrawCount = 0;

    if (!m_vkRecording)
    {
        VkLog("[Vulkan] EndFrame #%d: SKIPPED — command buffer not recording (BeginFrame returned early?)\n", s_vkFrameNumber);
        m_vkCurrentFrame = (m_vkCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        return;
    }

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    if (m_vkInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        m_vkInRenderPass = false;
        // Update layout tracking based on which render pass was active
        if (m_vkActiveRT)
        {
            // Off-screen: finalLayout = SHADER_READ_ONLY_OPTIMAL
            m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            m_vkActiveRT = nullptr;
        }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth)
        {
            // Read-only depth pass: color stays COLOR_ATTACHMENT_OPTIMAL
            m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            // Depth stays READ_ONLY (render pass finalLayout preserves it)
            m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        else
        {
            // Main backbuffer (CLEAR or LOAD): finalLayout = PRESENT_SRC_KHR
            m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
    }

    VkResult endResult = vkEndCommandBuffer(cmd);
    m_vkRecording = false;
    if (endResult != VK_SUCCESS)
    {
        fprintf(stderr, "[Vulkan] vkEndCommandBuffer failed: %d\n", endResult);
        return; // Don't submit a broken command buffer
    }

    VkSemaphore waitSemaphores[] = { m_vkImageAvailableSemaphores[m_vkCurrentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_vkRenderFinishedSemaphores[m_vkCurrentFrame] };

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult = vkQueueSubmit(m_vkGraphicsQueue, 1, &submitInfo, m_vkInFlightFences[m_vkCurrentFrame]);
    if (submitResult != VK_SUCCESS)
        VkLog("[Vulkan] vkQueueSubmit FAILED: %d (frame %d)\n", submitResult, s_vkFrameNumber);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_vkSwapChain;
    presentInfo.pImageIndices = &m_vkImageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_vkPresentQueue, &presentInfo);
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
        VkLog("[Vulkan] vkQueuePresentKHR FAILED: %d (frame %d, imageIdx %u)\n",
              presentResult, s_vkFrameNumber, m_vkImageIndex);

    if (s_vkFrameNumber < 5)
        VkLog("[Vulkan] EndFrame #%d: submit=%d present=%d recording=%d inRP=%d imageIdx=%u\n",
              s_vkFrameNumber, submitResult, presentResult,
              (int)m_vkRecording, (int)m_vkInRenderPass, m_vkImageIndex);

    m_vkCurrentFrame = (m_vkCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Device::Resize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    m_width = width;
    m_height = height;

    vkDeviceWaitIdle(m_vkDevice);
    CleanupSwapChainVK();
    CreateSwapChainVK();
    CreateDepthResources();
    CreateFramebuffers();
}

// --- RHI draw commands ---

void Device::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
{
    if (!m_vkInRenderPass) return;
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    // Vulkan Y-axis is flipped vs D3D11. Use negative height + offset for compatibility.
    VkViewport vp = {};
    vp.x = x;
    vp.y = y + h;
    vp.width = w;
    vp.height = -h;
    vp.minDepth = minDepth;
    vp.maxDepth = maxDepth;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {};
    scissor.offset = { (int32_t)x, (int32_t)y };
    scissor.extent = { (uint32_t)w, (uint32_t)h };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Device::SetTopology(Topology topology)
{
    if (m_vkCurrentTopology != topology)
    {
        m_vkCurrentTopology = topology;
        m_vkStateDirty = true;
    }
}

void Device::BindCurrentPipeline()
{
    if (!m_vkPipelineManager || !m_vkCurrentShader)
        return;

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];

    // Bind pipeline if state changed
    if (m_vkStateDirty || m_vkBoundPipeline == VK_NULL_HANDLE)
    {
        VkPipeline pipeline = m_vkPipelineManager->GetOrCreatePipeline(
            m_vkDevice, m_vkCurrentShader, m_vkCurrentRaster,
            m_vkCurrentBlend, m_vkCurrentDepth, m_vkCurrentTopology,
            m_vkActiveRenderPass);

        if (pipeline != VK_NULL_HANDLE && pipeline != m_vkBoundPipeline)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            m_vkBoundPipeline = pipeline;
        }
        m_vkStateDirty = false;
    }

    // Don't bind descriptors if no pipeline is bound (shader compile failed, etc.)
    if (m_vkBoundPipeline == VK_NULL_HANDLE)
        return;

    // Allocate per-draw descriptor set
    VkDescriptorSet ds = m_vkPipelineManager->AllocateDescriptorSet(m_vkDevice);
    if (ds == VK_NULL_HANDLE)
        return;

    VkWriteDescriptorSet writes[16] = {};
    VkDescriptorBufferInfo bufInfos[3] = {};
    VkDescriptorImageInfo imgInfos[5] = {};
    uint32_t writeCount = 0;

    // Write constant buffers (bindings 0, 1, 2)
    for (uint32_t i = 0; i < 3; i++)
    {
        if (m_vkBoundCBs[i])
        {
            bufInfos[i].buffer = m_vkBoundCBs[i];
            bufInfos[i].offset = 0;
            bufInfos[i].range = m_vkBoundCBSizes[i];

            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstSet = ds;
            writes[writeCount].dstBinding = i;
            writes[writeCount].descriptorCount = 1;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[writeCount].pBufferInfo = &bufInfos[i];
            writeCount++;
        }
    }

    // Write ALL sampled image slots (bindings 10-14 = t0-t4)
    // Use default white texture for unbound slots. Slot 4 (binding 14 = shadowMap)
    // uses a depth-format default because the shader samples with a comparison sampler.
    VkDescriptorImageInfo samplerInfos[3] = {};
    for (uint32_t i = 0; i < 5; i++)
    {
        VkImageView view;
        VkImageLayout layout;

        if (m_vkBoundDepthSlot == i && m_vkBoundDepthView != VK_NULL_HANDLE)
        {
            // BindDepthTexturePS was called for this slot — use the device depth buffer
            view = m_vkBoundDepthView;
            layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        else if (m_vkBoundTextures[i] && m_vkBoundTextures[i]->m_vkImageView)
        {
            view = m_vkBoundTextures[i]->m_vkImageView;
            // Use the texture's actual layout for the descriptor write.
            // GENERAL is valid for sampling; SHADER_READ_ONLY_OPTIMAL is optimal.
            layout = m_vkBoundTextures[i]->m_vkLayout;
            if (layout == VK_IMAGE_LAYOUT_UNDEFINED)
                layout = VK_IMAGE_LAYOUT_GENERAL; // avoid UNDEFINED in descriptor
        }
        else if (i == 4)
        {
            // Shadow map slot — use depth default to satisfy comparison sampler
            view = m_vkDefaultDepthView;
            layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        else
        {
            view = m_vkDefaultImageView;
            layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        imgInfos[i].imageView = view;
        imgInfos[i].imageLayout = layout;

        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = ds;
        writes[writeCount].dstBinding = 10 + i;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[writeCount].pImageInfo = &imgInfos[i];
        writeCount++;
    }

    // Write ALL sampler slots (bindings 20-22 = s0-s2)
    for (uint32_t i = 0; i < 3; i++)
    {
        VkSampler sampler = m_vkDefaultSampler;
        if (m_vkBoundTextures[i] && m_vkBoundTextures[i]->m_vkSampler)
            sampler = m_vkBoundTextures[i]->m_vkSampler;

        samplerInfos[i].sampler = sampler;
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = ds;
        writes[writeCount].dstBinding = 20 + i;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[writeCount].pImageInfo = &samplerInfos[i];
        writeCount++;
    }

    // Write storage buffer at binding 30 if bound (GPU particle SSBO)
    VkDescriptorBufferInfo ssboBufInfo = {};
    if (m_vkBoundSSBO && writeCount < 16)
    {
        ssboBufInfo.buffer = m_vkBoundSSBO;
        ssboBufInfo.range = m_vkBoundSSBOSize;
        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = ds;
        writes[writeCount].dstBinding = 30;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[writeCount].pBufferInfo = &ssboBufInfo;
        writeCount++;
    }

    if (writeCount > 0)
        vkUpdateDescriptorSets(m_vkDevice, writeCount, writes, 0, nullptr);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_vkPipelineManager->GetPipelineLayout(), 0, 1, &ds, 0, nullptr);
}

void Device::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
    if (!m_vkInRenderPass) return;
    BindCurrentPipeline();
    if (m_vkBoundPipeline == VK_NULL_HANDLE) return;
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];

    vkCmdDrawIndexed(cmd, indexCount, 1, startIndex, baseVertex, 0);
    s_vkFrameDrawCount++;
}

void Device::Draw(uint32_t vertexCount, uint32_t startVertex)
{
    if (!m_vkInRenderPass) return;
    BindCurrentPipeline();
    if (m_vkBoundPipeline == VK_NULL_HANDLE) return;
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    vkCmdDraw(cmd, vertexCount, 1, startVertex, 0);
    s_vkFrameDrawCount++;
}

void Device::DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
    if (!m_vkInRenderPass) return;
    BindCurrentPipeline();
    if (m_vkBoundPipeline == VK_NULL_HANDLE) return;
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    vkCmdDraw(cmd, vertexCountPerInstance, instanceCount, startVertex, startInstance);
}

void Device::ClearInputLayout() { /* No-op in Vulkan — vertex input is part of pipeline */ }

// --- Render target management ---
// --- Render target management ---
// Off-screen rendering uses per-texture VkFramebuffer + VkRenderPass, created lazily.

static bool CreateOffscreenRT(VkDevice device, VkRenderPass& outRP, VkFramebuffer& outFB,
    VkImageView imageView, VkFormat format, uint32_t width, uint32_t height, bool isDepth);

void Device::SetRenderTarget(Texture& colorRT)
{
    if (!colorRT.m_vkImage || !m_vkRecording) return;

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];

    // End current render pass
    if (m_vkInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        m_vkInRenderPass = false;
        if (m_vkActiveRT) { m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; m_vkActiveRT = nullptr; }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth) { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; }
        else { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    }

    // Create RT resources lazily
    if (!CreateOffscreenRT(m_vkDevice, colorRT.m_vkRenderPass, colorRT.m_vkFramebuffer,
            colorRT.m_vkImageView, colorRT.m_vkFormat, colorRT.GetWidth(), colorRT.GetHeight(), false))
        return;

    // Transition texture to color attachment
    if (colorRT.m_vkLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        VkTransitionImageLayout(cmd, colorRT.m_vkImage,
            colorRT.m_vkLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        colorRT.m_vkLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Begin off-screen render pass
    VkClearValue clearVal = {};
    clearVal.color = {{ 0, 0, 0, 0 }};

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = colorRT.m_vkRenderPass;
    rpInfo.framebuffer = colorRT.m_vkFramebuffer;
    rpInfo.renderArea.extent = { colorRT.GetWidth(), colorRT.GetHeight() };
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_vkInRenderPass = true;
    m_vkActiveRenderPass = colorRT.m_vkRenderPass;
    m_vkActiveRT = &colorRT;
    SetViewport(0, 0, (float)colorRT.GetWidth(), (float)colorRT.GetHeight(), 0, 1);
}

void Device::SetDepthOnlyRenderTarget(Texture& depthRT)
{
    if (!depthRT.m_vkImage || !m_vkRecording) return;

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];

    if (m_vkInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        m_vkInRenderPass = false;
        if (m_vkActiveRT) { m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; m_vkActiveRT = nullptr; }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth) { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; }
        else { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    }

    if (!CreateOffscreenRT(m_vkDevice, depthRT.m_vkRenderPass, depthRT.m_vkFramebuffer,
            depthRT.m_vkImageView, depthRT.m_vkFormat, depthRT.GetWidth(), depthRT.GetHeight(), true))
        return;

    // Transition depth texture to depth attachment
    if (depthRT.m_vkLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        VkTransitionImageLayout(cmd, depthRT.m_vkImage,
            depthRT.m_vkLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT);
        depthRT.m_vkLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkClearValue clearVal = {};
    clearVal.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = depthRT.m_vkRenderPass;
    rpInfo.framebuffer = depthRT.m_vkFramebuffer;
    rpInfo.renderArea.extent = { depthRT.GetWidth(), depthRT.GetHeight() };
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_vkInRenderPass = true;
    m_vkActiveRenderPass = depthRT.m_vkRenderPass;
    m_vkActiveRT = &depthRT;
    SetViewport(0, 0, (float)depthRT.GetWidth(), (float)depthRT.GetHeight(), 0, 1);
}

// Lazily create VkRenderPass + VkFramebuffer for off-screen texture rendering.
// This is a free function but accesses Texture private members — it compiles because
// it's only called from Device methods which inline the access through friendship.
// We use a namespace-scope helper with the Texture members accessed via public getters
// or through VkDeviceAccess.
static bool CreateOffscreenRT(VkDevice device, VkRenderPass& outRP, VkFramebuffer& outFB,
    VkImageView imageView, VkFormat format, uint32_t width, uint32_t height, bool isDepth)
{
    if (outRP != VK_NULL_HANDLE && outFB != VK_NULL_HANDLE)
        return true;

    if (isDepth)
    {
        VkAttachmentDescription depthAtt = {};
        depthAtt.format = format;
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep = {};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &depthAtt;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &outRP) != VK_SUCCESS)
            return false;

        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = outRP;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &imageView;
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        return vkCreateFramebuffer(device, &fbInfo, nullptr, &outFB) == VK_SUCCESS;
    }
    else
    {
        VkAttachmentDescription colorAtt = {};
        colorAtt.format = format;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dep = {};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAtt;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &outRP) != VK_SUCCESS)
            return false;

        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = outRP;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &imageView;
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        return vkCreateFramebuffer(device, &fbInfo, nullptr, &outFB) == VK_SUCCESS;
    }
}

void Device::SetBackBuffer()
{
    if (!m_vkRecording) return;

    // End current off-screen render pass if active
    if (m_vkInRenderPass)
    {
        // If already in the main/load backbuffer pass, nothing to do
        if (m_vkActiveRenderPass == m_vkRenderPass || m_vkActiveRenderPass == m_vkRenderPassLoad)
            return;

        VkCommandBuffer ecmd = m_vkCommandBuffers[m_vkCurrentFrame];
        vkCmdEndRenderPass(ecmd);
        m_vkInRenderPass = false;
        if (m_vkActiveRT) { m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; m_vkActiveRT = nullptr; }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth) { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; }
        else { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    }

    // Re-begin the main render pass with LOAD to preserve existing content
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];

    // Ensure swapchain image is in COLOR_ATTACHMENT_OPTIMAL for the LOAD render pass.
    // After EndRenderPass, the actual layout depends on which render pass was active:
    // - Main pass finalLayout: PRESENT_SRC_KHR
    // - Read-only depth pass finalLayout: COLOR_ATTACHMENT_OPTIMAL
    // Our tracking may be wrong if EndFrame path set it incorrectly, so always transition
    // from whatever the actual state is. Use UNDEFINED to be safe — LOAD_OP_LOAD still
    // works because the GPU preserves content even with UNDEFINED→X transition when
    // we know the image contains valid data.
    // Actually, UNDEFINED discards. We need to know the real layout.
    // The safest approach: after read-only depth pass, color stays COLOR_ATTACHMENT_OPTIMAL.
    // After main pass, it goes to PRESENT_SRC_KHR.
    // If we just ended a render pass, check what we tracked.
    VkImage swapImg = m_vkSwapChainImages[m_vkImageIndex];
    if (m_vkSwapChainImageLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        VkTransitionImageLayout(cmd, swapImg,
            m_vkSwapChainImageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Ensure depth is in DEPTH_STENCIL_ATTACHMENT_OPTIMAL (may be in READ_ONLY after
    // SetBackBufferReadOnlyDepth). The LOAD render pass expects ATTACHMENT layout.
    if (m_vkDepthLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        VkTransitionImageLayout(cmd, m_vkDepthImage,
            m_vkDepthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkClearValue clearValues[2] = {};
    clearValues[0].color = {{ 0, 0, 0, 1 }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_vkRenderPassLoad;
    rpInfo.framebuffer = m_vkFramebuffers[m_vkImageIndex];
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = m_vkSwapChainExtent;
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_vkInRenderPass = true;
    m_vkActiveRenderPass = m_vkRenderPass;
    m_vkActiveRT = nullptr;
    m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Dynamic viewport/scissor MUST be set in every render pass instance
    SetViewport(0, 0, (float)m_vkSwapChainExtent.width, (float)m_vkSwapChainExtent.height, 0, 1);
}

void Device::SetBackBufferReadOnlyDepth()
{
    if (!m_vkRecording) return;

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];

    // End current render pass if active
    if (m_vkInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        m_vkInRenderPass = false;
        if (m_vkActiveRT) { m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; m_vkActiveRT = nullptr; }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth) { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; }
        else { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    }

    // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL (render pass expects it)
    VkImage swapImg = m_vkSwapChainImages[m_vkImageIndex];
    if (m_vkSwapChainImageLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        VkTransitionImageLayout(cmd, swapImg,
            m_vkSwapChainImageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Transition depth to read-only for sampling in PS
    if (m_vkDepthLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    {
        VkTransitionImageLayout(cmd, m_vkDepthImage,
            m_vkDepthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }

    // Begin render pass with read-only depth
    VkClearValue clearValues[2] = {};
    clearValues[0].color = {{ 0, 0, 0, 1 }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_vkRenderPassReadOnlyDepth;
    rpInfo.framebuffer = m_vkFramebuffersReadOnlyDepth[m_vkImageIndex];
    rpInfo.renderArea.extent = m_vkSwapChainExtent;
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_vkInRenderPass = true;
    m_vkActiveRenderPass = m_vkRenderPassReadOnlyDepth;
    m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    SetViewport(0, 0, (float)m_vkSwapChainExtent.width, (float)m_vkSwapChainExtent.height, 0, 1);
}

void Device::ClearRenderTarget(Texture& rt, float r, float g, float b, float a)
{
    if (!m_vkInRenderPass) return;
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    VkClearAttachment clearAtt = {};
    clearAtt.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearAtt.colorAttachment = 0;
    clearAtt.clearValue.color = {{ r, g, b, a }};

    VkClearRect clearRect = {};
    // Use the RT's dimensions if off-screen, otherwise swapchain extent
    if (m_vkActiveRT)
        clearRect.rect.extent = { m_vkActiveRT->GetWidth(), m_vkActiveRT->GetHeight() };
    else
        clearRect.rect.extent = m_vkSwapChainExtent;
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    vkCmdClearAttachments(cmd, 1, &clearAtt, 1, &clearRect);
}

void Device::ClearDepthStencil(Texture& /*depthRT*/)
{
    if (!m_vkInRenderPass) return; // Can't clear outside render pass
    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    VkClearAttachment clearAtt = {};
    clearAtt.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    clearAtt.clearValue.depthStencil = { 1.0f, 0 };

    VkClearRect clearRect = {};
    clearRect.rect.extent = m_vkSwapChainExtent;
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    vkCmdClearAttachments(cmd, 1, &clearAtt, 1, &clearRect);
}

// --- Texture copies ---
// These require ending the render pass, performing the copy with proper layout
// transitions, then re-entering the render pass.

void Device::CopyBackBufferToTexture(Texture& dst)
{
    if (!dst.m_vkImage || m_vkSwapChainImages.empty() || !m_vkRecording) return;

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    VkImage srcImage = m_vkSwapChainImages[m_vkImageIndex];

    // End current render pass to perform transfer operations
    if (m_vkInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        m_vkInRenderPass = false;
        if (m_vkActiveRT) { m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; m_vkActiveRT = nullptr; }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth) { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; }
        else { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    }

    // Transition swapchain image from its actual current layout to TRANSFER_SRC
    VkTransitionImageLayout(cmd, srcImage,
        m_vkSwapChainImageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkTransitionImageLayout(cmd, dst.m_vkImage,
        dst.m_vkLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    uint32_t copyW = std::min(m_vkSwapChainExtent.width, dst.GetWidth());
    uint32_t copyH = std::min(m_vkSwapChainExtent.height, dst.GetHeight());
    VkCopyImageToImage(cmd, srcImage, dst.m_vkImage, copyW, copyH);

    // Transition back — swapchain goes to COLOR_ATTACHMENT for re-entering render pass
    VkTransitionImageLayout(cmd, srcImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkTransitionImageLayout(cmd, dst.m_vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dst.m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    SetBackBuffer();
}

void Device::CopyTextureToBackBuffer(Texture& src)
{
    if (!src.m_vkImage || m_vkSwapChainImages.empty() || !m_vkRecording) return;

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    VkImage dstImage = m_vkSwapChainImages[m_vkImageIndex];

    if (m_vkInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        m_vkInRenderPass = false;
        if (m_vkActiveRT) { m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; m_vkActiveRT = nullptr; }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth) { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; }
        else { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    }

    VkTransitionImageLayout(cmd, src.m_vkImage,
        src.m_vkLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkTransitionImageLayout(cmd, dstImage,
        m_vkSwapChainImageLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    uint32_t copyW = std::min(m_vkSwapChainExtent.width, src.GetWidth());
    uint32_t copyH = std::min(m_vkSwapChainExtent.height, src.GetHeight());
    VkCopyImageToImage(cmd, src.m_vkImage, dstImage, copyW, copyH);

    VkTransitionImageLayout(cmd, src.m_vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    src.m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkTransitionImageLayout(cmd, dstImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    SetBackBuffer();
}

void Device::CopyTexture(Texture& dst, Texture& src)
{
    if (!dst.m_vkImage || !src.m_vkImage || !m_vkRecording) return;

    VkCommandBuffer cmd = m_vkCommandBuffers[m_vkCurrentFrame];
    if (m_vkInRenderPass)
    {
        vkCmdEndRenderPass(cmd);
        m_vkInRenderPass = false;
        if (m_vkActiveRT) { m_vkActiveRT->m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; m_vkActiveRT = nullptr; }
        else if (m_vkActiveRenderPass == m_vkRenderPassReadOnlyDepth) { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; }
        else { m_vkSwapChainImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; m_vkDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    }

    VkTransitionImageLayout(cmd, src.m_vkImage,
        src.m_vkLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkTransitionImageLayout(cmd, dst.m_vkImage,
        dst.m_vkLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    uint32_t copyW = std::min(src.GetWidth(), dst.GetWidth());
    uint32_t copyH = std::min(src.GetHeight(), dst.GetHeight());
    VkCopyImageToImage(cmd, src.m_vkImage, dst.m_vkImage, copyW, copyH);

    VkTransitionImageLayout(cmd, src.m_vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    src.m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkTransitionImageLayout(cmd, dst.m_vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dst.m_vkLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    SetBackBuffer();
}

// --- Resource binding ---
// In Vulkan, resources are bound via descriptor sets, not individual slot bindings.
// The Unbind* methods are no-ops since Vulkan descriptors are set-based.
// BindPSTextures and BindDepthTexturePS will update descriptor sets once the
// descriptor pool/layout infrastructure is built in ShaderVK.cpp.

void Device::UnbindPSSRVs(uint32_t startSlot, uint32_t count)
{
    for (uint32_t i = 0; i < count && (startSlot + i) < 5; i++)
        m_vkBoundTextures[startSlot + i] = nullptr;
}

void Device::UnbindVSSRVs(uint32_t startSlot, uint32_t count)
{
    // VS textures share the same tracking array
    for (uint32_t i = 0; i < count && (startSlot + i) < 5; i++)
        m_vkBoundTextures[startSlot + i] = nullptr;
}

void Device::UnbindVSSamplers(uint32_t /*startSlot*/, uint32_t /*count*/) { /* Samplers in combined image sampler */ }
void Device::UnbindVSConstantBuffers(uint32_t startSlot, uint32_t count)
{
    for (uint32_t i = 0; i < count && (startSlot + i) < 3; i++)
    {
        m_vkBoundCBs[startSlot + i] = VK_NULL_HANDLE;
        m_vkBoundCBSizes[startSlot + i] = 0;
    }
}

void Device::BindPSTextures(uint32_t startSlot, const Texture* const* textures, uint32_t count)
{
    // Record texture bindings — descriptor set will be written at draw time
    for (uint32_t i = 0; i < count && (startSlot + i) < 5; i++)
        m_vkBoundTextures[startSlot + i] = textures[i];
}

void Device::BindDepthTexturePS(uint32_t slot)
{
    // Bind the main depth buffer as a sampled image for water shore foam.
    // We store the depth view + slot, and BindCurrentPipeline will use it
    // in the descriptor write instead of the default texture at this slot.
    m_vkBoundDepthView = m_vkDepthSRView;
    m_vkBoundDepthSlot = slot;
}

bool Device::CaptureScreenshot(const char* filename)
{
    if (m_vkSwapChainImages.empty()) return false;

    // Wait for all GPU work to complete
    vkDeviceWaitIdle(m_vkDevice);

    uint32_t w = m_vkSwapChainExtent.width;
    uint32_t h = m_vkSwapChainExtent.height;
    VkDeviceSize bufSize = (VkDeviceSize)w * h * 4;

    // Create staging buffer using raw allocation (not VMA)
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    if (!VkCreateBuffer(m_vkDevice, m_vkPhysicalDevice, bufSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging, stagingMem))
        return false;

    // Copy swapchain image to staging buffer.
    // The image is in PRESENT_SRC_KHR after EndFrame.
    VkCommandBuffer cmd = VkBeginSingleTimeCommands(m_vkDevice, m_vkCommandPool);
    VkImage srcImage = m_vkSwapChainImages[m_vkImageIndex];

    VkTransitionImageLayout(cmd, srcImage,
        m_vkSwapChainImageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { w, h, 1 };
    vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);

    VkTransitionImageLayout(cmd, srcImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_vkSwapChainImageLayout);

    VkEndSingleTimeCommands(m_vkDevice, m_vkCommandPool, m_vkGraphicsQueue, cmd);

    // Read pixels and write BMP
    void* mapped;
    vkMapMemory(m_vkDevice, stagingMem, 0, bufSize, 0, &mapped);

    int rowBytes = w * 3;
    int padBytes = (4 - (rowBytes % 4)) % 4;
    int stride = rowBytes + padBytes;
    int imageSize = stride * h;
    int fileSize = 54 + imageSize;

    FILE* f = fopen(filename, "wb");
    if (!f) { vkUnmapMemory(m_vkDevice, stagingMem); vkDestroyBuffer(m_vkDevice, staging, nullptr); vkFreeMemory(m_vkDevice, stagingMem, nullptr); return false; }

    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(int*)(hdr + 2) = fileSize;
    *(int*)(hdr + 10) = 54;
    *(int*)(hdr + 14) = 40;
    *(int*)(hdr + 18) = w;
    *(int*)(hdr + 22) = -(int)h;
    *(short*)(hdr + 26) = 1;
    *(short*)(hdr + 28) = 24;
    *(int*)(hdr + 34) = imageSize;
    fwrite(hdr, 1, 54, f);

    // BGRA -> BGR (Vulkan swapchain is B8G8R8A8)
    std::vector<uint8_t> row(stride, 0);
    const uint8_t* src = (const uint8_t*)mapped;
    for (uint32_t y = 0; y < h; ++y)
    {
        const uint32_t* srcRow = (const uint32_t*)(src + y * w * 4);
        for (uint32_t x = 0; x < w; ++x)
        {
            uint32_t px = srcRow[x];
            row[x * 3 + 0] = (px >> 0) & 0xFF;  // B
            row[x * 3 + 1] = (px >> 8) & 0xFF;  // G
            row[x * 3 + 2] = (px >> 16) & 0xFF; // R
        }
        fwrite(row.data(), 1, stride, f);
    }
    fclose(f);

    vkUnmapMemory(m_vkDevice, stagingMem);
    vkDestroyBuffer(m_vkDevice, staging, nullptr);
    vkFreeMemory(m_vkDevice, stagingMem, nullptr);
    return true;
}

} // namespace Render

// Helper to get SDL window for Vulkan surface creation
#include "SDLPlatform.h"
SDL_Window* GetSDLWindowForVulkan()
{
    return Platform::SDLPlatform::Instance().GetWindow();
}

#endif // BUILD_WITH_VULKAN
