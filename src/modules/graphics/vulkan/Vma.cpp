#if defined(VKB_ENABLE_VMA)

// 这个翻译单元是 VMA 实现的唯一宿主（EVBackends 组）。gpgpu（EVWorld 组）直接调用
// vmaCreateBuffer / vmaDestroyBuffer / vmaMapMemory / vmaUnmapMemory 这些 C 入口，
// 而 VMA 是非类 C API，拿不到 EVENGINE_API_<GROUP> 那种类级导出标注：只有把 VMA
// 自己的 VMA_CALL_PRE 钩子定义成 dllexport，这些入口才会进入 EVBackends.dll 的
// 导出表（否则 EVWorld 链接期报 LNK2019）。非 SHARED 模式未定义
// EVENGINE_EXPORTS_BACKENDS，保持原样。
#if defined(EVENGINE_EXPORTS_BACKENDS) && defined(_WIN32)
#define VMA_CALL_PRE __declspec(dllexport)
#endif

#define VMA_IMPLEMENTATION
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#elif __has_include(<vulkan/vk_mem_alloc.h>)
#include <vulkan/vk_mem_alloc.h>
#else
#error "EVENGINE_VULKAN_USE_VMA requires the bundled vk_mem_alloc.h"
#endif

#endif
