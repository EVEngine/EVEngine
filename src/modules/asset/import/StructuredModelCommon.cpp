#include "asset/import/StructuredModelCommon.h"
#include "asset/import/ImportCommon.h"

#include <cmath>
#include <limits>

namespace eve::asset_import::structured_detail {
Result<void> appendTriangle(asset::CanonicalMeshData& mesh, const std::array<std::array<double, 3>, 3>& triangle,
                            const AssetImportLimits& limits) {
    const auto vertices = mesh.positions.size() / 3;
    if (vertices + 3 > limits.maximumVerticesPerPrimitive ||
        mesh.indices.size() + 3 > limits.maximumIndicesPerPrimitive ||
        (vertices + 3) * 28 + 24 > limits.maximumDecodedBytes)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "structured model triangle budget exceeded", {}, {}, "asset.import"));
    for (const auto& point : triangle)
        for (double value : point)
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "nonfinite or overflowing model coordinate", {}, {}, "asset.import"));
    auto rounded = triangle;
    for (auto& point : rounded)
        for (auto& value : point) value = static_cast<float>(value);
    std::array<double, 3> a{}, b{}, normal{};
    for (std::size_t i = 0; i < 3; ++i) {
        a[i] = rounded[1][i] - rounded[0][i];
        b[i] = rounded[2][i] - rounded[0][i];
    }
    normal              = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const double length = std::hypot(normal[0], normal[1], normal[2]);
    if (!std::isfinite(length) || length == 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "degenerate model triangle", {}, {}, "asset.import"));
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            mesh.positions.push_back(float(triangle[vertex][axis]));
            mesh.normals.push_back(float(normal[axis] / length));
        }
        mesh.indices.push_back(static_cast<std::uint32_t>(vertices + vertex));
    }
    return Result<void>::success();
}

Result<PreparedAssetImport> packageModel(const ImportPackageIdentity& identity, std::string_view sourceName,
                                         std::string_view format, std::map<std::string, MeshPart> parts,
                                         const std::map<std::string, std::vector<std::uint8_t>>& sources,
                                         std::vector<ImportFinding> findings, const AssetImportLimits& limits) {
    using Output = Result<PreparedAssetImport>;
    if (parts.empty() || parts.size() * 2 + 1 > limits.maximumAssets || sourceName.empty() ||
        sourceName.size() > limits.maximumStringBytes)
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "model must contain bounded triangle parts and a source name", {}, {},
                                                 "asset.import"));
    auto manifest = detail::baseManifest(identity, "eve." + std::string(format));
    if (!manifest) return Output::failure(manifest.status());
    PreparedAssetImport out;
    out.manifest        = std::move(manifest).takeValue();
    out.findings        = std::move(findings);
    const auto sceneId  = identity.packageId.child(std::string(format) + ":scene");
    auto       sceneRef = detail::assetRef(sceneId);
    if (!sceneRef) return Output::failure(sceneRef.status());
    auto addDefinition = [&](PersistentId id, std::string type, unsigned version,
                             Value::Object definition) -> Result<AssetRef> {
        auto reference = detail::assetRef(id);
        if (!reference) return reference;
        auto json = Value(std::move(definition)).toJson();
        if (!json) return Result<AssetRef>::failure(json.status());
        const auto                path = "assets/" + id.format() + "/asset.json";
        std::vector<std::uint8_t> bytes(json.value().begin(), json.value().end());
        out.manifest.assets.push_back({reference.value(),
                                       std::move(type),
                                       SchemaVersion(version),
                                       path,
                                       detail::sha256(bytes),
                                       {"source:" + std::string(format)}});
        out.entries.push_back({path, std::move(bytes)});
        return reference;
    };
    Value::Array nodes, renderers;
    const auto   rootId = sceneId.child("root");
    auto         node   = [](PersistentId id, std::int64_t index, std::int64_t parent, std::string name) -> Value {
        return Value::Object{{"objectId", id.format()},           {"sourceFileId", index},
                                       {"parentSourceFileId", parent},      {"name", std::move(name)},
                                       {"position", Value::Array{0, 0, 0}}, {"rotation", Value::Array{0, 0, 0, 1}},
                                       {"scale", Value::Array{1, 1, 1}}};
    };
    nodes.push_back(node(rootId, 1, 0, std::string(sourceName)));
    std::uint64_t totalBytes = 0;
    for (auto& [name, part] : parts) {
        asset::CanonicalMeshLimits meshLimits{limits.maximumVerticesPerPrimitive, limits.maximumIndicesPerPrimitive,
                                              limits.maximumDecodedBytes};
        auto                       blob = asset::encodeCanonicalMesh(part.mesh, meshLimits);
        if (!blob) return Output::failure(blob.status());
        if (blob.value().size() > limits.maximumDecodedBytes - totalBytes)
            return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                     "model exceeds total decoded budget", {}, {}, "asset.import"));
        totalBytes += blob.value().size();
        const auto            meshId   = identity.packageId.child(std::string(format) + ":mesh:" + name);
        const auto            blobPath = "assets/" + meshId.format() + "/mesh.bin";
        std::array<double, 3> minimum{}, maximum{};
        for (std::size_t axis = 0; axis < 3; ++axis) minimum[axis] = maximum[axis] = part.mesh.positions[axis];
        for (std::size_t i = 0; i < part.mesh.positions.size(); ++i) {
            minimum[i % 3] = std::min(minimum[i % 3], double(part.mesh.positions[i]));
            maximum[i % 3] = std::max(maximum[i % 3], double(part.mesh.positions[i]));
        }
        auto meshRef = addDefinition(meshId, "eve.mesh", 2,
                                     {{"schema", "eve.mesh"},
                                      {"schemaVersion", 2},
                                      {"topology", "triangles"},
                                      {"vertexCount", std::int64_t(part.mesh.positions.size() / 3)},
                                      {"indexCount", std::int64_t(part.mesh.indices.size())},
                                      {"positions", true},
                                      {"normals", true},
                                      {"texcoordSets", Value::Array{}},
                                      {"coordinateSystem", "right-handed-x-right-y-up-minus-z-forward"},
                                      {"unit", "meter"},
                                      {"frontFace", "counter-clockwise"},
                                      {"boundsMin", Value::Array{minimum[0], minimum[1], minimum[2]}},
                                      {"boundsMax", Value::Array{maximum[0], maximum[1], maximum[2]}},
                                      {"blob", blobPath}});
        if (!meshRef) return Output::failure(meshRef.status());
        out.entries.push_back({blobPath, std::move(blob).takeValue()});
        const auto materialId = identity.packageId.child(std::string(format) + ":material:" + name);
        auto       materialRef =
            addDefinition(materialId, "eve.material", 15,
                          {{"schema", "eve.material"},
                           {"schemaVersion", 15},
                           {"alphaToCoverage", false},
                           {"shadingModel", "pbr"},
                           {"surfaceMode", part.color[3] < 1 ? "transparent" : "opaque"},
                           {"blendMode", "alpha"},
                           {"baseColor", Value::Array{part.color[0], part.color[1], part.color[2], part.color[3]}},
                           {"metallic", 0.0},
                           {"roughness", 0.45},
                           {"doubleSided", true}});
        if (!materialRef) return Output::failure(materialRef.status());
        const auto objectId = sceneId.child(name);
        nodes.push_back(node(objectId, std::int64_t(nodes.size() + 1), 1, name));
        renderers.emplace_back(Value::Object{{"objectId", objectId.format()},
                                             {"mesh", meshRef.value().format()},
                                             {"material", materialRef.value().format()},
                                             {"enabled", true},
                                             {"castShadows", true},
                                             {"receiveShadows", true}});
        const auto prefix = "renderers[" + std::to_string(renderers.size() - 1) + "]";
        out.manifest.dependencies.push_back({sceneRef.value(),
                                             meshRef.value(),
                                             asset::EvaDependencyKind::RuntimeRequired,
                                             prefix + ".mesh",
                                             {},
                                             "eve.mesh/2",
                                             {}});
        out.manifest.dependencies.push_back({sceneRef.value(),
                                             materialRef.value(),
                                             asset::EvaDependencyKind::RuntimeRequired,
                                             prefix + ".material",
                                             {},
                                             "eve.material/15",
                                             {}});
        out.manifest.entrypoints.emplace("mesh:" + name, meshRef.value());
        out.sourceMappings.push_back({name, meshRef.value()});
    }
    auto scene = addDefinition(sceneId, "eve.scene-template", 3,
                               {{"schema", "eve.scene-template"},
                                {"schemaVersion", 3},
                                {"coordinateSystem", "right-handed-x-right-y-up-minus-z-forward"},
                                {"nodes", std::move(nodes)},
                                {"renderers", std::move(renderers)}});
    if (!scene) return Output::failure(scene.status());
    out.manifest.entrypoints.emplace("default", scene.value());
    Value::Array  sourceIndex;
    std::uint64_t sourceBytes = 0;
    for (const auto& [name, bytes] : sources) {
        if (bytes.size() > limits.maximumSourceBytes - sourceBytes)
            return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "model sources exceed budget", {},
                                                     {}, "asset.import"));
        sourceBytes += bytes.size();
        const auto path = "sources/" + std::to_string(sourceIndex.size()) + ".txt";
        sourceIndex.emplace_back(Value::Object{{"name", name}, {"path", path}, {"hash", detail::sha256(bytes)}});
        out.entries.push_back({path, bytes});
    }
    out.manifest.provenance["sourceName"] = std::string(sourceName);
    out.manifest.provenance["sources"]    = std::move(sourceIndex);
    auto report                           = detail::finalizeImportReport(out, identity, format, "1");
    if (!report) return Output::failure(report.status());
    return Output::success(std::move(out));
}
}  // namespace eve::asset_import::structured_detail
