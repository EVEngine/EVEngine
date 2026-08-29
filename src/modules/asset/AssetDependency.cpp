#include "asset/AssetDependency.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace eve::asset {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.dependency"));
}
bool requiresPresence(EvaDependencyKind kind) {
    return kind == EvaDependencyKind::RuntimeRequired || kind == EvaDependencyKind::Build ||
           kind == EvaDependencyKind::Platform;
}
bool validFallback(const EvaDependency& dependency) {
    if (dependency.kind != EvaDependencyKind::RuntimeOptional)
        return dependency.fallback.empty();
    const auto behavior = dependency.fallback.find("behavior");
    const auto observable = dependency.fallback.find("observableCode");
    if (behavior == dependency.fallback.end() || !behavior->second.isString() ||
        observable == dependency.fallback.end() || !observable->second.isString() ||
        observable->second.asString().empty())
        return false;
    const std::string& name = behavior->second.asString();
    const auto asset = dependency.fallback.find("asset");
    if (name == "use-asset")
        return asset != dependency.fallback.end() && asset->second.isString() &&
               AssetRef::parse(asset->second.asString()).ok();
    return (name == "omit-feature" || name == "use-default") &&
           asset == dependency.fallback.end();
}
}  // namespace

Result<EvaDependencyValidation> validateEvaDependencies(
    const EvaManifest& manifest, std::span<const AvailableAssetDependency> available) {
    using TypeIdentity = std::pair<std::string, SchemaVersion>;
    std::map<PersistentId, TypeIdentity> types;
    std::set<PersistentId> local;
    for (const auto& asset : manifest.assets) {
        types.emplace(asset.asset.id(), TypeIdentity{asset.type, asset.schemaVersion});
        local.emplace(asset.asset.id());
    }
    for (const auto& external : available) {
        if (external.asset.id().isNil() || external.type.empty() || external.schemaVersion.isZero())
            return fail<EvaDependencyValidation>(DiagnosticCode::InvalidArgument,
                                                 "external dependency fact is invalid");
        const auto [found, inserted] = types.emplace(
            external.asset.id(), TypeIdentity{external.type, external.schemaVersion});
        if (!inserted && found->second != TypeIdentity{external.type, external.schemaVersion})
            return fail<EvaDependencyValidation>(DiagnosticCode::TypeMismatch,
                                                 "dependency resolver reports conflicting types",
                                                 external.asset.format());
    }
    for (const auto& [name, entrypoint] : manifest.entrypoints)
        if (!local.contains(entrypoint.id()))
            return fail<EvaDependencyValidation>(DiagnosticCode::NotFound,
                                                 "entrypoint does not resolve to a local asset", name);

    using EdgeKey = std::tuple<PersistentId, PersistentId, EvaDependencyKind, std::string>;
    std::set<EdgeKey> edges;
    std::map<PersistentId, std::vector<PersistentId>> graph;
    EvaDependencyValidation result;
    for (const auto& dependency : manifest.dependencies) {
        if (!validFallback(dependency))
            return fail<EvaDependencyValidation>(DiagnosticCode::InvalidArgument,
                                                 "dependency fallback policy is invalid",
                                                 dependency.path);
        if (!local.contains(dependency.from.id()))
            return fail<EvaDependencyValidation>(DiagnosticCode::NotFound,
                                                 "dependency source is not locally authoritative",
                                                 dependency.from.format());
        if (!edges.emplace(dependency.from.id(), dependency.to.id(), dependency.kind,
                           dependency.path).second)
            return fail<EvaDependencyValidation>(DiagnosticCode::Conflict,
                                                 "duplicate dependency edge", dependency.path);
        const auto target = types.find(dependency.to.id());
        if (target == types.end()) {
            if (requiresPresence(dependency.kind))
                return fail<EvaDependencyValidation>(DiagnosticCode::NotFound,
                                                     "required dependency is unavailable",
                                                     dependency.to.format());
            result.omittedOptionalAssets.push_back(dependency.to.id());
            continue;
        }
        const std::string resolvedType = target->second.first + "/" +
                                         std::to_string(target->second.second.value());
        if (!dependency.expectedType.empty() && resolvedType != dependency.expectedType)
            return fail<EvaDependencyValidation>(DiagnosticCode::TypeMismatch,
                                                 "dependency resolved to the wrong asset type",
                                                 dependency.path);
        result.presentAssets.push_back(dependency.to.id());
        if (dependency.kind == EvaDependencyKind::RuntimeRequired && local.contains(dependency.to.id()))
            graph[dependency.from.id()].push_back(dependency.to.id());
    }

    enum class Mark : std::uint8_t { Visiting, Complete };
    std::map<PersistentId, Mark> marks;
    std::function<Result<void>(const PersistentId&)> visit = [&](const PersistentId& node) -> Result<void> {
        const auto marked = marks.find(node);
        if (marked != marks.end()) {
            if (marked->second == Mark::Visiting)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict, "runtime-required dependency cycle detected",
                    node.format(), {}, "asset.dependency"));
            return Result<void>::success();
        }
        marks.emplace(node, Mark::Visiting);
        auto& targets = graph[node];
        std::sort(targets.begin(), targets.end());
        for (const auto& target : targets) {
            auto nested = visit(target);
            if (!nested) return nested;
        }
        marks[node] = Mark::Complete;
        return Result<void>::success();
    };
    for (const auto& id : local) {
        auto visited = visit(id);
        if (!visited) return Result<EvaDependencyValidation>::failure(visited.status());
    }
    std::sort(result.presentAssets.begin(), result.presentAssets.end());
    result.presentAssets.erase(std::unique(result.presentAssets.begin(), result.presentAssets.end()),
                               result.presentAssets.end());
    std::sort(result.omittedOptionalAssets.begin(), result.omittedOptionalAssets.end());
    result.omittedOptionalAssets.erase(
        std::unique(result.omittedOptionalAssets.begin(), result.omittedOptionalAssets.end()),
        result.omittedOptionalAssets.end());
    return Result<EvaDependencyValidation>::success(std::move(result));
}
}  // namespace eve::asset
