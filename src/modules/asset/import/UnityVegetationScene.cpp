#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>

namespace eve::asset_import {
namespace {
constexpr std::string_view controlGuid = "504b8163e405ec24bab88beccbe43d42";
constexpr std::string_view detailsGuid = "f3cf74665658f2249a2a56df92106e94";
constexpr std::string_view motionGuid  = "c879218671cebf84d9385947d79161cd";
constexpr std::string_view volumeGuid  = "eaf239e2056652741929c4183efd08b2";
constexpr std::string_view elementGuid = "2d447c3f2fda29f41a5b7e81398c0ab0";

struct SceneComponent {
    std::int64_t fileId = 0;
    std::string body;
};

std::vector<SceneComponent> components(std::string_view source, std::string_view scriptGuid) {
    std::vector<SceneComponent> result;
    const std::regex document(
        R"((?:^|\n)--- !u!114 &(-?[0-9]+)[^\n]*\nMonoBehaviour:\s*\n([\s\S]*?)(?=\n--- !u!|$))");
    const std::regex script("(?:^|\\n)  m_Script: *\\{fileID: *11500000, guid: *" +
                            std::string(scriptGuid) + R"(, type: *3\})");
    for (std::cregex_iterator it(source.data(), source.data() + source.size(), document), end; it != end; ++it) {
        const auto body = (*it)[2].str();
        if (!std::regex_search(body, script)) continue;
        try {
            result.push_back({std::stoll((*it)[1].str()), body});
        } catch (...) {
            return {};
        }
    }
    return result;
}

std::optional<std::string> component(std::string_view source, std::string_view scriptGuid) {
    const std::regex document(R"((?:^|\n)--- !u!114 &-?[0-9]+[^\n]*\nMonoBehaviour:\s*\n([\s\S]*?)(?=\n--- !u!|$))");
    const std::regex script("(?:^|\\n)  m_Script: *\\{fileID: *11500000, guid: *" +
                            std::string(scriptGuid) + R"(, type: *3\})");
    for (std::cregex_iterator it(source.data(), source.data() + source.size(), document), end; it != end; ++it) {
        const auto body = (*it)[1].str();
        if (std::regex_search(body, script)) return body;
    }
    return {};
}

Result<double> number(std::string_view body, std::string_view name, double fallback) {
    std::cmatch match;
    const std::regex field("(?:^|\\n)  " + std::string(name) + R"(: *([^\r\n]+))");
    if (!std::regex_search(body.data(), body.data() + body.size(), match, field))
        return Result<double>::success(fallback);
    try {
        std::size_t end = 0;
        const auto token = match[1].str();
        const auto value = std::stod(token, &end);
        if (end != token.size() || !std::isfinite(value)) throw std::invalid_argument("number");
        return Result<double>::success(value);
    } catch (...) {
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::ParseError, "TVE scene number is invalid",
                                                         std::string(name), {}, "asset.import"));
    }
}

Result<Value> array(std::string_view body,
                    std::initializer_list<std::pair<std::string_view, double>> fields) {
    Value::Array result;
    for (const auto& [name, fallback] : fields) {
        auto value = number(body, name, fallback);
        if (!value) return Result<Value>::failure(value.status());
        result.emplace_back(value.value());
    }
    return Result<Value>::success(Value(std::move(result)));
}

Result<Value> color(std::string_view body, std::string_view name, std::array<double, 4> fallback) {
    std::cmatch match;
    const std::regex field("(?:^|\\n)  " + std::string(name) +
                           R"(: *\{r: *([^,]+), *g: *([^,]+), *b: *([^,]+), *a: *([^}]+)\})");
    if (!std::regex_search(body.data(), body.data() + body.size(), match, field))
        return Result<Value>::success(Value::array({fallback[0], fallback[1], fallback[2], fallback[3]}));
    Value::Array result;
    for (std::size_t index = 1; index <= 4; ++index) {
        try {
            std::size_t end = 0;
            const auto token = match[index].str();
            const auto value = std::stod(token, &end);
            if (end != token.size() || !std::isfinite(value)) throw std::invalid_argument("color");
            result.emplace_back(value);
        } catch (...) {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "TVE scene color is invalid",
                                                            std::string(name), {}, "asset.import"));
        }
    }
    return Result<Value>::success(Value(std::move(result)));
}

std::string guid(std::string_view body, std::string_view name) {
    std::cmatch match;
    const std::regex field("(?:^|\\n)  " + std::string(name) +
                           R"(: *\{fileID: *-?[0-9]+, guid: *([0-9a-fA-F]{32}), type: *[23]\})");
    return std::regex_search(body.data(), body.data() + body.size(), match, field)
               ? unity_detail::foldAscii(match[1].str())
               : std::string{};
}

Result<Value> details(std::string_view body) {
    auto layers = array(body, {{"layerColors", 0}, {"layerExtras", 0}, {"layerMotion", 0}});
    auto global = array(body, {{"globalColor", 1}, {"globalAlpha", 1}, {"globalOverlay", 1}, {"globalWetness", 1}});
    auto colorMask = array(body, {{"colorMaskMin", .4}, {"colorMaskMax", .6}});
    auto overlayMask = array(body, {{"overlayMaskMin", .4}, {"overlayMaskMax", .6}});
    auto perspective = array(body, {{"perspectivePush", 0}, {"perspectiveNoise", 0}, {"perspectiveAngle", 1}});
    auto highlight = color(body, "motionHighlight", {1, 1, 1, 1});
    auto bending = array(body, {{"bendingAmplitude", 1}, {"bendingSpeed", 6}, {"bendingScale", 2}});
    auto flutter = array(body, {{"flutterAmplitude", .1}, {"flutterSpeed", 20}, {"flutterScale", 2}});
    auto alpha = number(body, "alphaTreshold", .5), interaction = number(body, "interactionAmplitude", 1);
    const bool layersOk = bool(layers), globalOk = bool(global), colorMaskOk = bool(colorMask),
               overlayMaskOk = bool(overlayMask), perspectiveOk = bool(perspective), highlightOk = bool(highlight),
               bendingOk = bool(bending), flutterOk = bool(flutter), alphaOk = bool(alpha),
               interactionOk = bool(interaction);
    if (!layersOk || !globalOk || !colorMaskOk || !overlayMaskOk || !perspectiveOk || !highlightOk || !bendingOk ||
        !flutterOk || !alphaOk || !interactionOk)
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "TVE Global Details is malformed", {}, {}, "asset.import"));
    auto highlightValue = std::move(highlight).takeValue();
    highlightValue.getIf<Value::Array>()->pop_back();
    return Result<Value>::success(Value::object({
        {"layers", std::move(layers).takeValue()}, {"global", std::move(global).takeValue()},
        {"colorMask", std::move(colorMask).takeValue()}, {"overlayMask", std::move(overlayMask).takeValue()},
        {"alphaThreshold", alpha.value()}, {"perspective", std::move(perspective).takeValue()},
        {"motionHighlight", std::move(highlightValue)}, {"bending", std::move(bending).takeValue()},
        {"flutter", std::move(flutter).takeValue()}, {"interactionAmplitude", interaction.value()}}));
}

Result<Value> control(std::string_view body) {
    auto tint = color(body, "globalColor", {.5, .5, .5, 0});
    auto overlay = color(body, "overlayColor", {1, 1, 1, 1});
    auto values = array(body, {{"seasonControl", 2}, {"globalAlpha", 1}, {"globalOverlay", 0},
                                {"globalWetness", 0}, {"globalEmissive", 1}, {"globalSubsurface", 1},
                                {"globalSizeFade", 1}, {"overlaySmoothness", .5}, {"overlayNormalScale", .5},
                                {"overlaySubsurface", .5}, {"overlayScale", 1}, {"wetnessContrast", .5},
                                {"wetnessNormalScale", .5}, {"noiseTextureTilling", 1},
                                {"proximityDistanceFade", 1}, {"sizeFadeDistanceBias", 1},
                                {"defaultConformHeight", 0}});
    const bool tintOk = bool(tint), overlayOk = bool(overlay), valuesOk = bool(values);
    if (!tintOk || !overlayOk || !valuesOk)
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "TVE Global Control is malformed", {}, {}, "asset.import"));
    return Result<Value>::success(Value::object({
        {"values", std::move(values).takeValue()}, {"globalColor", std::move(tint).takeValue()},
        {"overlayColor", std::move(overlay).takeValue()}, {"overlayAlbedoGuid", guid(body, "overlayAlbedo")},
        {"overlayNormalGuid", guid(body, "overlayNormal")}, {"noiseTextureGuid", guid(body, "noiseTexture3D")}}));
}

Result<Value> motion(std::string_view body) {
    auto values = array(body, {{"windPower", .5}, {"noiseTilling", 1}, {"motionBending", 1},
                                {"motionBranch", 1}, {"motionFlutter", 1}, {"motionSpeed", 1},
                                {"useAnimatedMotionTime", 1}, {"motionFadeDistance", 100}});
    if (!values) return Result<Value>::failure(values.status());
    return Result<Value>::success(Value::object({{"values", std::move(values).takeValue()},
                                                 {"noiseTextureGuid", guid(body, "noiseTexture")}}));
}

struct SceneTransform {
    std::int64_t parent = 0;
    std::int64_t gameObject = 0;
    std::array<double, 4> rotation{0, 0, 0, 1};
    std::array<double, 3> position{0, 0, 0}, scale{1, 1, 1};
};

std::optional<std::int64_t> fileId(std::string_view body, std::string_view name) {
    const std::string field = "  " + std::string(name) + ":";
    auto position = body.find(field);
    while (position != std::string_view::npos && position != 0 && body[position - 1] != '\n')
        position = body.find(field, position + field.size());
    if (position == std::string_view::npos) return {};
    position = body.find("fileID:", position + field.size());
    if (position == std::string_view::npos) return {};
    position += 7;
    while (position < body.size() && body[position] == ' ') ++position;
    const auto end = body.find_first_of(",}", position);
    try {
        std::size_t consumed = 0;
        const auto token = std::string(body.substr(position, end - position));
        const auto value = std::stoll(token, &consumed);
        if (consumed != token.size()) return {};
        return value;
    } catch (...) {
        return {};
    }
}

std::optional<double> rotationComponent(std::string_view line, std::string_view name) {
    const std::string field = std::string(name) + ":";
    auto position = line.find(field);
    if (position == std::string_view::npos) return {};
    position += field.size();
    while (position < line.size() && line[position] == ' ') ++position;
    const auto end = line.find_first_of(",}", position);
    try {
        std::size_t consumed = 0;
        const auto token = std::string(line.substr(position, end - position));
        const auto value = std::stod(token, &consumed);
        if (consumed != token.size() || !std::isfinite(value)) return {};
        return value;
    } catch (...) {
        return {};
    }
}

std::optional<std::array<double, 3>> vector3Field(std::string_view body, std::string_view name) {
    const std::string field = "  " + std::string(name) + ":";
    const auto start = body.find(field);
    if (start == std::string_view::npos) return {};
    const auto end = body.find('\n', start);
    const auto line = body.substr(start, end - start);
    const auto x = rotationComponent(line, "x"), y = rotationComponent(line, "y"), z = rotationComponent(line, "z");
    if (!x || !y || !z) return {};
    return std::array<double, 3>{*x, *y, *z};
}

std::map<std::int64_t, SceneTransform> transforms(std::string_view source) {
    std::map<std::int64_t, SceneTransform> result;
    constexpr std::string_view marker = "--- !u!4 &";
    std::size_t cursor = 0;
    while ((cursor = source.find(marker, cursor)) != std::string_view::npos) {
        if (cursor != 0 && source[cursor - 1] != '\n') {
            cursor += marker.size();
            continue;
        }
        const auto idBegin = cursor + marker.size();
        const auto headerEnd = source.find('\n', idBegin);
        if (headerEnd == std::string_view::npos) break;
        const auto bodyBegin = headerEnd + 1;
        if (!source.substr(bodyBegin).starts_with("Transform:")) {
            cursor = bodyBegin;
            continue;
        }
        const auto bodyContent = source.find('\n', bodyBegin);
        if (bodyContent == std::string_view::npos) break;
        const auto next = source.find("\n--- !u!", bodyContent + 1);
        const auto body = source.substr(bodyContent + 1,
                                        next == std::string_view::npos ? source.size() - bodyContent - 1
                                                                      : next - bodyContent - 1);
        SceneTransform transform;
        try {
            transform.gameObject = fileId(body, "m_GameObject").value_or(0);
            transform.parent = fileId(body, "m_Father").value_or(0);
            transform.position = vector3Field(body, "m_LocalPosition").value_or(std::array<double, 3>{0, 0, 0});
            transform.scale = vector3Field(body, "m_LocalScale").value_or(std::array<double, 3>{1, 1, 1});
            const auto rotationStart = body.find("  m_LocalRotation:");
            if (rotationStart == std::string_view::npos) {
                cursor = next == std::string_view::npos ? source.size() : next + 1;
                continue;
            }
            const auto rotationEnd = body.find('\n', rotationStart);
            const auto line = body.substr(rotationStart, rotationEnd - rotationStart);
            const auto x = rotationComponent(line, "x"), y = rotationComponent(line, "y"),
                       z = rotationComponent(line, "z"), w = rotationComponent(line, "w");
            if (!x || !y || !z || !w) return {};
            transform.rotation = {*x, *y, *z, *w};
            result.emplace(std::stoll(std::string(source.substr(idBegin, headerEnd - idBegin))), transform);
        } catch (...) {
            return {};
        }
        cursor = next == std::string_view::npos ? source.size() : next + 1;
    }
    return result;
}

struct WorldTransform {
    std::array<double, 3> position{0, 0, 0}, scale{1, 1, 1};
    std::array<double, 4> rotation{0, 0, 0, 1};
};

std::array<double, 4> multiply(std::array<double, 4> lhs, std::array<double, 4> rhs);

Result<std::array<double, 4>> normalized(std::array<double, 4> value) {
    const double length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2] +
                                    value[3] * value[3]);
    if (!std::isfinite(length) || length <= 1e-12)
        return Result<std::array<double, 4>>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "TVE element Transform rotation is invalid", {}, {}, "asset.import"));
    for (auto& component : value) component /= length;
    return Result<std::array<double, 4>>::success(value);
}

std::array<double, 3> rotate(std::array<double, 4> q, std::array<double, 3> v) {
    const std::array<double, 4> p{v[0], v[1], v[2], 0};
    const std::array<double, 4> conjugate{-q[0], -q[1], -q[2], q[3]};
    const auto result = multiply(multiply(q, p), conjugate);
    return {result[0], result[1], result[2]};
}

Result<WorldTransform> worldTransform(const std::map<std::int64_t, SceneTransform>& sceneTransforms,
                                      std::int64_t gameObject) {
    auto found = std::find_if(sceneTransforms.begin(), sceneTransforms.end(),
                              [&](const auto& entry) { return entry.second.gameObject == gameObject; });
    if (found == sceneTransforms.end())
        return Result<WorldTransform>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "TVE element Transform is missing", {}, {}, "asset.import"));
    std::vector<const SceneTransform*> chain{&found->second};
    std::set<std::int64_t> visited{found->first};
    auto parent = found->second.parent;
    while (parent != 0) {
        const auto ancestor = sceneTransforms.find(parent);
        if (ancestor == sceneTransforms.end() || !visited.emplace(parent).second)
            return Result<WorldTransform>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "TVE element Transform hierarchy is invalid", {}, {}, "asset.import"));
        chain.push_back(&ancestor->second);
        parent = ancestor->second.parent;
    }
    WorldTransform world;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        auto localRotation = normalized((*it)->rotation);
        if (!localRotation) return Result<WorldTransform>::failure(localRotation.status());
        std::array<double, 3> local{(*it)->position[0] * world.scale[0], (*it)->position[1] * world.scale[1],
                                    (*it)->position[2] * world.scale[2]};
        local = rotate(world.rotation, local);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            world.position[axis] += local[axis];
            world.scale[axis] *= (*it)->scale[axis];
        }
        auto composed = normalized(multiply(world.rotation, localRotation.value()));
        if (!composed) return Result<WorldTransform>::failure(composed.status());
        world.rotation = std::move(composed).takeValue();
    }
    world.position[2] = -world.position[2];
    world.rotation[0] = -world.rotation[0];
    world.rotation[1] = -world.rotation[1];
    return Result<WorldTransform>::success(world);
}

std::optional<std::string_view> propertyBlock(std::string_view body, std::string_view name) {
    const std::string marker = "      prop: " + std::string(name);
    auto begin = body.find(marker);
    if (begin == std::string_view::npos) return {};
    begin = body.rfind("    - type:", begin);
    if (begin == std::string_view::npos) return {};
    auto end = body.find("\n    - type:", begin + 1);
    if (end == std::string_view::npos) end = body.size();
    return body.substr(begin, end - begin);
}

double materialNumber(std::string_view body, std::string_view name, double fallback) {
    const auto block = propertyBlock(body, name);
    if (!block) return fallback;
    const auto value = number(*block, "    value", fallback);
    return value ? value.value() : fallback;
}

Value materialVector(std::string_view body, std::string_view name, std::array<double, 4> fallback) {
    const auto block = propertyBlock(body, name);
    if (!block) return Value::array({fallback[0], fallback[1], fallback[2], fallback[3]});
    auto value = color(*block, "    vector", fallback);
    return value ? std::move(value).takeValue() : Value::array({fallback[0], fallback[1], fallback[2], fallback[3]});
}

std::string materialTexture(std::string_view body, std::string_view name) {
    const auto block = propertyBlock(body, name);
    return block ? guid(*block, "    texture") : std::string{};
}

Result<Value> materialProperties(std::string_view body) {
    Value::Array output;
    std::size_t cursor = 0;
    constexpr std::string_view marker = "    - type:";
    const std::regex namePattern(R"((?:^|\n)      prop: *([A-Za-z_][A-Za-z0-9_]{0,127})\s*(?:\r?\n|$))");
    while ((cursor = body.find(marker, cursor)) != std::string_view::npos) {
        const auto next = body.find("\n    - type:", cursor + marker.size());
        const auto block = body.substr(cursor, next == std::string_view::npos ? body.size() - cursor : next - cursor);
        std::cmatch name;
        if (!std::regex_search(block.data(), block.data() + block.size(), name, namePattern))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "TVE element property name is malformed", {}, {}, "asset.import"));
        auto type = number(block, "  - type", -1), value = number(block, "    value", 0);
        auto vector = color(block, "    vector", {0, 0, 0, 0});
        if (!type || !value || !vector || std::floor(type.value()) != type.value() || type.value() < 0 ||
            type.value() > 2)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                            "TVE element property value is malformed", name[1].str(),
                                                            {}, "asset.import"));
        output.push_back(Value::object({{"name", name[1].str()}, {"type", std::int64_t(type.value())},
                                        {"textureGuid", guid(block, "    texture")},
                                        {"vector", std::move(vector).takeValue()}, {"value", value.value()}}));
        if (output.size() > 256)
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "TVE element property count exceeds 256", {}, {}, "asset.import"));
        cursor = next == std::string_view::npos ? body.size() : next + 1;
    }
    return Result<Value>::success(Value(std::move(output)));
}

void collectTextureGuids(const Value& value, std::set<std::string>& output) {
    if (const auto* object = value.getIf<Value::Object>()) {
        for (const auto& [name, child] : *object) {
            if (name == "textureGuid" && child.isString() && !child.asString().empty()) output.insert(child.asString());
            collectTextureGuids(child, output);
        }
    } else if (const auto* array = value.getIf<Value::Array>()) {
        for (const auto& child : *array) collectTextureGuids(child, output);
    }
}

bool importedImageAvailable(const UnityProjectImportRequest& request, std::string_view targetGuid) {
    for (const auto& [path, bytes] : request.files) {
        if (!path.ends_with(".meta")) continue;
        const std::string meta(bytes.begin(), bytes.end());
        if (meta.find("guid: " + std::string(targetGuid)) == std::string::npos) continue;
        const auto sourcePath = path.substr(0, path.size() - 5);
        const auto source = request.files.find(sourcePath);
        if (source == request.files.end()) return false;
        const auto dot = sourcePath.find_last_of('.');
        const auto extension = dot == std::string::npos ? std::string{} : unity_detail::foldAscii(sourcePath.substr(dot));
        if (extension == ".png" || extension == ".tga" || extension == ".tif" || extension == ".tiff") return true;
        if (extension != ".asset") return false;
        const std::string text(source->second.begin(), source->second.end());
        return text.find("\nTexture2D:") != std::string::npos;
    }
    return false;
}

Result<void> bindTextureAssets(Value& value, const UnityProjectImportRequest& request) {
    if (auto* object = value.getIf<Value::Object>()) {
        const auto guidValue = object->find("textureGuid");
        if (guidValue != object->end() && guidValue->second.isString()) {
            std::string asset;
            if (!guidValue->second.asString().empty() && importedImageAvailable(request, guidValue->second.asString())) {
                auto reference = detail::assetRef(
                    request.package.packageId.child("unity:" + guidValue->second.asString()).child("image:default"));
                if (!reference) return Result<void>::failure(reference.status());
                asset = reference.value().format();
            }
            object->emplace("textureAsset", std::move(asset));
        }
        for (auto& [name, child] : *object) {
            (void)name;
            auto bound = bindTextureAssets(child, request);
            if (!bound) return bound;
        }
    } else if (auto* array = value.getIf<Value::Array>()) {
        for (auto& child : *array) {
            auto bound = bindTextureAssets(child, request);
            if (!bound) return bound;
        }
    }
    return Result<void>::success();
}

Result<Value> elements(std::string_view source, std::vector<ImportFinding>& findings, std::string_view path) {
    const std::map<std::string_view, std::pair<std::string_view, int>> shaders{
        {"1179777fe2a84944798f2df69a9616a5", {"color-effect", 0}},
        {"ebee0293d9bb4564889bd81c801438b7", {"color-map", 0}},
        {"da183c57da5cee64c9d1fd3e840427b6", {"color-noise", 0}},
        {"853be43322d350648a82ec17b27de5cf", {"color-tint", 0}},
        {"94b5d04d01c0973498bbb16cea8449ec", {"extras-alpha", 1}},
        {"47fd4a0e901c8844a9f5ae6bcdef7ca4", {"extras-emissive", 1}},
        {"b3820571eb3f04f4f855cc7161c165d7", {"extras-overlay", 1}},
        {"3a8db8eb5154b6c44940a6c8616408d8", {"extras-wetness", 1}},
        {"c2aed2f308142f94fa04326437ee0005", {"motion-advanced", 2}},
        {"8aca4d3e46e8b7b458ec65a9bb11f144", {"motion-interaction", 2}},
        {"c52053b6a008e1544bad0837b518fa94", {"motion-wind-power", 2}},
        {"1e5171ac7b63ed441960da6cf37146a2", {"vertex-conform-model", 3}},
        {"3f514efeb56cd5149a0f4c127eab8a8c", {"vertex-conform-simple", 3}},
        {"4bcfd77507de42d40ad7ce2543a44cd8", {"vertex-conform-terrain", 3}},
        {"387db0d42ba361645a8443beadc8bdd6", {"vertex-height-offset", 3}},
        {"abb24a67f2ce88d498809438c1104783", {"vertex-height", 3}},
        {"ab583b89d93bd5846883c513711e769a", {"vertex-orientation-model", 3}},
        {"74b163185090aaf41b8df93640d24c50", {"vertex-orientation-terrain", 3}},
        {"4776cc0409f0f4e4da64e5837bff4b0d", {"vertex-size", 3}},
    };
    const auto sceneTransforms = transforms(source);
    Value::Array output;
    std::size_t invalidMaterials = 0;
    for (const auto& component : components(source, elementGuid)) {
        const auto gameObject = fileId(component.body, "m_GameObject").value_or(0);
        auto world = worldTransform(sceneTransforms, gameObject);
        if (!world) return Result<Value>::failure(world.status());
        const auto shader = guid(component.body, "  shader");
        const auto shaderDefinition = shaders.find(shader);
        if (shaderDefinition == shaders.end()) { ++invalidMaterials; continue; }
        const std::string kind(shaderDefinition->second.first);
        const int channel = shaderDefinition->second.second;
        std::array<double, 4> main{1, 1, 1, 1};
        if (kind == "color-tint") main = {.5019608, .5019608, .5019608, 1};
        double layerMask = 1;
        if (propertyBlock(component.body, "_ElementLayerMask")) {
            layerMask = materialNumber(component.body, "_ElementLayerMask", 1);
        } else if (propertyBlock(component.body, "_ElementLayerValue")) {
            layerMask = materialNumber(component.body, "_ElementLayerValue", 1);
        } else {
            const auto legacyLayer = materialNumber(component.body, "_ElementLayer", 0);
            if (legacyLayer < 0 || legacyLayer > 8 || std::floor(legacyLayer) != legacyLayer)
                return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "TVE legacy element layer is outside 0..8",
                                                                std::string(path), {}, "asset.import"));
            layerMask = double(std::uint16_t(1u << std::uint32_t(legacyLayer)));
        }
        const auto seasonal = materialNumber(component.body, "_ElementMode", 0) >= .5;
        Value::Array seasons;
        for (int index = 1; index <= 4; ++index) {
            if (kind == "color-tint")
                seasons.push_back(materialVector(component.body, "_AdditionalColor" + std::to_string(index), main));
            else {
                const auto value = materialNumber(component.body, "_AdditionalValue" + std::to_string(index), 1);
                seasons.push_back(Value::array({value, value, value, 1}));
            }
        }
        const auto& transform = world.value();
        auto enabledValue = number(component.body, "m_Enabled", 1);
        auto visibilityValue = number(component.body, "customVisibility", -1);
        auto propertiesValue = materialProperties(component.body);
        if (!enabledValue || !visibilityValue || !propertiesValue)
            return Result<Value>::failure(!enabledValue      ? enabledValue.status()
                                          : !visibilityValue ? visibilityValue.status()
                                                             : propertiesValue.status());
        const double mainValue = materialNumber(component.body, "_MainValue", main[0]);
        Value channelValue = materialVector(component.body, "_MainColor", main);
        if (kind == "extras-overlay") channelValue = Value::array({1, 0, mainValue, 1});
        else if (kind == "motion-interaction") channelValue = Value::array({1, 0, 0, mainValue});
        else if (kind == "motion-wind-power") channelValue = Value::array({0, 0, mainValue, 1});
        else if (kind == "vertex-size") channelValue = Value::array({0, 0, 0, mainValue});
        output.push_back(Value::object({
            {"sourceFileId", component.fileId}, {"shaderGuid", shader}, {"kind", kind}, {"channel", channel},
            {"properties", std::move(propertiesValue).takeValue()},
            {"enabled", enabledValue.value() >= .5},
            {"visibility", std::int64_t(visibilityValue.value())},
            {"position", Value::array({transform.position[0], transform.position[1], transform.position[2]})},
            {"rotation", Value::array({transform.rotation[0], transform.rotation[1], transform.rotation[2], transform.rotation[3]})},
            {"scale", Value::array({std::abs(transform.scale[0]), std::abs(transform.scale[1]), std::abs(transform.scale[2])})},
            {"layers", std::int64_t(layerMask)}, {"intensity", materialNumber(component.body, "_ElementIntensity", 1)},
            {"value", std::move(channelValue)},
            {"seasonal", seasonal}, {"seasons", Value(std::move(seasons))},
            {"textureGuid", materialTexture(component.body, "_MainTex")},
            {"remap", Value::array({materialNumber(component.body, "_MainTexColorMinValue", 0),
                                      materialNumber(component.body, "_MainTexColorMaxValue", 1),
                                      materialNumber(component.body, "_MainTexAlphaMinValue", 0),
                                      materialNumber(component.body, "_MainTexAlphaMaxValue", 1),
                                      materialNumber(component.body, "_MainTexFallofMinValue", 0),
                                      materialNumber(component.body, "_MainTexFallofMaxValue", 0)})},
            {"blendRgb", std::int64_t(materialNumber(component.body, "_ElementBlendRGB", 0))},
            {"blendAlpha", std::int64_t(materialNumber(component.body, "_ElementBlendA", 0))},
            {"directionMode", std::int64_t(materialNumber(component.body, "_ElementDirectionMode", 20))},
            {"invertDirection", materialNumber(component.body, "_ElementInvertMode",
                                                 materialNumber(component.body, "_InvertX", 0)) >= .5},
            {"volumeFade", materialNumber(component.body, "_ElementVolumeFadeMode",
                                           materialNumber(component.body, "_ElementFadeSupport", 0)) >= .5},
            {"motionMode", std::int64_t(materialNumber(component.body, "_ElementMotionMode", 15))},
            {"motionPower", materialNumber(component.body, "_MotionPower", 0)}}));
    }
    findings.push_back({std::string(path), "TVE.elements", ImportDisposition::Translated,
                        std::to_string(output.size()) + " executable element definitions normalized"});
    if (invalidMaterials != 0)
        findings.push_back({std::string(path), "TVE.elements.invalid-material", ImportDisposition::PreservedSource,
                            std::to_string(invalidMaterials) + " source elements have no executable material"});
    return Result<Value>::success(Value(std::move(output)));
}

std::array<double, 4> multiply(std::array<double, 4> lhs, std::array<double, 4> rhs) {
    return {lhs[3] * rhs[0] + lhs[0] * rhs[3] + lhs[1] * rhs[2] - lhs[2] * rhs[1],
            lhs[3] * rhs[1] - lhs[0] * rhs[2] + lhs[1] * rhs[3] + lhs[2] * rhs[0],
            lhs[3] * rhs[2] + lhs[0] * rhs[1] - lhs[1] * rhs[0] + lhs[2] * rhs[3],
            lhs[3] * rhs[3] - lhs[0] * rhs[0] - lhs[1] * rhs[1] - lhs[2] * rhs[2]};
}

Result<Value> motionDirection(std::string_view source, std::string_view body) {
    const auto manager = fileId(body, "m_GameObject").value_or(0);
    const auto target = fileId(body, "mainDirection").value_or(0);
    const auto gameObject = target == 0 ? manager : target;
    const auto sceneTransforms = transforms(source);
    auto found = std::find_if(sceneTransforms.begin(), sceneTransforms.end(),
                              [&](const auto& entry) { return entry.second.gameObject == gameObject; });
    if (found == sceneTransforms.end())
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                        "TVE motion direction Transform is missing", "mainDirection",
                                                        {}, "asset.import"));
    auto rotation = found->second.rotation;
    std::set<std::int64_t> visited{found->first};
    auto parent = found->second.parent;
    while (parent != 0) {
        const auto ancestor = sceneTransforms.find(parent);
        if (ancestor == sceneTransforms.end() || !visited.emplace(parent).second)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                            "TVE motion direction Transform hierarchy is invalid",
                                                            "mainDirection", {}, "asset.import"));
        rotation = multiply(ancestor->second.rotation, rotation);
        parent = ancestor->second.parent;
    }
    const double length = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
                                    rotation[2] * rotation[2] + rotation[3] * rotation[3]);
    if (!std::isfinite(length) || length <= 1e-12)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                        "TVE motion direction rotation is invalid", "mainDirection", {},
                                                        "asset.import"));
    for (auto& value : rotation) value /= length;
    const double unityX = 2.0 * (rotation[0] * rotation[2] + rotation[3] * rotation[1]);
    const double unityZ = 1.0 - 2.0 * (rotation[0] * rotation[0] + rotation[1] * rotation[1]);
    const double horizontal = std::hypot(unityX, unityZ);
    if (!std::isfinite(horizontal) || horizontal <= 1e-12)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "TVE motion direction is vertical",
                                                        "mainDirection", {}, "asset.import"));
    return Result<Value>::success(Value::array({-unityX / horizontal, -unityZ / horizontal}));
}

Result<Value> volumeChannel(std::string_view body, std::string_view name) {
    std::cmatch legacy;
    const std::regex legacyField("(?:^|\\n)  " + std::string(name) + R"(: *(-?[0-9]+)\s*(?:\r?\n|$))");
    if (std::regex_search(body.data(), body.data() + body.size(), legacy, legacyField)) {
        try {
            const auto resolution = static_cast<std::int64_t>(std::stoll(legacy[1].str()));
            return Result<Value>::success(
                Value::array({Value(std::int64_t{10}), Value(resolution), Value(resolution)}));
        } catch (...) {
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                            "TVE legacy volume resolution is invalid",
                                                            std::string(name), {}, "asset.import"));
        }
    }
    std::cmatch match;
    const std::regex section("(?:^|\\n)  " + std::string(name) + R"(:\s*\n([\s\S]*?)(?=\n  [A-Za-z_][^\n]*:|$))");
    if (!std::regex_search(body.data(), body.data() + body.size(), match, section))
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "TVE volume channel is missing",
                                                        std::string(name), {}, "asset.import"));
    const auto nested = match[1].str();
    auto read = [&](std::string_view field) -> Result<double> {
        std::cmatch value;
        const std::regex pattern("(?:^|\\n)    " + std::string(field) + R"(: *([^\r\n]+))");
        if (!std::regex_search(nested.data(), nested.data() + nested.size(), value, pattern))
            return Result<double>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "TVE volume channel field is missing",
                                          std::string(name) + "." + std::string(field), {}, "asset.import"));
        try {
            std::size_t end = 0;
            const auto token = value[1].str();
            const auto decoded = std::stod(token, &end);
            if (end != token.size() || !std::isfinite(decoded)) throw std::invalid_argument("number");
            return Result<double>::success(decoded);
        } catch (...) {
            return Result<double>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "TVE volume channel field is invalid",
                                          std::string(name) + "." + std::string(field), {}, "asset.import"));
        }
    };
    auto mode = read("renderMode"), width = read("textureWidth"), height = read("textureHeight");
    const bool modeOk = bool(mode), widthOk = bool(width), heightOk = bool(height);
    if (!modeOk || !widthOk || !heightOk)
        return Result<Value>::failure(!modeOk ? mode.status() : !widthOk ? width.status() : height.status());
    return Result<Value>::success(Value::array({mode.value(), width.value(), height.value()}));
}

Result<Value> volume(std::string_view body) {
    auto values = array(body, {{"renderScale", 1}, {"elementsVisibility", 0}, {"elementsSorting", 0},
                                {"elementsEdgeFade", .75}});
    auto colors = volumeChannel(body, "renderColors"), extras = volumeChannel(body, "renderExtras"),
         motion = volumeChannel(body, "renderMotion"), vertex = volumeChannel(body, "renderVertex");
    const bool valuesOk = bool(values), colorsOk = bool(colors), extrasOk = bool(extras), motionOk = bool(motion),
               vertexOk = bool(vertex);
    if (!valuesOk || !colorsOk || !extrasOk || !motionOk || !vertexOk)
        return Result<Value>::failure(!valuesOk   ? values.status()
                                      : !colorsOk ? colors.status()
                                      : !extrasOk ? extras.status()
                                      : !motionOk ? motion.status()
                                                  : vertex.status());
    return Result<Value>::success(Value::object({{"values", std::move(values).takeValue()},
                                                  {"colors", std::move(colors).takeValue()},
                                                  {"extras", std::move(extras).takeValue()},
                                                  {"motion", std::move(motion).takeValue()},
                                                  {"vertex", std::move(vertex).takeValue()}}));
}
}  // namespace

Result<PreparedAssetImport> prepareUnityVegetationScene(const UnityProjectImportRequest& request,
                                                        const UnitySourceAsset& source) {
    const auto& bytes = request.files.at(source.path);
    const std::string text(bytes.begin(), bytes.end());
    const auto controlBody = component(text, controlGuid), detailsBody = component(text, detailsGuid),
               motionBody = component(text, motionGuid), volumeBody = component(text, volumeGuid);
    if (!controlBody && !detailsBody && !motionBody && !volumeBody)
        return Result<PreparedAssetImport>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "Unity scene has no TVE runtime manager components",
                              source.path, {}, "asset.import"));
    if (!controlBody || !detailsBody || !motionBody || !volumeBody)
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                      "TVE scene manager component set is incomplete",
                                                                      source.path, {}, "asset.import"));
    std::vector<ImportFinding> elementFindings;
    auto controlValue = control(*controlBody), detailsValue = details(*detailsBody), motionValue = motion(*motionBody),
         volumeValue = volume(*volumeBody), elementsValue = elements(text, elementFindings, source.path);
    const bool controlOk = bool(controlValue), detailsOk = bool(detailsValue), motionOk = bool(motionValue),
               volumeOk = bool(volumeValue), elementsOk = bool(elementsValue);
    if (!controlOk || !detailsOk || !motionOk || !volumeOk || !elementsOk)
        return Result<PreparedAssetImport>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "TVE scene manager data is malformed", source.path, {}, "asset.import"));
    auto directionValue = motionDirection(text, *motionBody);
    if (!directionValue) return Result<PreparedAssetImport>::failure(directionValue.status());
    (*motionValue.value().getIf<Value::Object>())["direction"] = std::move(directionValue).takeValue();
    std::set<std::string> textureGuids;
    auto boundTextures = bindTextureAssets(elementsValue.value(), request);
    if (!boundTextures) return Result<PreparedAssetImport>::failure(boundTextures.status());
    collectTextureGuids(elementsValue.value(), textureGuids);
    for (const auto name : {"overlayAlbedo", "overlayNormal", "noiseTexture3D"}) {
        auto value = guid(*controlBody, name);
        if (!value.empty()) textureGuids.insert(std::move(value));
    }
    auto motionNoise = guid(*motionBody, "noiseTexture");
    if (!motionNoise.empty()) textureGuids.insert(std::move(motionNoise));
    Value definition = Value::object({{"schema", "eve.vegetation-scene"}, {"schemaVersion", 1},
                                      {"sourceGuid", source.guid}, {"control", std::move(controlValue).takeValue()},
                                      {"details", std::move(detailsValue).takeValue()},
                                      {"motion", std::move(motionValue).takeValue()},
                                      {"volume", std::move(volumeValue).takeValue()},
                                      {"elements", std::move(elementsValue).takeValue()}});
    auto encoded = definition.toJson();
    if (!encoded) return Result<PreparedAssetImport>::failure(encoded.status());
    auto manifest = detail::baseManifest(request.package, "eve.unity-vegetation-scene/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport out;
    out.manifest = std::move(manifest).takeValue();
    const auto id = request.package.packageId.child("unity:" + source.guid).child("vegetation-scene:default");
    auto ref = detail::assetRef(id);
    if (!ref) return Result<PreparedAssetImport>::failure(ref.status());
    for (const auto& textureGuid : textureGuids) {
        if (!importedImageAvailable(request, textureGuid)) continue;
        auto image = detail::assetRef(request.package.packageId.child("unity:" + textureGuid).child("image:default"));
        if (!image) return Result<PreparedAssetImport>::failure(image.status());
        out.manifest.dependencies.push_back({ref.value(), image.value(), asset::EvaDependencyKind::RuntimeRequired,
                                             "elementTexture:" + textureGuid, {}, "eve.image/3", {}});
    }
    const std::string path = "assets/" + id.format() + "/asset.json";
    const std::vector<std::uint8_t> payload(encoded.value().begin(), encoded.value().end());
    out.manifest.assets.push_back({ref.value(), "eve.vegetation-scene", SchemaVersion(1), path,
                                   detail::sha256(payload), {"vegetation", "scene", "source:unity"}});
    out.manifest.entrypoints.emplace("default", ref.value());
    out.entries.push_back({path, payload});
    out.sourceMappings.push_back({"TVE.Manager", ref.value()});
    out.findings.push_back({source.path, "TVE.sceneManager", ImportDisposition::Translated,
                            "Global Control, Details, Motion and Volume serialized as native scene state"});
    out.findings.insert(out.findings.end(), std::make_move_iterator(elementFindings.begin()),
                        std::make_move_iterator(elementFindings.end()));

    auto hierarchy = prepareUnityPrefab(request, source.path);
    if (!hierarchy) return Result<PreparedAssetImport>::failure(hierarchy.status());
    const auto templateAsset = std::find_if(hierarchy.value().manifest.assets.begin(),
                                            hierarchy.value().manifest.assets.end(), [](const auto& asset) {
                                                return asset.type == "eve.scene-template";
                                            });
    if (templateAsset == hierarchy.value().manifest.assets.end())
        return Result<PreparedAssetImport>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation, "Unity manager scene hierarchy produced no scene template", source.path,
            {}, "asset.import"));
    const AssetRef templateRef = templateAsset->asset;
    out.manifest.dependencies.push_back({templateRef, ref.value(), asset::EvaDependencyKind::RuntimeRequired,
                                         "vegetationScene", {}, "eve.vegetation-scene/1", {}});
    out.manifest.entrypoints.emplace("scene", templateRef);
    for (auto& asset : hierarchy.value().manifest.assets) out.manifest.assets.push_back(std::move(asset));
    for (auto& dependency : hierarchy.value().manifest.dependencies)
        out.manifest.dependencies.push_back(std::move(dependency));
    for (auto& entry : hierarchy.value().entries) out.entries.push_back(std::move(entry));
    for (auto& mapping : hierarchy.value().sourceMappings) out.sourceMappings.push_back(std::move(mapping));
    for (auto& finding : hierarchy.value().findings) out.findings.push_back(std::move(finding));
    return Result<PreparedAssetImport>::success(std::move(out));
}
}  // namespace eve::asset_import
