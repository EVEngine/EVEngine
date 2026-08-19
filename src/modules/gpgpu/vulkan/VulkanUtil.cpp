#include "gpgpu/vulkan/VulkanUtil.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#if defined(EVE_HAS_SHADERC)
#include <shaderc/shaderc.h>
#endif
#else
#include <unistd.h>
#endif

namespace eve::gpgpu {
namespace {

std::vector<uint32_t> loadSpirvBytes(const void *data, size_t size) {
    if (!data || size < 4 || (size % 4) != 0)
        throw Exception("Gpgpu SPIR-V: invalid size %zu", size);
    const auto *words = static_cast<const uint32_t *>(data);
    if (words[0] != 0x07230203)
        throw Exception("Gpgpu SPIR-V: bad magic (expected 0x07230203)");
    return std::vector<uint32_t>(words, words + size / 4);
}

}  // namespace

graphics::vulkan::Graphics *requireVulkanGraphics() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    auto *vkg = dynamic_cast<graphics::vulkan::Graphics *>(gfx);
    if (!vkg) throw Exception("Gpgpu: requires Vulkan Graphics backend");
    if (!static_cast<VkDevice>(vkg->getDevice().instance))
        throw Exception("Gpgpu: Graphics device not initialized (create a window first)");
    return vkg;
}

bool vulkanGraphicsReady() {
    try {
        auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        if (!gfx) return false;
        auto *vkg = dynamic_cast<graphics::vulkan::Graphics *>(gfx);
        if (!vkg) return false;
        return static_cast<VkDevice>(vkg->getDevice().instance) != VK_NULL_HANDLE;
    } catch (...) {
        return false;
    }
}

vk::Queue computeQueue(graphics::vulkan::Graphics *vkg) {
    // Must match computeCommandPool (uploadPool is graphics-family). Only use a
    // dedicated compute queue when it shares that family; otherwise submit on graphics.
    auto &device = vkg->getDevice();
    const uint32_t graphicsFamily = device.get_queue_index(vkb::QueueType::graphics);
    vk::Queue compute = device.getQueue(vkb::QueueType::compute);
    if (compute && device.get_queue_index(vkb::QueueType::compute) == graphicsFamily)
        return compute;
    return device.getQueue(vkb::QueueType::graphics);
}

vk::CommandPool computeCommandPool(graphics::vulkan::Graphics *vkg) {
    return vkg->getUploadPool();
}

std::vector<uint32_t> loadSpirvFile(const std::string &path) {
    auto *fs = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> fd(fs->read(path));
    if (!fd) throw Exception("Gpgpu.newShaderFromSpvFile: failed to read '%s'", path.c_str());
    return loadSpirvBytes(fd->getData(), fd->getSize());
}

#if defined(_WIN32)
namespace {

#if defined(EVE_HAS_SHADERC)
/** In-process GLSL -> SPIR-V via the Vulkan SDK's static shaderc library. */
std::vector<uint32_t> compileComputeGlslInProcess(const std::string &glsl) {
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    if (!compiler) throw Exception("Gpgpu.newShader: shaderc_compiler_initialize failed");
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    if (!options) {
        shaderc_compiler_release(compiler);
        throw Exception("Gpgpu.newShader: shaderc_compile_options_initialize failed");
    }
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_target_spirv(options, shaderc_spirv_version_1_0);
    shaderc_compilation_result_t result =
        shaderc_compile_into_spv(compiler, glsl.data(), glsl.size(),
                                 shaderc_glsl_compute_shader, "eve_compute.comp", "main",
                                 options);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        const std::string err = shaderc_result_get_error_message(result);
        shaderc_result_release(result);
        throw Exception("Gpgpu.newShader: shaderc failed:\n%s", err.c_str());
    }
    const size_t len  = shaderc_result_get_length(result);
    const char *bytes = shaderc_result_get_bytes(result);
    if (len == 0 || len % 4 != 0) {
        shaderc_result_release(result);
        throw Exception("Gpgpu.newShader: shaderc returned invalid SPIR-V");
    }
    std::vector<uint32_t> spv(len / 4);
    std::memcpy(spv.data(), bytes, len);
    shaderc_result_release(result);
    return spv;
}
#endif  // EVE_HAS_SHADERC

/** Locate a usable glslc.exe: VULKAN_SDK, common install roots, then PATH. */
std::string findGlslc() {
    if (const char *sdk = std::getenv("VULKAN_SDK"); sdk && *sdk) {
        for (const char *sub : {"\\bin\\glslc.exe", "\\Bin\\glslc.exe"}) {
            std::string p = std::string(sdk) + sub;
            if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
        }
    }
    // C:\VulkanSDK\<version>\Bin\glslc.exe
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA("C:\\VulkanSDK\\*", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::string p = std::string("C:\\VulkanSDK\\") + fd.cFileName + "\\Bin\\glslc.exe";
                if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    FindClose(h);
                    return p;
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    FILE *pipe = _popen("where glslc 2>nul", "r");
    if (pipe) {
        char buf[512];
        std::string result;
        while (fgets(buf, sizeof(buf), pipe)) result += buf;
        int status = _pclose(pipe);
        if (status == 0 && !result.empty()) {
            while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
                result.pop_back();
            return result;
        }
    }
    return {};
}

}  // namespace
#endif

std::vector<uint32_t> compileComputeGlsl(const std::string &glsl) {
    if (glsl.empty()) throw Exception("Gpgpu.newShader: empty GLSL");
#if defined(_WIN32)
#if defined(EVE_HAS_SHADERC)
    try {
        return compileComputeGlslInProcess(glsl);
    } catch (...) {
        // Fall through to spawning glslc (SDK lib present but compile failed).
    }
#endif
    const std::string glslc = findGlslc();
    if (glslc.empty())
        throw Exception("Gpgpu.newShader: glslc not found on Windows "
                        "(install the Vulkan SDK or set VULKAN_SDK)");

    char tmpDir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmpDir) == 0)
        throw Exception("Gpgpu.newShader: GetTempPath failed");
    char inPath[MAX_PATH];
    if (GetTempFileNameA(tmpDir, "eve", 0, inPath) == 0)
        throw Exception("Gpgpu.newShader: GetTempFileName failed");
    std::string outPath = std::string(inPath) + ".spv";

    {
        FILE *f = nullptr;
        if (fopen_s(&f, inPath, "wb") != 0 || !f) {
            DeleteFileA(inPath);
            throw Exception("Gpgpu.newShader: failed to write temp GLSL");
        }
        fwrite(glsl.data(), 1, glsl.size(), f);
        fclose(f);
    }

    // Spawn glslc directly with CreateProcess: _popen routes through cmd.exe
    // /c whose quote-stripping rules mangle commands starting with a quoted
    // program path. Capture stderr/stdout into a sidecar file.
    const std::string errPath = outPath + ".err";
    std::string cmd = "\"" + glslc + "\" -fshader-stage=comp \"" + inPath + "\" -o \"" +
                      outPath + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE errFile = CreateFileA(errPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (errFile != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = errFile;
        si.hStdError = errFile;
    }
    char *mutableCmd = _strdup(cmd.c_str());
    const BOOL spawned =
        CreateProcessA(glslc.c_str(), mutableCmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &si, &pi);
    free(mutableCmd);
    if (errFile != INVALID_HANDLE_VALUE) CloseHandle(errFile);
    if (!spawned) {
        const DWORD errCode = GetLastError();
        DeleteFileA(inPath);
        DeleteFileA(outPath.c_str());
        DeleteFileA(errPath.c_str());
        throw Exception("Gpgpu.newShader: failed to launch glslc (error %lu)", errCode);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DeleteFileA(inPath);
    if (exitCode != 0) {
        std::string err;
        FILE *ef = nullptr;
        if (fopen_s(&ef, errPath.c_str(), "rb") == 0 && ef) {
            char buf[512];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf) - 1, ef)) > 0) {
                buf[n] = '\0';
                err += buf;
            }
            fclose(ef);
        }
        DeleteFileA(outPath.c_str());
        DeleteFileA(errPath.c_str());
        throw Exception("Gpgpu.newShader: glslc failed (exit %lu):\n%s", exitCode, err.c_str());
    }
    DeleteFileA(errPath.c_str());

    FILE *f = nullptr;
    if (fopen_s(&f, outPath.c_str(), "rb") != 0 || !f) {
        DeleteFileA(outPath.c_str());
        throw Exception("Gpgpu.newShader: failed to open compiled SPIR-V");
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz > 0 ? sz : 0));
    if (sz > 0 && fread(bytes.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
        fclose(f);
        DeleteFileA(outPath.c_str());
        throw Exception("Gpgpu.newShader: failed to read compiled SPIR-V");
    }
    fclose(f);
    DeleteFileA(outPath.c_str());
    return loadSpirvBytes(bytes.data(), bytes.size());
#else
    char inPath[] = "/tmp/eve_comp_XXXXXX";
    int fd = mkstemp(inPath);
    if (fd < 0) throw Exception("Gpgpu.newShader: mkstemp failed");
    std::string outPath = std::string(inPath) + ".spv";
    {
        ssize_t n = write(fd, glsl.data(), glsl.size());
        close(fd);
        if (n < 0 || size_t(n) != glsl.size()) {
            unlink(inPath);
            throw Exception("Gpgpu.newShader: failed to write temp GLSL");
        }
    }

    std::string cmd =
        std::string("glslc -fshader-stage=comp \"") + inPath + "\" -o \"" + outPath + "\" 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    std::string err;
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) err += buf;
        int status = pclose(pipe);
        unlink(inPath);
        if (status != 0) {
            unlink(outPath.c_str());
            throw Exception("Gpgpu.newShader: glslc failed:\n%s", err.c_str());
        }
    } else {
        unlink(inPath);
        throw Exception("Gpgpu.newShader: glslc not available (popen failed)");
    }

    FILE *f = fopen(outPath.c_str(), "rb");
    if (!f) {
        unlink(outPath.c_str());
        throw Exception("Gpgpu.newShader: failed to open compiled SPIR-V");
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz > 0 ? sz : 0));
    if (sz > 0 && fread(bytes.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
        fclose(f);
        unlink(outPath.c_str());
        throw Exception("Gpgpu.newShader: failed to read compiled SPIR-V");
    }
    fclose(f);
    unlink(outPath.c_str());
    return loadSpirvBytes(bytes.data(), bytes.size());
#endif
}

}  // namespace eve::gpgpu
