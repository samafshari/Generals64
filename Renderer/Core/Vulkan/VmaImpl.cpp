#ifdef BUILD_WITH_VULKAN

// VMA implementation — must be included in exactly one .cpp file
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4189) // local variable is initialized but not referenced
#pragma warning(disable : 4127) // conditional expression is constant
#pragma warning(disable : 4100) // unreferenced formal parameter
#endif

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // BUILD_WITH_VULKAN
