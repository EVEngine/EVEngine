#include "graphics/vulkan/GlslCompiler.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#if defined(EVE_HAS_SHADERC)
#include <shaderc/shaderc.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace eve::graphics {
namespace {

/** First word of every SPIR-V module. */
constexpr std::uint32_t kSpirvMagic = 0x07230203u;

std::runtime_error compileFailure(const std::string &message) { return std::runtime_error(message); }

/** Validate a compiler's output and copy it into SPIR-V words. */
std::vector<std::uint32_t> spirvWords(const void *data, std::size_t size) {
    if (!data || size < 4 || (size % 4) != 0)
        throw compileFailure("compiled SPIR-V has an invalid size");
    const auto *words = static_cast<const std::uint32_t *>(data);
    if (words[0] != kSpirvMagic) throw compileFailure("compiled SPIR-V has a bad magic number");
    return std::vector<std::uint32_t>(words, words + size / 4);
}

/** Human-readable stage name used in compiler diagnostics. */
const char *stageName(GlslStage stage) {
    switch (stage) {
        case GlslStage::eVertex: return "vertex";
        case GlslStage::eFragment: return "fragment";
        case GlslStage::eCompute: return "compute";
    }
    return "shader";
}

#if !defined(EVE_HAS_SHADERC)

/** `glslc -fshader-stage=` argument for a stage. */
const char *stageArgument(GlslStage stage) {
    switch (stage) {
        case GlslStage::eVertex: return "vert";
        case GlslStage::eFragment: return "frag";
        case GlslStage::eCompute: return "comp";
    }
    return "frag";
}

/** Read a compiled SPIR-V file and remove it. */
std::vector<std::uint32_t> readCompiledFile(const std::string &path, bool remove) {
    FILE *file = nullptr;
#if defined(_WIN32)
    if (fopen_s(&file, path.c_str(), "rb") != 0) file = nullptr;
#else
    file = fopen(path.c_str(), "rb");
#endif
    if (!file) {
        if (remove) std::remove(path.c_str());
        throw compileFailure("failed to open compiled SPIR-V");
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size > 0 ? size : 0));
    const bool shortRead =
        size > 0 && std::fread(bytes.data(), 1, static_cast<std::size_t>(size), file) != static_cast<std::size_t>(size);
    std::fclose(file);
    if (remove) std::remove(path.c_str());
    if (shortRead) throw compileFailure("failed to read compiled SPIR-V");
    return spirvWords(bytes.data(), bytes.size());
}

#if defined(_WIN32)

/** Locate a usable glslc.exe: VULKAN_SDK, common install roots, then PATH. */
std::string findGlslc() {
    if (const char *sdk = std::getenv("VULKAN_SDK"); sdk && *sdk) {
        for (const char *sub : {"\\bin\\glslc.exe", "\\Bin\\glslc.exe"}) {
            std::string path = std::string(sdk) + sub;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
        }
    }
    // C:\VulkanSDK\<version>\Bin\glslc.exe
    WIN32_FIND_DATAA entry{};
    HANDLE           find = FindFirstFileA("C:\\VulkanSDK\\*", &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::string path = std::string("C:\\VulkanSDK\\") + entry.cFileName + "\\Bin\\glslc.exe";
                if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    FindClose(find);
                    return path;
                }
            }
        } while (FindNextFileA(find, &entry));
        FindClose(find);
    }
    FILE *pipe = _popen("where glslc 2>nul", "r");
    if (pipe) {
        char        buffer[512];
        std::string found;
        while (fgets(buffer, sizeof(buffer), pipe)) found += buffer;
        const int status = _pclose(pipe);
        if (status == 0 && !found.empty()) {
            while (!found.empty() && (found.back() == '\r' || found.back() == '\n')) found.pop_back();
            return found;
        }
    }
    return {};
}

/**
 * Spawn the external glslc for one stage.
 *
 * CreateProcess is used instead of _popen: the latter routes through cmd.exe /c,
 * whose quote stripping mangles a command that starts with a quoted program path,
 * and its diagnostics have to be captured into a sidecar file instead of a pipe.
 */
std::vector<std::uint32_t> compileWithExternalGlslc(const std::string &source, GlslStage stage,
                                                   const std::string &debugName) {
    const std::string glslc = findGlslc();
    if (glslc.empty())
        throw compileFailure(
            "no GLSL compiler available: this build has no in-process shaderc and no "
            "glslc.exe was found (install the Vulkan SDK or set VULKAN_SDK)");

    char tempDir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempDir) == 0) throw compileFailure("GetTempPath failed");
    char inputPath[MAX_PATH];
    if (GetTempFileNameA(tempDir, "eve", 0, inputPath) == 0) throw compileFailure("GetTempFileName failed");
    const std::string outputPath = std::string(inputPath) + ".spv";
    const std::string errorPath  = outputPath + ".err";
    struct TemporaryFiles {
        const char        *input;
        const std::string &output;
        const std::string &error;
        ~TemporaryFiles() {
            DeleteFileA(error.c_str());
            DeleteFileA(output.c_str());
            // Reserve the unique basename until both sidecars are consumed.
            DeleteFileA(input);
        }
    } temporary{inputPath, outputPath, errorPath};

    {
        FILE *file = nullptr;
        if (fopen_s(&file, inputPath, "wb") != 0 || !file) throw compileFailure("failed to write temp GLSL");
        fwrite(source.data(), 1, source.size(), file);
        fclose(file);
    }

    std::string command = "\"" + glslc + "\" -fshader-stage=" + stageArgument(stage) + " \"" + inputPath +
                          "\" -o \"" + outputPath + "\"";
    STARTUPINFOEXA        startup{};
    startup.StartupInfo.cb = sizeof(startup);
    PROCESS_INFORMATION   process{};
    SECURITY_ATTRIBUTES   security{};
    security.nLength        = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE diagnostics =
        CreateFileA(errorPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, &security, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input =
        CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (diagnostics == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (diagnostics != INVALID_HANDLE_VALUE) CloseHandle(diagnostics);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        throw compileFailure("failed to create compiler standard handles");
    }
    struct StandardHandles {
        HANDLE output, input;
        ~StandardHandles() {
            CloseHandle(output);
            CloseHandle(input);
        }
    } handles{diagnostics, input};
    // Without an explicit list, concurrent CreateProcess calls inherit other
    // jobs' temporary-file handles and can prevent glslc from opening its input.
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<std::uint8_t> attributes(attributeBytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeBytes))
        throw compileFailure("failed to initialize process attributes");
    struct AttributeList {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeList() { DeleteProcThreadAttributeList(value); }
    } attributeList{startup.lpAttributeList};
    HANDLE inherited[] = {diagnostics, input};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr))
        throw compileFailure("failed to restrict compiler handle inheritance");
    startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput  = input;
    startup.StartupInfo.hStdOutput = diagnostics;
    startup.StartupInfo.hStdError  = diagnostics;
    if (!CreateProcessA(glslc.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo,
                        &process)) {
        throw compileFailure("failed to launch glslc (error " + std::to_string(GetLastError()) + ")");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    if (exitCode != 0) {
        std::string errors;
        HANDLE      reader =
            CreateFileA(errorPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (reader != INVALID_HANDLE_VALUE) {
            char  buffer[512];
            DWORD count = 0;
            while (ReadFile(reader, buffer, sizeof(buffer), &count, nullptr) && count)
                errors.append(buffer, count);
            CloseHandle(reader);
        }
        throw compileFailure("glslc failed for " + debugName + " (exit " + std::to_string(exitCode) +
                               "):\n" + errors);
    }
    return readCompiledFile(outputPath, false);
}

#else  // !_WIN32

/** Compile by spawning `glslc` from PATH through a pipe. */
std::vector<std::uint32_t> compileWithExternalGlslc(const std::string &source, GlslStage stage,
                                                   const std::string &debugName) {
    char inputPath[] = "/tmp/eve_shader_XXXXXX";
    const int descriptor = mkstemp(inputPath);
    if (descriptor < 0) throw compileFailure("mkstemp failed");
    const std::string outputPath = std::string(inputPath) + ".spv";
    {
        const ssize_t written = write(descriptor, source.data(), source.size());
        close(descriptor);
        if (written < 0 || static_cast<std::size_t>(written) != source.size()) {
            unlink(inputPath);
            throw compileFailure("failed to write temp GLSL");
        }
    }

    const std::string command = std::string("glslc -fshader-stage=") + stageArgument(stage) + " \"" + inputPath +
                                "\" -o \"" + outputPath + "\" 2>&1";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        unlink(inputPath);
        throw compileFailure("glslc not available (popen failed)");
    }
    char        buffer[256];
    std::string errors;
    while (fgets(buffer, sizeof(buffer), pipe)) errors += buffer;
    const int status = pclose(pipe);
    unlink(inputPath);
    if (status != 0) {
        unlink(outputPath.c_str());
        throw compileFailure("glslc failed for " + debugName + ":\n" + errors);
    }
    return readCompiledFile(outputPath, true);
}

/** @return True when `glslc` is reachable on PATH. */
bool externalGlslcAvailable() {
    FILE *pipe = popen("command -v glslc >/dev/null 2>&1", "r");
    if (!pipe) return false;
    return pclose(pipe) == 0;
}

#endif  // _WIN32

#endif  // !EVE_HAS_SHADERC

#if defined(EVE_HAS_SHADERC)
std::vector<std::uint32_t> compileInProcess(const std::string &source, GlslStage stage, const std::string &debugName) {
    struct CompilerContext {
        shaderc_compiler_t        compiler = shaderc_compiler_initialize();
        shaderc_compile_options_t options  = nullptr;
        CompilerContext() {
            if (!compiler) throw compileFailure("shaderc_compiler_initialize failed");
            options = shaderc_compile_options_initialize();
            if (!options) {
                shaderc_compiler_release(compiler);
                throw compileFailure("shaderc_compile_options_initialize failed");
            }
            shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
            shaderc_compile_options_set_target_spirv(options, shaderc_spirv_version_1_0);
        }
        ~CompilerContext() {
            shaderc_compile_options_release(options);
            shaderc_compiler_release(compiler);
        }
    };
    // Keep glslang's compiler/built-in state alive across jobs and graph segments.
    // Each CPU thread owns its context; no device references survive here.
    thread_local CompilerContext context;
    shaderc_shader_kind          kind = shaderc_glsl_vertex_shader;
    switch (stage) {
        case GlslStage::eVertex: kind = shaderc_glsl_vertex_shader; break;
        case GlslStage::eFragment: kind = shaderc_glsl_fragment_shader; break;
        case GlslStage::eCompute: kind = shaderc_glsl_compute_shader; break;
    }
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        context.compiler, source.data(), source.size(), kind, debugName.c_str(), "main", context.options);
    if (!result) throw compileFailure("shaderc returned no compilation result");
    struct CompilationResult {
        shaderc_compilation_result_t value;
        ~CompilationResult() { shaderc_result_release(value); }
    } ownedResult{result};
    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
        throw compileFailure("shaderc failed:\n" + std::string(shaderc_result_get_error_message(result)));
    const std::size_t length = shaderc_result_get_length(result);
    return spirvWords(shaderc_result_get_bytes(result), length);
}
#endif

}  // namespace

bool glslRuntimeCompilationAvailable() {
#if defined(EVE_HAS_SHADERC)
    return true;
#elif defined(_WIN32)
    // findGlslc() walks the SDK directories and may spawn `where`, so probe once.
    static const bool available = !findGlslc().empty();
    return available;
#else
    static const bool available = externalGlslcAvailable();
    return available;
#endif
}

std::vector<std::uint32_t> compileGlslToSpirv(const std::string &source, GlslStage stage,
                                              const std::string &debugName) {
    if (source.empty()) throw compileFailure(std::string("empty ") + stageName(stage) + " GLSL source");
#if defined(EVE_HAS_SHADERC)
    return compileInProcess(source, stage, debugName);
#else
    return compileWithExternalGlslc(source, stage, debugName);
#endif
}

}  // namespace eve::graphics
