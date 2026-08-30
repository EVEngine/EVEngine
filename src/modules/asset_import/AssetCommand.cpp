#include "asset/AssetCooker.h"
#include "asset/AssetMigration.h"
#include "asset/AssetDiff.h"
#include "asset/AssetPackageStore.h"
#include "asset_import/AssetImporter.h"
#include "asset_import/UnityImporter.h"
#include "asset_import/UnrealImporter.h"
#include "asset_import/LegacyAnimationImporter.h"

#include "cmdline/cmdline.h"

#include <CLI11.hpp>

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

namespace eve::cmd {
namespace {

using asset_import::PreparedAssetImport;
using asset_import::ImportPackageIdentity;

Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path,
                                           std::uint64_t maximumBytes) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > maximumBytes || size > std::numeric_limits<std::size_t>::max())
        return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "asset input size is outside limits", path.string(), {},
            "cmd.asset"));
    std::ifstream input(path, std::ios::binary);
    if (!input) return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(
        DiagnosticCode::NotFound, "cannot open asset input", path.string(), {}, "cmd.asset"));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(
        DiagnosticCode::Failed, "cannot read complete asset input", path.string(), {}, "cmd.asset"));
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

void printFailure(const Status& status) {
    const Diagnostic* diagnostic = status.primaryDiagnostic();
    if (!diagnostic) {
        std::cerr << "asset operation failed\n";
        return;
    }
    std::cerr << diagnostic->source() << ": " << diagnostic->message();
    if (!diagnostic->path().empty()) std::cerr << " [" << diagnostic->path() << "]";
    std::cerr << "\n";
}

Result<ImportPackageIdentity> packageIdentity(std::string_view id, std::string name,
                                              std::string version) {
    auto parsed = PersistentId::parse(id);
    if (!parsed || parsed->isNil() || name.empty() || version.empty())
        return Result<ImportPackageIdentity>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "--package-id must be a non-nil canonical UUID and package name/version are required", {}, {},
            "cmd.asset"));
    Value::Object provenance{{"provider", Value("local")},
                             {"license", Value(Value::Object{{"redistribution", Value("unknown")}})}};
    return Result<ImportPackageIdentity>::success(
        {*parsed, std::move(name), std::move(version), std::move(provenance)});
}

Result<std::map<std::string, std::vector<std::uint8_t>>> readTree(
    const std::filesystem::path& root, std::uint64_t maximumBytes) {
    if (!std::filesystem::is_directory(root))
        return Result<std::map<std::string, std::vector<std::uint8_t>>>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "asset project root is not a directory", root.string(), {}, "cmd.asset"));
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::uint64_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) return Result<std::map<std::string, std::vector<std::uint8_t>>>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "cannot traverse asset project: " + ec.message(), root.string(), {},
            "cmd.asset"));
        if (!it->is_regular_file()) continue;
        const auto size = it->file_size(ec);
        if (ec || size > maximumBytes || total > maximumBytes - size)
            return Result<std::map<std::string, std::vector<std::uint8_t>>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "asset project exceeds source budget", it->path().string(), {},
                "cmd.asset"));
        auto bytes = readFile(it->path(), maximumBytes);
        if (!bytes) return Result<std::map<std::string, std::vector<std::uint8_t>>>::failure(bytes.status());
        auto relative = std::filesystem::relative(it->path(), root, ec).generic_string();
        if (ec || relative.empty())
            return Result<std::map<std::string, std::vector<std::uint8_t>>>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "cannot canonicalize project-relative asset path", it->path().string(), {},
                "cmd.asset"));
        total += size;
        files.emplace(std::move(relative), std::move(bytes).takeValue());
    }
    return Result<std::map<std::string, std::vector<std::uint8_t>>>::success(std::move(files));
}

struct AssetArgs final : Handler {
    CLI::App* assetCommand = nullptr;
    CLI::App* importCommand = nullptr;
    CLI::App* validateCommand = nullptr;
    CLI::App* inspectCommand = nullptr;
    CLI::App* cookCommand = nullptr;
    CLI::App* migrateCommand = nullptr;
    CLI::App* migrateAnimationCommand = nullptr;
    CLI::App* diffCommand = nullptr;
    std::string from, input, secondInput, output, packageId, packageName = "imported.asset", packageVersion = "1.0.0";
    std::string descriptor, terrain, prefab, colorSpace = "srgb", usage = "color", target;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        assetCommand = app.add_subcommand("asset", "Import, validate, inspect and Cook EVEngine asset packages");
        assetCommand->formatter(formatter);
        importCommand = assetCommand->add_subcommand("import", "Import source content into a canonical .eva");
        importCommand->add_option("input", input)->required();
        importCommand->add_option("--from", from, "image|gltf|unity|ue5")->required();
        importCommand->add_option("--out", output)->required();
        importCommand->add_option("--package-id", packageId)->required();
        importCommand->add_option("--name", packageName);
        importCommand->add_option("--version", packageVersion);
        importCommand->add_option("--descriptor", descriptor, "UE adapter descriptor relative to input root");
        importCommand->add_option("--terrain", terrain, "Unity TerrainData relative to input root");
        importCommand->add_option("--prefab", prefab, "Unity Prefab relative to input root");
        importCommand->add_option("--color-space", colorSpace, "srgb|linear");
        importCommand->add_option("--usage", usage);
        validateCommand = assetCommand->add_subcommand("validate", "Fully validate a .eva or .evpack");
        validateCommand->add_option("input", input)->required();
        inspectCommand = assetCommand->add_subcommand("inspect", "Print package identities and contents");
        inspectCommand->add_option("input", input)->required();
        cookCommand = assetCommand->add_subcommand("cook", "Cook .eva into target-specific .evpack");
        cookCommand->add_option("input", input)->required();
        cookCommand->add_option("--target", target)->required();
        cookCommand->add_option("--out", output)->required();
        migrateCommand = assetCommand->add_subcommand(
            "migrate", "Migrate N-1 canonical definitions and atomically publish a new .eva");
        migrateCommand->add_option("input", input)->required();
        migrateCommand->add_option("--out", output)->required();
        migrateAnimationCommand = assetCommand->add_subcommand(
            "migrate-animation", "Wrap a legacy EVA 1 text fixture as canonical skeleton/clip .eva");
        migrateAnimationCommand->add_option("input", input)->required();
        migrateAnimationCommand->add_option("--out", output)->required();
        migrateAnimationCommand->add_option("--package-id", packageId)->required();
        migrateAnimationCommand->add_option("--name", packageName);
        migrateAnimationCommand->add_option("--version", packageVersion);
        diffCommand = assetCommand->add_subcommand("diff", "Compare canonical assets and dependencies in two .eva files");
        diffCommand->add_option("before", input)->required();
        diffCommand->add_option("after", secondInput)->required();
    }

    int parse(CLI::App&, Cmdline&) override {
        if (importCommand && importCommand->parsed()) return runImport();
        if (validateCommand && validateCommand->parsed()) return runValidate(false);
        if (inspectCommand && inspectCommand->parsed()) return runValidate(true);
        if (cookCommand && cookCommand->parsed()) return runCook();
        if (migrateCommand && migrateCommand->parsed()) return runMigrate();
        if (migrateAnimationCommand && migrateAnimationCommand->parsed()) return runMigrateAnimation();
        if (diffCommand && diffCommand->parsed()) return runDiff();
        return -1;
    }

    int runImport() {
        auto identity = packageIdentity(packageId, packageName, packageVersion);
        if (!identity) { printFailure(identity.status()); return 2; }
        Result<PreparedAssetImport> prepared = Result<PreparedAssetImport>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "unknown importer", from, {}, "cmd.asset"));
        asset_import::AssetImportLimits limits;
        if (from == "image" || from == "gltf") {
            auto source = readFile(input, limits.maximumSourceBytes);
            if (!source) { prepared.ignore(); printFailure(source.status()); return 2; }
            if (from == "image") {
                if (colorSpace != "srgb" && colorSpace != "linear") {
                    prepared.ignore(); std::cerr << "--color-space must be srgb or linear\n"; return 2;
                }
                prepared.ignore();
                prepared = asset_import::prepareImageImport(
                    {identity.value(), std::filesystem::path(input).filename().string(),
                     std::move(source).takeValue(),
                     colorSpace == "srgb" ? asset_import::ImageColorSpace::Srgb
                                          : asset_import::ImageColorSpace::Linear,
                     usage, limits});
            } else {
                std::map<std::string, std::vector<std::uint8_t>> external;
                std::error_code ec;
                const auto parent = std::filesystem::path(input).parent_path();
                for (std::filesystem::directory_iterator it(parent.empty() ? "." : parent, ec), end;
                     !ec && it != end; it.increment(ec)) {
                    if (!it->is_regular_file() || it->path() == std::filesystem::path(input)) continue;
                    auto value = readFile(it->path(), limits.maximumSourceBytes);
                    if (value) external.emplace(it->path().filename().generic_string(), std::move(value).takeValue());
                }
                prepared.ignore();
                prepared = asset_import::prepareGltfImport(
                    {identity.value(), std::filesystem::path(input).filename().string(),
                     std::move(source).takeValue(), std::move(external), limits});
            }
        } else if (from == "unity" || from == "ue5") {
            auto files = readTree(input, limits.maximumSourceBytes);
            if (!files) { prepared.ignore(); printFailure(files.status()); return 2; }
            prepared.ignore();
            if (from == "unity")
                prepared = asset_import::prepareUnityProjectImport(
                    {identity.value(), std::move(files).takeValue(), terrain, prefab, limits});
            else
                prepared = asset_import::prepareUnrealM4Import(
                    {identity.value(), std::move(files).takeValue(), descriptor, limits});
        }
        if (!prepared) { printFailure(prepared.status()); return 2; }
        asset::AtomicAssetPackageStore store;
        auto receipt = store.publishEva(output, prepared.value().manifest, prepared.value().entries);
        if (!receipt) { printFailure(receipt.status()); return 3; }
        std::cout << "published " << receipt.value().destination << " package="
                  << receipt.value().packageId.format() << " bytes=" << receipt.value().byteSize << "\n";
        for (const auto& finding : prepared.value().findings)
            std::cout << "finding " << static_cast<unsigned>(finding.disposition) << " "
                      << finding.feature << " " << finding.message << "\n";
        return 0;
    }

    int runValidate(bool inspect) {
        asset::EvaArchiveLimits evaLimits;
        asset::EvpackLimits packLimits;
        auto source = readFile(input, std::max(evaLimits.maximumArchiveBytes, packLimits.maximumPackageBytes));
        if (!source) { printFailure(source.status()); return 2; }
        if (source.value().size() >= 8 &&
            std::string_view(reinterpret_cast<const char*>(source.value().data()), 6) == "EVPACK") {
            auto parsed = asset::parseEvpack(source.value(), packLimits);
            if (!parsed) { printFailure(parsed.status()); return 2; }
            if (inspect) std::cout << "evpack package=" << parsed.value().packageId().format()
                                   << " build=" << parsed.value().buildId().format()
                                   << " variants=" << parsed.value().variants().size()
                                   << " chunks=" << parsed.value().chunks().size() << "\n";
        } else {
            auto parsed = asset::parseEvaArchive(source.value(), evaLimits);
            if (!parsed) { printFailure(parsed.status()); return 2; }
            if (inspect) std::cout << "eva package=" << parsed.value().manifest.packageId.format()
                                   << " name=" << parsed.value().manifest.packageName
                                   << " assets=" << parsed.value().manifest.assets.size()
                                   << " entries=" << parsed.value().entries.size() << "\n";
        }
        if (!inspect) std::cout << "valid " << input << "\n";
        return 0;
    }

    int runCook() {
        asset::EvaArchiveLimits evaLimits;
        auto source = readFile(input, evaLimits.maximumArchiveBytes);
        if (!source) { printFailure(source.status()); return 2; }
        auto archive = asset::parseEvaArchive(source.value(), evaLimits);
        if (!archive) { printFailure(archive.status()); return 2; }
        auto profile = asset::assetCookProfileForTarget(target);
        if (!profile) { printFailure(profile.status()); return 2; }
        auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
        if (!cooked) { printFailure(cooked.status()); return 3; }
        asset::AtomicAssetPackageStore store;
        auto receipt = store.publishEvpack(output, cooked.value().bytes);
        if (!receipt) { printFailure(receipt.status()); return 3; }
        std::cout << "published " << receipt.value().destination << " build="
                  << receipt.value().buildId->format() << " chunks=" << cooked.value().chunkCount << "\n";
        return 0;
    }

    int runMigrate() {
        asset::EvaArchiveLimits limits;
        auto source = readFile(input, limits.maximumArchiveBytes);
        if (!source) { printFailure(source.status()); return 2; }
        auto archive = asset::parseEvaArchive(source.value(), limits);
        if (!archive) { printFailure(archive.status()); return 2; }
        auto migrated = asset::migrateEvaArchive(std::move(archive).takeValue(), limits);
        if (!migrated) { printFailure(migrated.status()); return 3; }
        auto candidate = std::move(migrated).takeValue();
        asset::AtomicAssetPackageStore store;
        auto receipt = store.publishEva(output, candidate.manifest, std::move(candidate.entries), limits);
        if (!receipt) { printFailure(receipt.status()); return 3; }
        std::cout << "published migrated " << receipt.value().destination << " package="
                  << receipt.value().packageId.format() << "\n";
        return 0;
    }

    int runMigrateAnimation() {
        asset_import::AssetImportLimits limits;
        auto identity = packageIdentity(packageId, packageName, packageVersion);
        if (!identity) { printFailure(identity.status()); return 2; }
        auto source = readFile(input, limits.maximumSourceBytes);
        if (!source) { printFailure(source.status()); return 2; }
        std::string text(reinterpret_cast<const char*>(source.value().data()), source.value().size());
        auto prepared = asset_import::prepareLegacyAnimationImport(
            {identity.value(), std::filesystem::path(input).filename().string(), std::move(text), limits});
        if (!prepared) { printFailure(prepared.status()); return 2; }
        auto candidate = std::move(prepared).takeValue();
        asset::AtomicAssetPackageStore store;
        auto receipt = store.publishEva(output, candidate.manifest, std::move(candidate.entries));
        if (!receipt) { printFailure(receipt.status()); return 3; }
        std::cout << "published animation " << receipt.value().destination << " package="
                  << receipt.value().packageId.format() << "\n";
        return 0;
    }

    int runDiff() {
        asset::EvaArchiveLimits limits;
        auto beforeBytes = readFile(input, limits.maximumArchiveBytes);
        auto afterBytes = readFile(secondInput, limits.maximumArchiveBytes);
        if (!beforeBytes) { printFailure(beforeBytes.status()); return 2; }
        if (!afterBytes) { printFailure(afterBytes.status()); return 2; }
        auto before = asset::parseEvaArchive(beforeBytes.value(), limits);
        auto after = asset::parseEvaArchive(afterBytes.value(), limits);
        if (!before) { printFailure(before.status()); return 2; }
        if (!after) { printFailure(after.status()); return 2; }
        auto difference = asset::diffEvaManifests(before.value().manifest, after.value().manifest);
        if (!difference) { printFailure(difference.status()); return 3; }
        for (const auto& change : difference.value().assets) {
            const char* kind = change.kind == asset::EvaAssetChangeKind::Added ? "added" :
                               change.kind == asset::EvaAssetChangeKind::Removed ? "removed" : "changed";
            std::cout << kind << " asset://" << change.assetId.format();
            if (!change.beforeType.empty()) std::cout << " before=" << change.beforeType << "/" << change.beforeVersion.value();
            if (!change.afterType.empty()) std::cout << " after=" << change.afterType << "/" << change.afterVersion.value();
            std::cout << "\n";
        }
        std::cout << "dependencies +" << difference.value().addedDependencies << " -"
                  << difference.value().removedDependencies << " entrypoints="
                  << (difference.value().entrypointsChanged ? "changed" : "same") << "\n";
        return 0;
    }
};

CMD_REG(AssetArgs);

}  // namespace
}  // namespace eve::cmd
