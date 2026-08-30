#include "asset_import/UnrealImporter.h"

#include "asset_import/ImportCommon.h"

#include <cmath>
#include <set>

namespace eve::asset_import {
namespace {

const Value* member(const Value::Object& object, std::string_view key) {
    const auto found = object.find(std::string(key));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::string> requiredString(const Value::Object& object, std::string_view key, std::string path) {
    const Value* value = member(object, key);
    if (!value || !value->isString() || value->asString().empty())
        return detail::failure<std::string>(DiagnosticCode::ParseError,
                                            "Unreal adapter field must be a non-empty string", std::move(path));
    return Result<std::string>::success(value->asString());
}

Result<std::uint32_t> requiredUint32(const Value::Object& object, std::string_view key, std::string path) {
    const Value* value = member(object, key);
    if (!value || !value->isInt64() || value->asInt() <= 0 ||
        value->asInt() > std::numeric_limits<std::uint32_t>::max())
        return detail::failure<std::uint32_t>(DiagnosticCode::ParseError,
                                              "Unreal adapter field must be a positive uint32", std::move(path));
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value->asInt()));
}

Result<float> number(const Value& value, std::string path) {
    double result = 0;
    if (value.isDouble()) result = value.asDouble();
    else if (value.isInt64()) result = static_cast<double>(value.asInt());
    else return detail::failure<float>(DiagnosticCode::ParseError, "Unreal numeric field is invalid", std::move(path));
    if (!std::isfinite(result) || result < -std::numeric_limits<float>::max() ||
        result > std::numeric_limits<float>::max())
        return detail::failure<float>(DiagnosticCode::ParseError, "Unreal numeric field is non-finite", std::move(path));
    return Result<float>::success(static_cast<float>(result));
}

template <std::size_t Count>
Result<std::array<float, Count>> numberArray(const Value* value, std::string path) {
    const auto* array = value ? value->getIf<Value::Array>() : nullptr;
    if (!array || array->size() != Count)
        return detail::failure<std::array<float, Count>>(DiagnosticCode::ParseError,
                                                        "Unreal vector field has the wrong shape", std::move(path));
    std::array<float, Count> result{};
    for (std::size_t index = 0; index < Count; ++index) {
        auto parsed = number((*array)[index], path + "[" + std::to_string(index) + "]");
        if (!parsed) return Result<std::array<float, Count>>::failure(parsed.status());
        result[index] = parsed.value();
    }
    return Result<std::array<float, Count>>::success(result);
}

bool safeProjectPath(std::string_view path, const AssetImportLimits& limits) {
    if (path.empty() || path.size() > limits.maximumStringBytes || path.front() == '/' ||
        path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos) return false;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= path.size(); ++index) {
        if (index != path.size() && path[index] != '/') continue;
        const auto segment = path.substr(start, index - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        start = index + 1;
    }
    return true;
}

std::array<float, 4> unrealQuaternionToCanonical(const std::array<float, 4>& source) {
    const float x = source[0], y = source[1], z = source[2], w = source[3];
    const float r[3][3] = {
        {1 - 2*y*y - 2*z*z, 2*x*y - 2*z*w,     2*x*z + 2*y*w},
        {2*x*y + 2*z*w,     1 - 2*x*x - 2*z*z, 2*y*z - 2*x*w},
        {2*x*z - 2*y*w,     2*y*z + 2*x*w,     1 - 2*x*x - 2*y*y}
    };
    // C maps UE (X forward,Y right,Z up) to canonical (X right,Y up,-Z forward).
    const float c[3][3] = {{0,1,0},{0,0,1},{-1,0,0}};
    float cr[3][3]{};
    float out[3][3]{};
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
        for (int k = 0; k < 3; ++k) cr[row][col] += c[row][k] * r[k][col];
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
        for (int k = 0; k < 3; ++k) out[row][col] += cr[row][k] * c[col][k];
    std::array<float, 4> q{};
    const float trace = out[0][0] + out[1][1] + out[2][2];
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2;
        q = {(out[2][1] - out[1][2]) / s, (out[0][2] - out[2][0]) / s,
             (out[1][0] - out[0][1]) / s, 0.25f * s};
    } else if (out[0][0] > out[1][1] && out[0][0] > out[2][2]) {
        const float s = std::sqrt(1 + out[0][0] - out[1][1] - out[2][2]) * 2;
        q = {0.25f*s, (out[0][1]+out[1][0])/s, (out[0][2]+out[2][0])/s,
             (out[2][1]-out[1][2])/s};
    } else if (out[1][1] > out[2][2]) {
        const float s = std::sqrt(1 + out[1][1] - out[0][0] - out[2][2]) * 2;
        q = {(out[0][1]+out[1][0])/s, 0.25f*s, (out[1][2]+out[2][1])/s,
             (out[0][2]-out[2][0])/s};
    } else {
        const float s = std::sqrt(1 + out[2][2] - out[0][0] - out[1][1]) * 2;
        q = {(out[0][2]+out[2][0])/s, (out[1][2]+out[2][1])/s, 0.25f*s,
             (out[1][0]-out[0][1])/s};
    }
    return q;
}

Result<void> preserveSource(PreparedAssetImport& output, const UnrealProjectImportRequest& request,
                            std::string_view path) {
    if (!safeProjectPath(path, request.limits))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unreal source path is unsafe", std::string(path), {},
                                                       "asset.import.unreal"));
    const auto found = request.files.find(std::string(path));
    if (found == request.files.end())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                       "referenced Unreal source file was not supplied",
                                                       std::string(path), {}, "asset.import.unreal"));
    output.entries.push_back({"sources/unreal/" + std::string(path), found->second});
    return Result<void>::success();
}

}  // namespace

Result<PreparedAssetImport> prepareUnrealM4Import(const UnrealProjectImportRequest& request) {
    if (!safeProjectPath(request.descriptorPath, request.limits))
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                    "Unreal adapter descriptor path is unsafe");
    const auto descriptorFile = request.files.find(request.descriptorPath);
    if (descriptorFile == request.files.end() || descriptorFile->second.size() > request.limits.maximumSourceBytes)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::NotFound,
                                                    "Unreal adapter descriptor was not supplied", request.descriptorPath);
    std::uint64_t sourceTotal = 0;
    for (const auto& [path, bytes] : request.files) {
        if (!safeProjectPath(path, request.limits) || bytes.size() > request.limits.maximumSourceBytes ||
            sourceTotal > request.limits.maximumSourceBytes - bytes.size())
            return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                        "Unreal source path or total budget is invalid", path);
        sourceTotal += bytes.size();
    }
    const std::string json(descriptorFile->second.begin(), descriptorFile->second.end());
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<PreparedAssetImport>::failure(parsed.status());
    const auto* root = parsed.value().getIf<Value::Object>();
    if (!root) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                           "Unreal adapter descriptor root must be an object");
    auto schema = requiredString(*root, "schema", "$.schema");
    auto version = requiredUint32(*root, "schemaVersion", "$.schemaVersion");
    if (!schema || !version) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                                         "Unreal descriptor envelope is invalid");
    if (schema.value() != "eve.unreal-landscape-import" || version.value() != 1)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::UnknownVersion,
                                                    "unsupported Unreal adapter descriptor schema/version");
    const auto* landscapeValue = member(*root, "landscape");
    const auto* landscape = landscapeValue ? landscapeValue->getIf<Value::Object>() : nullptr;
    if (!landscape) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                                "Unreal descriptor landscape is required");
    auto sourceWidth = requiredUint32(*landscape, "width", "$.landscape.width");
    auto sourceHeight = requiredUint32(*landscape, "height", "$.landscape.height");
    auto heightmap = requiredString(*landscape, "heightmap", "$.landscape.heightmap");
    auto scale = numberArray<3>(member(*landscape, "scaleCm"), "$.landscape.scaleCm");
    if (!sourceWidth || !sourceHeight || !heightmap || !scale)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                    "Unreal Landscape fields are invalid");
    if (sourceWidth.value() < 2 || sourceHeight.value() < 2 || scale.value()[0] <= 0 ||
        scale.value()[1] <= 0 || scale.value()[2] <= 0 || !safeProjectPath(heightmap.value(), request.limits))
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                    "Unreal Landscape dimensions/scale/path are invalid");
    const auto heightFile = request.files.find(heightmap.value());
    const std::uint64_t sampleCount = std::uint64_t(sourceWidth.value()) * sourceHeight.value();
    if (heightFile == request.files.end() || sampleCount > request.limits.maximumDecodedBytes / sizeof(float) ||
        heightFile->second.size() != sampleCount * 2)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                    "Unreal Landscape R16 byte count is invalid", heightmap.value());
    CanonicalTerrainInput terrain;
    // UE X rows become canonical -Z rows; UE Y columns become canonical +X columns.
    terrain.width = sourceWidth.value();
    terrain.height = sourceHeight.value();
    terrain.spacingX = scale.value()[1] / 100.0f;
    terrain.spacingZ = scale.value()[0] / 100.0f;
    terrain.heightsMeters.resize(sampleCount);
    for (std::uint32_t sourceX = 0; sourceX < sourceHeight.value(); ++sourceX) {
        for (std::uint32_t sourceY = 0; sourceY < sourceWidth.value(); ++sourceY) {
            const std::size_t sourceIndex = std::size_t(sourceX) * sourceWidth.value() + sourceY;
            const std::uint16_t encoded = std::uint16_t(heightFile->second[sourceIndex * 2]) |
                                          (std::uint16_t(heightFile->second[sourceIndex * 2 + 1]) << 8);
            const std::size_t canonicalRow = sourceHeight.value() - 1 - sourceX;
            const std::size_t canonicalColumn = sourceY;
            terrain.heightsMeters[canonicalRow * terrain.width + canonicalColumn] =
                (float(encoded) - 32768.0f) / 128.0f * scale.value()[2] / 100.0f;
        }
    }
    terrain.sourceTransform = {{"source", Value("ue-left-handed-x-forward-y-right-z-up-centimeters")},
                               {"conversion", Value("canonical=(ueY,ueZ,-ueX)/100")},
                               {"landscapeHeight", Value("(r16-32768)/128*zScaleCm/100")}};

    std::set<std::string> preserved;
    const Value* layersValue = member(*root, "layers");
    const auto* layers = layersValue ? layersValue->getIf<Value::Array>() : nullptr;
    if (layers) for (std::size_t index = 0; index < layers->size(); ++index) {
        const auto* layer = (*layers)[index].getIf<Value::Object>();
        if (!layer) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                                "M4 layer must be an object");
        auto name = requiredString(*layer, "name", "$.layers[].name");
        if (!name) return Result<PreparedAssetImport>::failure(name.status());
        CanonicalTerrainLayer output;
        output.name = name.value(); output.normalConvention = "directx";
        if (const Value* diffuse = member(*layer, "diffuse")) {
            if (!diffuse->isString() || !safeProjectPath(diffuse->asString(), request.limits))
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "M4 diffuse path is invalid");
            output.diffuseSource = "sources/unreal/" + diffuse->asString(); preserved.emplace(diffuse->asString());
        }
        if (const Value* normal = member(*layer, "normal")) {
            if (!normal->isString() || !safeProjectPath(normal->asString(), request.limits))
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "M4 normal path is invalid");
            output.normalSource = "sources/unreal/" + normal->asString(); preserved.emplace(normal->asString());
        }
        if (const Value* weight = member(*layer, "weight")) {
            if (!weight->isString() || !safeProjectPath(weight->asString(), request.limits))
                return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "M4 weight path is invalid");
            output.weightSource = "sources/unreal/" + weight->asString(); preserved.emplace(weight->asString());
        }
        if (const Value* tile = member(*layer, "tileSizeCm")) {
            auto parsedTile = number(*tile, "$.layers[].tileSizeCm");
            if (!parsedTile || parsedTile.value() <= 0) return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                                                                   "M4 tile size is invalid");
            output.tileSizeMeters = parsedTile.value() / 100.0f;
        }
        terrain.layers.push_back(std::move(output));
    }
    const Value* grassValue = member(*root, "grass");
    const auto* grass = grassValue ? grassValue->getIf<Value::Array>() : nullptr;
    if (grass) for (std::size_t index = 0; index < grass->size(); ++index) {
        const auto* rule = (*grass)[index].getIf<Value::Object>();
        if (!rule) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "M4 grass rule must be an object");
        auto id = requiredString(*rule, "id", "$.grass[].id");
        auto prototype = requiredString(*rule, "prototype", "$.grass[].prototype");
        auto layer = requiredString(*rule, "layer", "$.grass[].layer");
        const Value* densityValue = member(*rule, "densityPerSquareMeter");
        if (!id || !prototype || !layer || !densityValue)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "M4 grass fields are incomplete");
        auto density = number(*densityValue, "$.grass[].densityPerSquareMeter");
        if (!density) return Result<PreparedAssetImport>::failure(density.status());
        CanonicalScatterRule output{id.value(), prototype.value(), layer.value(), density.value(), 0, 1.57079632679f, 0};
        if (const Value* seed = member(*rule, "seed")) {
            if (!seed->isInt64() || seed->asInt() < 0)
                return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "M4 grass seed is invalid");
            output.seed = static_cast<std::uint64_t>(seed->asInt());
        }
        terrain.scatterRules.push_back(std::move(output));
    }
    const Value* instancesValue = member(*root, "instances");
    const auto* instances = instancesValue ? instancesValue->getIf<Value::Array>() : nullptr;
    if (instances) for (const auto& value : *instances) {
        const auto* instance = value.getIf<Value::Object>();
        if (!instance) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "UE instance must be an object");
        auto prototype = requiredString(*instance, "prototype", "$.instances[].prototype");
        auto position = numberArray<3>(member(*instance, "positionCm"), "$.instances[].positionCm");
        auto rotation = numberArray<4>(member(*instance, "rotationQuat"), "$.instances[].rotationQuat");
        auto scaleValue = numberArray<3>(member(*instance, "scale"), "$.instances[].scale");
        if (!prototype || !position || !rotation || !scaleValue)
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "UE instance transform is invalid");
        CanonicalTerrainInstance output;
        output.prototype = prototype.value();
        output.position[0] = position.value()[1] / 100.0f;
        output.position[1] = position.value()[2] / 100.0f;
        output.position[2] = -position.value()[0] / 100.0f;
        const auto q = unrealQuaternionToCanonical(rotation.value());
        std::copy(q.begin(), q.end(), output.rotation);
        output.scale[0] = scaleValue.value()[1]; output.scale[1] = scaleValue.value()[2];
        output.scale[2] = scaleValue.value()[0];
        terrain.instances.push_back(std::move(output));
    }

    auto prepared = prepareCanonicalTerrainImport(request.package, terrain, "eve.unreal-m4/1", request.limits);
    if (!prepared) return Result<PreparedAssetImport>::failure(prepared.status());
    PreparedAssetImport output = std::move(prepared).takeValue();
    output.manifest.provenance["sourceEngine"] = Value("unreal-engine-5");
    output.manifest.provenance["adapterDescriptor"] = Value(request.descriptorPath);
    output.manifest.provenance["sourceHash"] = Value(detail::sha256(descriptorFile->second));
    output.entries.push_back({"sources/unreal/adapter.json", descriptorFile->second});
    preserved.emplace(heightmap.value());
    for (const auto& path : preserved) {
        auto stored = preserveSource(output, request, path);
        if (!stored) return Result<PreparedAssetImport>::failure(stored.status());
    }
    output.findings.push_back({heightmap.value(), "Landscape.heightfield", ImportDisposition::Translated,
                               "UE R16 height and centimetre scale converted to canonical metres"});
    if (layers) output.findings.push_back({request.descriptorPath, "M4.MaterialInstance.layers",
                                           ImportDisposition::Translated,
                                           "M4 layer textures, tiling and DirectX normal convention mapped"});
    if (grass) output.findings.push_back({request.descriptorPath, "LandscapeGrassType",
                                          ImportDisposition::Translated,
                                          "grass density/layer rules mapped to deterministic PCG"});
    if (const Value* unsupportedValue = member(*root, "unsupported")) {
        const auto* unsupported = unsupportedValue->getIf<Value::Array>();
        if (!unsupported) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                                      "unsupported must be an array");
        for (const auto& feature : *unsupported) {
            if (!feature.isString()) return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                                                 "unsupported feature must be a string");
            output.findings.push_back({request.descriptorPath, feature.asString(), ImportDisposition::Unsupported,
                                       "native Unreal behavior is not executed by the data-only adapter"});
        }
    }
    for (const auto& asset : output.manifest.assets) {
        std::string sourceObject = request.descriptorPath + "#" + asset.type;
        if (asset.type == "eve.terrain") sourceObject = heightmap.value();
        else if (asset.type == "eve.terrain-material") sourceObject = request.descriptorPath + "#layers";
        else if (asset.type == "eve.pcg-graph") sourceObject = request.descriptorPath + "#grass";
        else if (asset.type == "eve.instance-set") sourceObject = request.descriptorPath + "#instances";
        output.sourceMappings.push_back({std::move(sourceObject), asset.asset});
    }
    auto report = detail::finalizeImportReport(output, request.package, "unreal-engine", "5.x",
                                               {{"descriptorSchema", Value("eve.unreal-landscape-import/1")}});
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    return Result<PreparedAssetImport>::success(std::move(output));
}

}  // namespace eve::asset_import
