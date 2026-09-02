#if defined(VKB_ENABLE_VMA)

#define VMA_IMPLEMENTATION
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#elif __has_include(<vulkan/vk_mem_alloc.h>)
#include <vulkan/vk_mem_alloc.h>
#else
#error "EVENGINE_VULKAN_USE_VMA requires the bundled vk_mem_alloc.h"
#endif

#endif
