#include <vulkan/vulkan.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
enum class ProbeStatus { Ready, LoadFailed, MissingEntryPoint, MissingSurface };

[[nodiscard]] ProbeStatus probe(const char* path) {
    HMODULE library = LoadLibraryA(path);
    if (!library) {
        std::fprintf(stderr, "LoadLibrary(%s): Windows error %lu\n", path, GetLastError());
        return ProbeStatus::LoadFailed;
    }
    char loaded[MAX_PATH]{};
    GetModuleFileNameA(library, loaded, MAX_PATH);
    std::printf("Vulkan module: %s\n", loaded);
    auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        GetProcAddress(library, "vkEnumerateInstanceExtensionProperties"));
    if (!enumerate) {
        std::fprintf(stderr, "vkEnumerateInstanceExtensionProperties missing\n");
        FreeLibrary(library);
        return ProbeStatus::MissingEntryPoint;
    }
    uint32_t                           count  = 0;
    VkResult                           result = enumerate(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    if (result == VK_SUCCESS) result = enumerate(nullptr, &count, extensions.data());
    bool surface = false;
    bool win32   = false;
    for (const auto& extension : extensions) {
        std::printf("  %s\n", extension.extensionName);
        surface |= std::strcmp(extension.extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
        win32 |= std::strcmp(extension.extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME) == 0;
    }
    std::printf("enumerate result=%d, extensions=%u, surface=%d, win32=%d\n", result, count, surface, win32);
    FreeLibrary(library);
    return result == VK_SUCCESS && surface && win32 ? ProbeStatus::Ready : ProbeStatus::MissingSurface;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const auto driver = probe(argv[1]);
    const auto loader = probe(argv[2]);
    return driver == ProbeStatus::Ready && loader == ProbeStatus::Ready ? 0 : 1;
}
