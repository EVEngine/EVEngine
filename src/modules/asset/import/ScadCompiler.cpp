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
namespace {
#if !defined(__EMSCRIPTEN__) && !defined(EVENGINE_ANDROID) && !defined(__ANDROID__) && !defined(EVENGINE_IOS)
/** @brief Read part names from a ScadRig document so the compiler can batch `-D part=` runs.
 * Full schema validation still happens in prepareScadImport after geometry is produced.
 */
Result<std::vector<std::string>> listScadRigParts(const std::vector<std::uint8_t>& bytes,
                                                  const AssetImportLimits&         limits) {
    using Output = Result<std::vector<std::string>>;
    auto invalid = [](std::string message) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::ParseError, std::move(message), {}, {}, "asset.import.scad"));
    };
    if (bytes.empty() || bytes.size() > limits.maximumSourceBytes)
        return invalid("ScadRig sidecar is missing or outside budget");
    auto parsed = Value::fromJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!parsed) return Output::failure(parsed.status());
    const auto* partsValue = parsed.value().find("parts");
    if (!partsValue || !partsValue->isArray() || partsValue->arraySize() == 0)
        return invalid("ScadRig sidecar requires a non-empty parts array");
    if (partsValue->arraySize() > limits.maximumAssets) return invalid("ScadRig part count exceeds budget");
    std::vector<std::string> names;
    names.reserve(partsValue->arraySize());
    for (std::size_t i = 0; i < partsValue->arraySize(); ++i) {
        const auto& partValue = partsValue->at(i);
        const auto* name      = partValue.find("name");
        if (!name || !name->isString() || name->asString().empty() ||
            name->asString().size() > limits.maximumStringBytes)
            return invalid("ScadRig part name is required and bounded");
        for (const auto& existing : names)
            if (existing == name->asString()) return invalid("ScadRig part names must be unique");
        names.push_back(name->asString());
    }
    return Output::success(std::move(names));
}
#endif
}  // namespace

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
        auto utf8 = [](const std::filesystem::path& path) {
            const auto value = path.u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
        };
        auto compileOne = [&](const std::filesystem::path&      output,
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

        ScadImportRequest prepared{request.package, utf8(input.filename()), std::move(source).takeValue(),
                                   {}, {}, {}, request.limits};
        const auto        stemUtf8 = input.stem().u8string();
        const auto        stemName =
            std::string(reinterpret_cast<const char*>(stemUtf8.data()), stemUtf8.size());
        const auto      sidecar = input.parent_path() / (stemName + ".rig.json");
        std::error_code sidecarEc;
        if (std::filesystem::is_regular_file(sidecar, sidecarEc)) {
            auto rig = read(sidecar);
            if (!rig) return Output::failure(rig.status());
            prepared.rigBytes = std::move(rig).takeValue();
            auto parts        = listScadRigParts(prepared.rigBytes, request.limits);
            if (!parts) return Output::failure(parts.status());
            for (const auto& part : parts.value()) {
                auto stl = compileOne(directory / (part + ".stl"), {"-D", "part=\"" + part + "\""});
                if (!stl) return Output::failure(stl.status());
                prepared.partStls.emplace(part, std::move(stl).takeValue());
            }
        } else {
            auto stl = compileOne(directory / "model.stl", {});
            if (!stl) return Output::failure(stl.status());
            prepared.binaryStl = std::move(stl).takeValue();
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
