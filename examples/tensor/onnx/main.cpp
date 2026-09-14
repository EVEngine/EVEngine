#include <chrono>
#include <cstdlib>
#include "tensor/OnnxModel.h"
#ifdef EVE_ONNX_ENGINE_PROVIDER
#include "common/CrashHandler.h"
#include "graphics/Graphics.h"
#include "tensor/OnnxCompute.h"
#elif defined(EVE_ONNX_VULKAN_PROBE)
#include "VulkanProbeCompute.h"
#endif

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
#ifdef EVE_ONNX_ENGINE_PROVIDER
    eve::installCrashHandler();
#endif
    std::cout.setf(std::ios::unitbuf);
    if (argc < 2) {
        std::cerr << "Usage: onnx_probe MODEL [OUTPUT_NAME RAW_OUTPUT_FILE [COMMA_SEPARATED_TOKEN_IDS]]\n";
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 512 * 1024 * 1024) return 2;
    std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return 2;
    const auto importStart = std::chrono::steady_clock::now();
    auto       imported    = eve::tensor::OnnxModel::load(bytes);
    std::cout << "ONNX_IMPORT_TIME ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - importStart).count()
              << '\n';
    if (!imported.ok()) {
        std::cerr << imported.error()->message() << '\n';
        return 1;
    }
    auto       model = std::move(imported.value());
    const auto info  = model->info();
    std::cout << "ONNX_IMPORT nodes=" << info.nodeCount << " packed_bytes=" << info.initializerBytes
              << " unsupported_nodes=" << info.unsupportedNodes.size() << '\n';
    if (argc < 4) {
        for (const auto& node : info.unsupportedNodes) std::cout << node << '\n';
        return 0;
    }
    std::vector<eve::tensor::OnnxNamedTensor> feeds;
    if (argc > 4) {
        std::vector<int64_t> ids;
        std::istringstream   text(argv[4]);
        std::string          item;
        try {
            while (std::getline(text, item, ',')) ids.push_back(std::stoll(item));
        } catch (const std::exception&) {
            return 2;
        }
        eve::tensor::OnnxTensor tokens{eve::tensor::OnnxElement::Int64,
                                       {1, static_cast<int64_t>(ids.size())},
                                       std::vector<uint8_t>(ids.size() * 8)};
        if (!ids.empty()) std::memcpy(tokens.bytes.data(), ids.data(), tokens.bytes.size());
        feeds.push_back({"tokens", std::move(tokens)});
    }
    if (argc > 5) {
        eve::tensor::OnnxTensor style{eve::tensor::OnnxElement::Float32, {1, 256}, std::vector<uint8_t>(1024)};
        std::ifstream           voice(argv[5], std::ios::binary | std::ios::ate);
        if (!voice || voice.tellg() != 1024) {
            std::cerr << "Style file must contain 256 float32 values\n";
            return 2;
        }
        voice.seekg(0);
        if (!voice.read(reinterpret_cast<char*>(style.bytes.data()), 1024)) return 2;
        feeds.push_back({"style", std::move(style)});
        eve::tensor::OnnxTensor speed{eve::tensor::OnnxElement::Float32, {1}, std::vector<uint8_t>(4)};
        const float             rate = 1;
        std::memcpy(speed.bytes.data(), &rate, 4);
        feeds.push_back({"speed", std::move(speed)});
    }
    const std::vector<std::string> outputs{argv[2]};
    size_t                         dispatches = 0;
    const bool                     gpu = std::getenv("EVE_ONNX_GPU") && std::string(std::getenv("EVE_ONNX_GPU")) == "1";
    eve::tensor::OnnxRunOptions    options;
    options.requireFinite = std::getenv("EVE_ONNX_CHECK_FINITE") != nullptr;
    auto execute          = [&]() -> eve::Result<std::vector<eve::tensor::OnnxNamedTensor>> {
        if (!gpu) return model->run(feeds, outputs, options);
#if defined(EVE_ONNX_VULKAN_PROBE) || defined(EVE_ONNX_ENGINE_PROVIDER)
#ifdef EVE_ONNX_ENGINE_PROVIDER
        auto unavailable = eve::tensor::createOnnxGpuCompute();
        if (unavailable.ok())
            return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "Unexpected device before headless initialization"));
        std::cout << "ONNX_GPU_ABSENT_PASS\n";
        auto* graphics = eve::graphics::Graphics::create();
        graphics->initHeadless(64, 64);
        uint32_t compilerWorkers = 4;
        if (const auto* value = std::getenv("EVE_ONNX_COMPILER_WORKERS"))
            compilerWorkers = static_cast<uint32_t>(std::stoul(value));
        auto provider = eve::tensor::createOnnxGpuCompute(compilerWorkers);
#else
        auto provider = createVulkanProbeCompute();
#endif
        if (!provider.ok()) return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::failure(provider.status());
        auto runOnce = [&](int iteration) {
            const auto runStart = std::chrono::steady_clock::now();
            auto       result   = model->runGpu(feeds, *provider.value(), outputs, options);
            std::cout << "ONNX_ITERATION index=" << iteration << " ms="
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - runStart).count();
            if (result.ok())
                std::cout << " compilations=" << result.value().transfers.shaderCompilations
                          << " compiler_peak_workers=" << result.value().transfers.compilerPeakWorkers
                          << " cache_hits=" << result.value().transfers.shaderCacheHits
                          << " upload_bytes=" << result.value().transfers.uploadedBytes;
            std::cout << '\n';
            return result;
        };
        auto result = runOnce(0);
        if (const auto* repeat = std::getenv("EVE_ONNX_REPEAT"); repeat && std::string(repeat) == "2") {
            if (!result.ok()) return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::failure(result.status());
            auto previous = result.value().outputs;
            result        = runOnce(1);
            if (result.ok()) {
                if (previous.size() != result.value().outputs.size())
                    return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Repeat output count mismatch"));
                for (size_t i = 0; i < previous.size(); ++i)
                    if (previous[i].tensor.bytes != result.value().outputs[i].tensor.bytes)
                        return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::failure(
                            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Repeat output bytes differ"));
                std::cout << "ONNX_REPEAT_IDENTICAL_PASS\n";
            }
        }
        if (!result.ok()) return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::failure(result.status());
        dispatches    = result.value().dispatches;
        const auto& t = result.value().transfers;
        std::cout << "ONNX_TRANSFERS uploads=" << t.uploads << " downloads=" << t.downloads
                  << " submissions=" << t.submissions << " upload_bytes=" << t.uploadedBytes
                  << " download_bytes=" << t.downloadedBytes << " allocations=" << t.bufferAllocations
                  << " reuses=" << t.bufferReuses << " compilations=" << t.shaderCompilations
                  << " cache_hits=" << t.shaderCacheHits << '\n';
        return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::success(std::move(result.value().outputs));
#else
        return eve::Result<std::vector<eve::tensor::OnnxNamedTensor>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "Build EVE_ONNX_VULKAN_PROBE to enable GPU parity"));
#endif
    };
    auto generated = execute();
    if (!generated.ok()) {
        std::cerr << generated.error()->path() << ": " << generated.error()->message() << '\n';
        return 1;
    }
    const auto&   output = generated.value().front().tensor;
    std::ofstream target(argv[3], std::ios::binary);
    if (!target.write(reinterpret_cast<const char*>(output.bytes.data()), output.bytes.size())) return 2;
    std::cout << "ONNX_RUN_PASS backend=" << (gpu ? "vulkan_quantized" : "engine_cpu") << " dispatches=" << dispatches
              << " bytes=" << output.bytes.size() << '\n';
}
