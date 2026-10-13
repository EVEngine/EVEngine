#include "asset/import/StructuredModelImporter.h"

#if !defined(__EMSCRIPTEN__) && !defined(EVENGINE_ANDROID) && !defined(__ANDROID__) && !defined(EVENGINE_IOS)
#include <Poco/Exception.h>
#include <Poco/Process.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#endif

namespace eve::asset_import {
Result<PreparedAssetImport> prepareScadFileImport(const ScadFileImportRequest& request) {
    using Output = Result<PreparedAssetImport>;
    auto failure = [&](DiagnosticCode code, std::string message) {
        return Output::failure(
            Diagnostic::error(code, std::move(message), request.sourcePath, {}, "asset.import.scad"));
    };
#if defined(__EMSCRIPTEN__) || defined(EVENGINE_ANDROID) || defined(__ANDROID__) || defined(EVENGINE_IOS)
    return failure(DiagnosticCode::Unsupported, "OpenSCAD compilation is a desktop authoring operation");
#else
    if (request.executable.empty() || request.timeoutSeconds == 0 || request.timeoutSeconds > 3600)
        return failure(DiagnosticCode::InvalidArgument,
                       "OpenSCAD executable and timeout (1..3600 seconds) are required");
    auto read = [&](const std::filesystem::path& path) -> Result<std::vector<std::uint8_t> > {
        std::error_code ec;
        const auto      size = std::filesystem::file_size(path, ec);
        if (ec || size > request.limits.maximumSourceBytes || size > std::size_t(-1))
            return Result<std::vector<std::uint8_t> >::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "OpenSCAD input/output is missing or exceeds byte budget",
                path.string(), {}, "asset.import.scad"));
        std::ifstream             stream(path, std::ios::binary);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!bytes.empty())
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream)
            return Result<std::vector<std::uint8_t> >::failure(Diagnostic::error(
                DiagnosticCode::Failed, "cannot read OpenSCAD input/output", path.string(), {}, "asset.import.scad"));
        return Result<std::vector<std::uint8_t> >::success(std::move(bytes));
    };
    try {
        const auto input = std::filesystem::absolute(
            std::filesystem::path(std::u8string(request.sourcePath.begin(), request.sourcePath.end())));
        auto source = read(input);
        if (!source) return Output::failure(source.status());
        static std::atomic<std::uint64_t> sequence{0};
        const auto                        directory = std::filesystem::temp_directory_path() /
                               ("eve-scad-" + std::to_string(Poco::Process::id()) + "-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                                std::to_string(sequence.fetch_add(1)));
        if (!std::filesystem::create_directory(directory))
            return failure(DiagnosticCode::Conflict, "cannot create private OpenSCAD staging directory");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{directory};
        const auto output = directory / "model.stl";
        auto       utf8   = [](const std::filesystem::path& path) {
            const auto value = path.u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
        };
        auto process = Poco::Process::launch(
            request.executable, {"--hardwarnings", "--export-format", "binstl", "-o", utf8(output), utf8(input)});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(request.timeoutSeconds);
        int        exit     = -1;
        while ((exit = process.tryWait()) == -1) {
            if (std::chrono::steady_clock::now() >= deadline) {
                // Killing by PID keeps the wait handle valid on Poco's Windows backend.
                Poco::Process::kill(process.id());
                process.wait();
                return failure(DiagnosticCode::Failed, "OpenSCAD compilation timed out");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (exit != 0)
            return failure(DiagnosticCode::Failed, "OpenSCAD compiler exited with code " + std::to_string(exit));
        auto stl = read(output);
        if (!stl) return Output::failure(stl.status());
        auto unchanged = read(input);
        if (!unchanged) return Output::failure(unchanged.status());
        if (unchanged.value() != source.value())
            return failure(DiagnosticCode::Conflict, "SCAD source changed during compilation; retry import");
        return prepareScadImport({request.package, utf8(input.filename()), std::move(source).takeValue(),
                                  std::move(stl).takeValue(), request.limits});
    } catch (const Poco::Exception& error) {
        return failure(DiagnosticCode::Failed,
                       "cannot run OpenSCAD: " + error.displayText() + "; use --openscad <executable>");
    } catch (const std::filesystem::filesystem_error& error) {
        return failure(DiagnosticCode::Failed, "OpenSCAD staging IO failed: " + std::string(error.what()));
    }
#endif
}
}  // namespace eve::asset_import
