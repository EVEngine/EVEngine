#include "asset/import/StructuredModelImporter.h"

#if !defined(__EMSCRIPTEN__) && !defined(EVENGINE_ANDROID) && !defined(__ANDROID__) && !defined(EVENGINE_IOS)
#include <Poco/Exception.h>
#include <Poco/Process.h>

#include <algorithm>
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
    for (const auto& part : request.parts) {
        if (part.empty()) return failure(DiagnosticCode::InvalidArgument, "SCAD part names must be non-empty");
        if (std::count(request.parts.begin(), request.parts.end(), part) != 1)
            return failure(DiagnosticCode::InvalidArgument, "SCAD part names must be unique");
    }
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
        auto utf8 = [](const std::filesystem::path& path) {
            const auto value = path.u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
        };
        auto compileOne = [&](const std::filesystem::path& output,
                              const std::vector<std::string>& extraArgs) -> Result<std::vector<std::uint8_t> > {
            std::vector<std::string> args = {"--hardwarnings", "--export-format", "binstl", "-o", utf8(output)};
            args.insert(args.end(), extraArgs.begin(), extraArgs.end());
            args.push_back(utf8(input));
            auto       process  = Poco::Process::launch(request.executable, args);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(request.timeoutSeconds);
            int        exit     = -1;
            while ((exit = process.tryWait()) == -1) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    Poco::Process::kill(process.id());
                    process.wait();
                    return Result<std::vector<std::uint8_t> >::failure(
                        Diagnostic::error(DiagnosticCode::Failed, "OpenSCAD compilation timed out", request.sourcePath,
                                          {}, "asset.import.scad"));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (exit != 0)
                return Result<std::vector<std::uint8_t> >::failure(Diagnostic::error(
                    DiagnosticCode::Failed, "OpenSCAD compiler exited with code " + std::to_string(exit),
                    request.sourcePath, {}, "asset.import.scad"));
            return read(output);
        };

        ScadImportRequest prepared{request.package, utf8(input.filename()), std::move(source).takeValue(), {}, {},
                                   {}, request.limits};
        if (request.parts.empty()) {
            auto stl = compileOne(directory / "model.stl", {});
            if (!stl) return Output::failure(stl.status());
            prepared.binaryStl = std::move(stl).takeValue();
        } else {
            for (const auto& part : request.parts) {
                auto stl = compileOne(directory / (part + ".stl"), {"-D", "part=\"" + part + "\""});
                if (!stl) return Output::failure(stl.status());
                prepared.partStls.emplace(part, std::move(stl).takeValue());
            }
        }
        if (!request.rigPath.empty()) {
            const auto rig = std::filesystem::absolute(
                std::filesystem::path(std::u8string(request.rigPath.begin(), request.rigPath.end())));
            auto bytes = read(rig);
            if (!bytes) return Output::failure(bytes.status());
            prepared.rigBytes = std::move(bytes).takeValue();
        }
        auto unchanged = read(input);
        if (!unchanged) return Output::failure(unchanged.status());
        if (unchanged.value() != prepared.sourceBytes)
            return failure(DiagnosticCode::Conflict, "SCAD source changed during compilation; retry import");
        return prepareScadImport(prepared);
    } catch (const Poco::Exception& error) {
        return failure(DiagnosticCode::Failed,
                       "cannot run OpenSCAD: " + error.displayText() + "; use --openscad <executable>");
    } catch (const std::filesystem::filesystem_error& error) {
        return failure(DiagnosticCode::Failed, "OpenSCAD staging IO failed: " + std::string(error.what()));
    }
#endif
}
}  // namespace eve::asset_import
