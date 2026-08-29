#include "asset/AssetCooker.h"
#include "asset/AssetMigration.h"
#include "asset/CanonicalImageCook.h"
#include "asset/CanonicalPcgCook.h"

#include "asset/EvpackCompression.h"
#include "asset/RuntimeDefinition.h"

#include "data/HashFunction.h"

#include <algorithm>
#include <map>
#include <set>

namespace eve::asset {
namespace {

template <class T>
Result<T> cookFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.cook"));
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes) {
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")
        ->hash("sha256", reinterpret_cast<const char*>(bytes.data()), bytes.size(), digest);
    std::array<std::uint8_t, 32> result{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data), result.size(), result.begin());
    return result;
}

void appendString(std::vector<std::uint8_t>& bytes, std::string_view text) {
    const std::uint64_t size = text.size();
    for (unsigned shift = 0; shift != 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(size >> shift));
    bytes.insert(bytes.end(), text.begin(), text.end());
}

PersistentId buildIdentity(const EvaArchive& source, const AssetCookProfile& profile,
                           std::string_view canonicalManifest) {
    std::vector<std::uint8_t> key;
    appendString(key, "eve.asset-cooker/1");
    appendString(key, evpackCodecBuildIdentity(profile.chunkCodec));
    const auto packageBytes = source.manifest.packageId.bytes();
    key.insert(key.end(), packageBytes.begin(), packageBytes.end());
    appendString(key, canonicalManifest);
    appendString(key, profile.variant.os); appendString(key, profile.variant.arch);
    appendString(key, profile.variant.graphics); appendString(key, profile.variant.shaderFormat);
    appendString(key, profile.variant.quality);
    for (const auto& family : profile.variant.textureFamilies) appendString(key, family);
    for (const auto& feature : profile.variant.features) appendString(key, feature);
    for (unsigned shift = 0; shift != 32; shift += 8)
        key.push_back(static_cast<std::uint8_t>(profile.bulkAlignment >> shift));
    for (const auto& entry : source.entries) {
        appendString(key, entry.path);
        const auto hash = sha256(entry.bytes);
        key.insert(key.end(), hash.begin(), hash.end());
    }
    auto digest = sha256(key);
    PersistentId::Bytes bytes{};
    std::copy_n(digest.begin(), bytes.size(), bytes.begin());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x50);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80);
    return PersistentId(bytes);
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool predicateText(const Value::Object& predicate, std::string_view name, std::string_view actual) {
    const Value* value = field(predicate, name);
    return !value || (value->isString() && value->asString() == actual);
}

bool platformDependencyApplies(const EvaDependency& dependency, const EvpackVariant& variant) {
    if (dependency.kind != EvaDependencyKind::Platform) return true;
    return predicateText(dependency.predicate, "os", variant.os) &&
           predicateText(dependency.predicate, "arch", variant.arch) &&
           predicateText(dependency.predicate, "graphics", variant.graphics) &&
           predicateText(dependency.predicate, "quality", variant.quality);
}

Result<void> validatePublication(const EvaManifest& manifest, CookPublication publication) {
    if (publication == CookPublication::LocalInspection) return Result<void>::success();
    const Value* license = field(manifest.provenance, "license");
    const auto* object = license ? license->getIf<Value::Object>() : nullptr;
    const Value* redistribution = object ? field(*object, "redistribution") : nullptr;
    if (!redistribution || !redistribution->isString() ||
        (redistribution->asString() != "runtime-embedded" && redistribution->asString() != "project-only"))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation,
            "public Cook requires an explicit redistribution policy", "$.provenance.license.redistribution", {},
            "asset.cook"));
    return Result<void>::success();
}

EvpackChunkKind kindForPath(std::string_view path) {
    if (path.find("/stream/") != std::string_view::npos) return EvpackChunkKind::Stream;
    if (path.find("/shaders/") != std::string_view::npos) return EvpackChunkKind::Shader;
    return EvpackChunkKind::Bulk;
}

Result<std::vector<std::uint8_t>> binaryDefinition(
    std::span<const std::uint8_t> json, std::uint64_t maximumBytes,
    std::span<const EvaDependency> dependencies = {}) {
    auto parsed = Value::fromJson(std::string_view(
        reinterpret_cast<const char*>(json.data()), json.size()));
    if (!parsed) return Result<std::vector<std::uint8_t>>::failure(parsed.status());
    auto* object = parsed.value().getIf<Value::Object>();
    if (!object)
        return cookFailure<std::vector<std::uint8_t>>(
            DiagnosticCode::ParseError, "runtime definition root must be an object");
    Value::Array fallbacks;
    for (const auto& dependency : dependencies) {
        if (dependency.kind != EvaDependencyKind::RuntimeOptional) continue;
        Value::Object fallback;
        fallback["path"] = Value(dependency.path);
        fallback["to"] = Value(dependency.to.format());
        fallback["expectedType"] = Value(dependency.expectedType);
        fallback["policy"] = Value(dependency.fallback);
        fallbacks.emplace_back(std::move(fallback));
    }
    if (!fallbacks.empty()) (*object)["dependencyFallbacks"] = Value(std::move(fallbacks));
    RuntimeDefinitionLimits limits;
    limits.maximumBytes = maximumBytes;
    return encodeRuntimeDefinition(parsed.value(), limits);
}

}  // namespace

Result<AssetCookProfile> assetCookProfileForTarget(std::string_view target) {
    AssetCookProfile profile;
    profile.publication = CookPublication::LocalInspection;
    if (target == "windows-x86_64-vulkan")
        profile.variant = {"windows", "x86_64", "vulkan", {"bc", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "linux-x86_64-vulkan")
        profile.variant = {"linux", "x86_64", "vulkan", {"bc", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "macos-arm64-vulkan")
        profile.variant = {"macos", "arm64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "android-arm64-vulkan")
        profile.variant = {"android", "arm64", "vulkan", {"astc", "etc2", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "ios-arm64-vulkan")
        profile.variant = {"ios", "arm64", "vulkan", {"astc", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "web-wasm32-webgpu")
        profile.variant = {"web", "wasm32", "webgpu", {"rgba8"}, "wgsl-1", "high", {}};
    else
        return cookFailure<AssetCookProfile>(DiagnosticCode::Unsupported,
                                             "unknown asset Cook target", std::string(target));
    return Result<AssetCookProfile>::success(std::move(profile));
}

Result<AssetCookReceipt> cookEvaToEvpack(const EvaArchive& source, const AssetCookProfile& profile,
                                         const EvaArchiveLimits& evaLimits,
                                         const EvpackLimits& evpackLimits) {
    auto migrated = migrateEvaArchive(source, evaLimits);
    if (!migrated) return Result<AssetCookReceipt>::failure(migrated.status());
    auto rebuilt = buildEvaArchive(migrated.value().manifest, migrated.value().entries, evaLimits);
    if (!rebuilt) return Result<AssetCookReceipt>::failure(rebuilt.status());
    auto admitted = parseEvaArchive(rebuilt.value(), evaLimits);
    if (!admitted) return Result<AssetCookReceipt>::failure(admitted.status());
    EvaArchive canonical = std::move(admitted).takeValue();
    EvaManifest dependencyManifest = canonical.manifest;
    std::erase_if(dependencyManifest.dependencies, [&](const EvaDependency& dependency) {
        return dependency.kind == EvaDependencyKind::Platform &&
               !platformDependencyApplies(dependency, profile.variant);
    });
    auto dependencyValidation = validateEvaDependencies(dependencyManifest,
                                                        profile.availableDependencies);
    if (!dependencyValidation)
        return Result<AssetCookReceipt>::failure(dependencyValidation.status());
    auto canonicalManifest = serializeEvaManifest(canonical.manifest);
    if (!canonicalManifest) return Result<AssetCookReceipt>::failure(canonicalManifest.status());
    auto publication = validatePublication(canonical.manifest, profile.publication);
    if (!publication) return Result<AssetCookReceipt>::failure(publication.status());

    std::map<PersistentId, std::vector<PersistentId>> runtimeDependencies;
    for (const auto& dependency : canonical.manifest.dependencies) {
        if (dependency.kind == EvaDependencyKind::RuntimeOptional ||
            dependency.kind == EvaDependencyKind::Build || dependency.kind == EvaDependencyKind::Editor ||
            dependency.kind == EvaDependencyKind::Source || !platformDependencyApplies(dependency, profile.variant))
            continue;
        const bool present = std::binary_search(dependencyValidation.value().presentAssets.begin(),
                                                dependencyValidation.value().presentAssets.end(),
                                                dependency.to.id());
        if (present) runtimeDependencies[dependency.from.id()].push_back(dependency.to.id());
    }
    for (auto& [asset, dependencies] : runtimeDependencies) {
        (void)asset;
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    }

    EvpackBuild build;
    build.packageId = canonical.manifest.packageId;
    build.buildId = buildIdentity(canonical, profile, canonicalManifest.value());
    build.variants.push_back(profile.variant);
    for (const auto& asset : canonical.manifest.assets) {
        std::vector<EvaDependency> assetDependencyPolicies;
        for (const auto& dependency : canonical.manifest.dependencies)
            if (dependency.from.id() == asset.asset.id() &&
                platformDependencyApplies(dependency, profile.variant))
                assetDependencyPolicies.push_back(dependency);
        const auto definition = std::lower_bound(canonical.entries.begin(), canonical.entries.end(), asset.definition,
                                                 [](const EvaArchiveEntry& entry, std::string_view path) {
                                                     return entry.path < path;
                                                 });
        if (definition == canonical.entries.end() || definition->path != asset.definition)
            return cookFailure<AssetCookReceipt>(DiagnosticCode::NotFound, "asset definition is missing",
                                                 asset.definition);
        const auto dependencies = runtimeDependencies[asset.asset.id()];
        if (asset.type == "eve.image") {
            if (std::find(profile.variant.textureFamilies.begin(),
                          profile.variant.textureFamilies.end(), "rgba8") ==
                profile.variant.textureFamilies.end())
                return cookFailure<AssetCookReceipt>(
                    DiagnosticCode::Unsupported,
                    "eve.image/2 requires an explicit rgba8 target texture family", asset.definition);
            auto parsedDefinition = Value::fromJson(std::string_view(
                reinterpret_cast<const char*>(definition->bytes.data()), definition->bytes.size()));
            if (!parsedDefinition)
                return Result<AssetCookReceipt>::failure(parsedDefinition.status());
            const auto* definitionObject = parsedDefinition.value().getIf<Value::Object>();
            const Value* blobValue = definitionObject ? field(*definitionObject, "blob") : nullptr;
            if (!blobValue || !blobValue->isString())
                return cookFailure<AssetCookReceipt>(DiagnosticCode::ParseError,
                                                     "eve.image/2 source blob path is missing",
                                                     asset.definition);
            const auto sourceBlob = std::lower_bound(
                canonical.entries.begin(), canonical.entries.end(), blobValue->asString(),
                [](const EvaArchiveEntry& entry, std::string_view path) { return entry.path < path; });
            if (sourceBlob == canonical.entries.end() || sourceBlob->path != blobValue->asString())
                return cookFailure<AssetCookReceipt>(DiagnosticCode::NotFound,
                                                     "eve.image/2 source blob is missing",
                                                     blobValue->asString());
            auto image = cookCanonicalImageRgba8(definition->bytes, sourceBlob->bytes,
                                                 evpackLimits.maximumChunkBytes);
            if (!image) return Result<AssetCookReceipt>::failure(image.status());
            auto runtimeDefinition = binaryDefinition(image.value().definition,
                                                      evpackLimits.maximumChunkBytes,
                                                      assetDependencyPolicies);
            if (!runtimeDefinition)
                return Result<AssetCookReceipt>::failure(runtimeDefinition.status());
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0,
                                    EvpackChunkKind::Definition, 0, profile.chunkCodec, 8,
                                    dependencies, std::move(runtimeDefinition).takeValue()});
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0,
                                    EvpackChunkKind::Bulk, 1, EvpackCodec::None,
                                    profile.bulkAlignment, dependencies,
                                    std::move(image).takeValue().bulk});
            continue;
        }
        if (asset.type == "eve.pcg-graph") {
            auto pcg = cookCanonicalPcgGraph(definition->bytes, 4096,
                                             evpackLimits.maximumChunkBytes);
            if (!pcg) return Result<AssetCookReceipt>::failure(pcg.status());
            auto runtimeDefinition = binaryDefinition(pcg.value().definition,
                                                      evpackLimits.maximumChunkBytes,
                                                      assetDependencyPolicies);
            if (!runtimeDefinition)
                return Result<AssetCookReceipt>::failure(runtimeDefinition.status());
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0,
                                    EvpackChunkKind::Definition, 0, profile.chunkCodec, 8,
                                    dependencies, std::move(runtimeDefinition).takeValue()});
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0,
                                    EvpackChunkKind::Bulk, 1, profile.chunkCodec,
                                    profile.bulkAlignment, dependencies,
                                    std::move(pcg).takeValue().executionPlan});
            continue;
        }
        auto runtimeDefinition = binaryDefinition(definition->bytes,
                                                  evpackLimits.maximumChunkBytes,
                                                  assetDependencyPolicies);
        if (!runtimeDefinition)
            return Result<AssetCookReceipt>::failure(runtimeDefinition.status());
        build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0,
                                EvpackChunkKind::Definition, 0, profile.chunkCodec, 8,
                                dependencies, std::move(runtimeDefinition).takeValue()});
        const std::string prefix = "assets/" + asset.asset.id().format() + "/";
        std::uint32_t chunkId = 1;
        for (const auto& entry : canonical.entries) {
            if (entry.path == asset.definition || !entry.path.starts_with(prefix)) continue;
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0,
                                    kindForPath(entry.path), chunkId++, profile.chunkCodec,
                                    profile.bulkAlignment, dependencies, entry.bytes});
        }
    }
    const std::uint32_t chunkCount = static_cast<std::uint32_t>(build.chunks.size());
    const PersistentId buildId = build.buildId;
    auto packageBytes = buildEvpack(std::move(build), evpackLimits);
    if (!packageBytes) return Result<AssetCookReceipt>::failure(packageBytes.status());
    auto verified = parseEvpack(packageBytes.value(), evpackLimits);
    if (!verified) return Result<AssetCookReceipt>::failure(verified.status());
    return Result<AssetCookReceipt>::success(
        {canonical.manifest.packageId, buildId, chunkCount, std::move(packageBytes).takeValue()});
}

}  // namespace eve::asset
