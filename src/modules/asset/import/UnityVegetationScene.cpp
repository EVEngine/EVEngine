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

// Vegetation material import (previously UnityVegetationMaterial.cpp).
#include <array>
#include <cmath>
#include <limits>
#include <regex>
#include "asset/CanonicalImageCook.h"
#include "asset/SourcePng.h"
#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

namespace eve::asset_import {
namespace {
struct Invalid {};
std::optional<std::string> match(const std::string& s, const std::string& pattern) {
    const std::regex expression(pattern);
    auto             it = std::sregex_iterator(s.begin(), s.end(), expression);
    if (it == std::sregex_iterator()) return {};
    const auto result = (*it)[1].str();
    if (++it != std::sregex_iterator()) throw Invalid{};
    return result;
}
double numeric(const Value& value) {
    if (!value.isNumeric()) throw Invalid{};
    const double n = value.isInt64() ? double(value.asInt()) : value.asDouble();
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) throw Invalid{};
    return n;
}
double scalar(const std::string& text, const std::string& name, double fallback) {
    const auto raw = match(text, "(?:^|\\n)\\s*- " + name + ": *([^\\r\\n]+)");
    if (!raw) return fallback;
    auto decoded = Value::fromJson(*raw);
    if (!decoded) throw Invalid{};
    return numeric(decoded.value());
}
double importerScalar(const std::string& text, const std::string& name, double fallback) {
    const auto raw = match(text, "(?:^|\\n)[ \\t]*" + name + ": *([^\\r\\n]+)");
    if (!raw) return fallback;
    auto decoded = Value::fromJson(*raw);
    if (!decoded) throw Invalid{};
    return numeric(decoded.value());
}
std::array<double, 4> vector(const std::string& text, const std::string& name, std::array<double, 4> fallback) {
    auto raw = match(text, "(?:^|\\n)\\s*- " + name + ": *(\\{[^\\r\\n]+\\})");
    if (!raw) return fallback;
    auto value = Value::fromJson(std::regex_replace(*raw, std::regex("([rgba]):"), "\"$1\":"));
    if (!value || !value.value().isObject()) throw Invalid{};
    const auto& object = *value.value().getIf<Value::Object>();
    unsigned    i      = 0;
    for (const auto key : {"r", "g", "b", "a"}) {
        if (!object.contains(key)) throw Invalid{};
        fallback[i++] = numeric(object.at(key));
    }
    return fallback;
}
std::optional<std::string> texture(const std::string& text, const std::string& name) {
    auto block = match(text, "- " + name + ":([\\s\\S]*?)(?:\\n    - |\\n    m_|$)");
    if (!block) return {};
    if (match(*block, R"(m_Texture: *\{fileID: *(0)\})")) return {};
    auto guid = match(*block, R"(m_Texture: *\{fileID: *2800000, guid: *([0-9a-fA-F]{32}), type: *[23]\})");
    if (!guid) throw Invalid{};
    return unity_detail::foldAscii(*guid);
}
Result<std::string> texturePath(const UnityProjectImportRequest& request, const std::string& guid) {
    std::string found;
    for (const auto& [path, bytes] : request.files)
        if (path.ends_with(".meta")) {
            auto id = unity_detail::metaScalar(bytes, "guid", path);
            if (!id) return Result<std::string>::failure(id.status());
            if (unity_detail::foldAscii(id.value()) != guid) continue;
            if (!found.empty())
                return Result<std::string>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "duplicate texture GUID", guid, {}, "asset.import"));
            found = path.substr(0, path.size() - 5);
        }
    if (found.empty() || !request.files.contains(found))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "TVE texture source is absent", guid, {}, "asset.import"));
    return Result<std::string>::success(std::move(found));
}
Result<Value::Object> textureSampler(const UnityProjectImportRequest& request, const std::string& path) {
    const bool        native = path.ends_with(".asset");
    const auto&       bytes  = request.files.at(native ? path : path + ".meta");
    const std::string settings(bytes.begin(), bytes.end());
    const auto        u = importerScalar(settings, native ? "m_WrapU" : "wrapU", -1);
    const auto        v = importerScalar(settings, native ? "m_WrapV" : "wrapV", -1);
    for (const auto wrap : {u, v}) {
        if (wrap != -1 && wrap != 0 && wrap != 1 && wrap != 2 && wrap != 3) throw Invalid{};
        if (wrap == 3)
            return Result<Value::Object>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "texture MirrorOnce addressing is not supported by the canonical sampler",
                path, {}, "asset.import"));
    }
    auto convert = [](double wrap) { return wrap == 1 ? 33071 : wrap == 2 ? 33648 : 10497; };
    auto filter  = importerScalar(settings, native ? "m_FilterMode" : "filterMode", -1);
    if (filter != -1 && filter != 0 && filter != 1 && filter != 2) throw Invalid{};
    if (filter == -1) {
        if (native) return Result<Value::Object>::success({{"wrapS", convert(u)}, {"wrapT", convert(v)}});
        const auto heightNormal = importerScalar(settings, "convertToNormalMap", 0);
        if (heightNormal != 0 && heightNormal != 1) throw Invalid{};
        filter = heightNormal == 1 ? 2 : 1;
    }
    const auto mipSetting = importerScalar(settings, native ? "m_MipCount" : "enableMipMap", 1);
    if (native ? (mipSetting < 1 || mipSetting != std::floor(mipSetting)) : (mipSetting != 0 && mipSetting != 1))
        throw Invalid{};
    const bool mipmaps = native ? mipSetting > 1 : mipSetting == 1;
    const int  mag     = filter == 0 ? 9728 : 9729;
    const int  min     = !mipmaps ? mag : filter == 0 ? 9984 : filter == 2 ? 9987 : 9985;
    return Result<Value::Object>::success(
        {{"wrapS", convert(u)}, {"wrapT", convert(v)}, {"minFilter", min}, {"magFilter", mag}});
}
Result<PreparedAssetImport> ormImage(const UnityProjectImportRequest& request, const std::string& text,
                                     const PersistentId& materialId, double occlusion, double smoothness) {
    std::vector<std::uint8_t> rgba{255, 255, 255, 255};
    std::uint32_t             width = 1, height = 1;
    auto                      identity = request.package;
    identity.packageId                 = materialId.child("main-orm");
    if (auto guid = texture(text, "_MainMaskTex")) {
        auto path = texturePath(request, *guid);
        if (!path) return Result<PreparedAssetImport>::failure(path.status());
        auto imported = prepareImageImport(
            {identity, path.value(), request.files.at(path.value()), ImageColorSpace::Linear, "mask", request.limits});
        if (!imported) return imported;
        const auto definition = imported.value().manifest.assets.front().definition;
        for (const auto& entry : imported.value().entries)
            if (entry.path == definition) {
                auto cooked = asset::cookCanonicalImageRgba8(entry.bytes, request.files.at(path.value()),
                                                             request.limits.maximumDecodedBytes);
                if (!cooked) return Result<PreparedAssetImport>::failure(cooked.status());
                auto read32 = [&](std::size_t p) {
                    const auto& b = cooked.value().bulk;
                    return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) | (std::uint32_t(b[p + 2]) << 16) |
                           (std::uint32_t(b[p + 3]) << 24);
                };
                width  = read32(8);
                height = read32(12);
                rgba.assign(cooked.value().bulk.begin() + 28, cooked.value().bulk.end());
            }
    }
    for (std::size_t p = 0; p < rgba.size(); p += 4) {
        const auto ao        = 255.0 + (double(rgba[p + 1]) - 255.0) * occlusion;
        const auto roughness = 255.0 - double(rgba[p + 3]) * smoothness;
        const auto mask      = rgba[p + 2];
        rgba[p]              = std::uint8_t(std::lround(ao));
        rgba[p + 1]          = std::uint8_t(std::lround(roughness));
        rgba[p + 2]          = 0;
        rgba[p + 3]          = mask;
    }
    auto png = asset::detail::encodeSourcePng(width, height, rgba, request.limits.maximumDecodedBytes);
    if (!png) return Result<PreparedAssetImport>::failure(png.status());
    return prepareImageImport(
        {identity, "TVE main ORM", std::move(png).takeValue(), ImageColorSpace::Linear, "orm", request.limits});
}
enum class NormalSource { Authored, Height };
uint32_t normalExtent(uint32_t extent, int mode) {
    if (mode == 0 || (extent & (extent - 1)) == 0) return extent;
    uint32_t lower = 1;
    while (lower <= extent / 2) lower *= 2;
    if (lower > UINT32_MAX / 2) throw Invalid{};
    const auto upper = lower * 2;
    return mode == 3 || (mode == 1 && extent - lower < upper - extent) ? lower : upper;
}
struct NormalResizeTap {
    uint32_t index;
    double   weight;
};
std::vector<NormalResizeTap> normalResizeTaps(uint32_t source, uint32_t target, uint32_t coordinate, int algorithm) {
    if (source == target) return {{coordinate, 1}};
    const double center = algorithm == 1 && target > source ? double(coordinate) * (source - 1) / (target - 1)
                                                            : (coordinate + .5) * source / target - .5;
    std::vector<NormalResizeTap> taps;
    auto                         append = [&](int64_t index, double weight) {
        taps.push_back({uint32_t(std::clamp(index, int64_t(0), int64_t(source - 1))), weight});
    };
    if (algorithm == 1) {
        const auto first = int64_t(std::floor(center));
        append(first, 1 - (center - first));
        append(first + 1, center - first);
        return taps;
    }
    const double scale = std::max(double(source) / target, 1.0);
    const double b     = target > source ? 1.0 : 1.0 / 3.0;
    const double c     = target > source ? 0.0 : 1.0 / 3.0;
    double       sum   = 0;
    for (auto index = int64_t(std::floor(center - 2 * scale)); index <= int64_t(std::ceil(center + 2 * scale));
         ++index) {
        const double x = std::abs((center - index) / scale);
        const double weight =
            x < 1 ? ((12 - 9 * b - 6 * c) * x * x * x + (-18 + 12 * b + 6 * c) * x * x + 6 - 2 * b) / 6
            : x < 2
                ? ((-b - 6 * c) * x * x * x + (6 * b + 30 * c) * x * x + (-12 * b - 48 * c) * x + 8 * b + 24 * c) / 6
                : 0;
        append(index, weight);
        sum += weight;
    }
    for (auto& tap : taps) tap.weight /= sum;
    return taps;
}
std::vector<double> normalResizeRgb(const std::vector<uint8_t>& bulk, uint32_t width, uint32_t height,
                                    uint32_t targetWidth, uint32_t targetHeight, int algorithm) {
    std::vector<double> rgb(size_t(targetWidth) * targetHeight * 3);
    for (uint32_t y = 0; y < targetHeight; ++y) {
        const auto vertical = normalResizeTaps(height, targetHeight, y, algorithm);
        for (uint32_t x = 0; x < targetWidth; ++x) {
            const auto            horizontal = normalResizeTaps(width, targetWidth, x, algorithm);
            std::array<double, 3> value{};
            for (const auto& sy : vertical)
                for (const auto& sx : horizontal)
                    for (size_t c = 0; c < 3; ++c)
                        value[c] += bulk[28 + (size_t(sy.index) * width + sx.index) * 4 + c] * sy.weight * sx.weight;
            for (size_t c = 0; c < 3; ++c) {
                const auto clamped = std::clamp(value[c], 0.0, 255.0);
                // Unity's default cubic path quantizes after both axes. Convert
                // to float before rounding to preserve its half-integer boundary.
                rgb[(size_t(y) * targetWidth + x) * 3 + c] =
                    algorithm == 0 ? double(std::lround(float(clamped))) / 255.0 : clamped / 255.0;
            }
        }
    }
    return rgb;
}
Result<PreparedAssetImport> normalImage(const UnityProjectImportRequest& request, const std::string& path,
                                        const PersistentId& materialId, double strength, bool sobel, bool mipmaps,
                                        bool flipGreen, NormalSource source) {
    const bool   fromHeight = source == NormalSource::Height;
    const size_t channels   = fromHeight ? 1 : 2;
    auto         identity   = request.package;
    identity.packageId      = materialId.child(fromHeight ? "main-height-normal" : "main-authored-normal");
    auto image              = prepareImageImport({identity, path, request.files.at(path), ImageColorSpace::Linear,
                                     fromHeight ? "height" : "normal", request.limits});
    if (!image) return image;
    const auto definition = image.value().manifest.assets.front().definition;
    for (const auto& entry : image.value().entries) {
        if (entry.path != definition) continue;
        auto cooked =
            asset::cookCanonicalImageRgba8(entry.bytes, request.files.at(path), request.limits.maximumDecodedBytes);
        if (!cooked) return Result<PreparedAssetImport>::failure(cooked.status());
        const auto& bulk   = cooked.value().bulk;
        auto        read32 = [&](std::size_t p) {
            return std::uint32_t(bulk[p]) | (std::uint32_t(bulk[p + 1]) << 8) | (std::uint32_t(bulk[p + 2]) << 16) |
                   (std::uint32_t(bulk[p + 3]) << 24);
        };
        auto              width = read32(8), height = read32(12);
        const auto&       metaBytes = request.files.at(path + ".meta");
        const std::string meta(metaBytes.begin(), metaBytes.end());
        if (mipmaps && importerScalar(meta, "mipMapMode", 0) != 0)
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "normal Kaiser mips are not implemented", path, {}, "asset.import"));
        const auto npot = importerScalar(meta, "nPOTScale", 1);
        if (npot < 0 || npot > 3 || npot != std::floor(npot)) throw Invalid{};
        const auto targetWidth = normalExtent(width, int(npot)), targetHeight = normalExtent(height, int(npot));
        std::vector<double> resized;
        int                 resizeAlgorithm = 0;
        if (targetWidth != width || targetHeight != height) {
            const std::regex   algorithms(R"((?:^|\n)[ \t]*resizeAlgorithm: *([^\r\n]+))");
            std::optional<int> selected;
            for (auto it = std::sregex_iterator(meta.begin(), meta.end(), algorithms); it != std::sregex_iterator();
                 ++it) {
                auto value = Value::fromJson((*it)[1].str());
                if (!value) throw Invalid{};
                const auto algorithm = numeric(value.value());
                if (algorithm != 0 && algorithm != 1) throw Invalid{};
                if (selected && *selected != int(algorithm))
                    return Result<PreparedAssetImport>::failure(Diagnostic::error(
                        DiagnosticCode::Unsupported,
                        "platform-specific normal resize algorithms require target-specific image variants", path, {},
                        "asset.import"));
                selected = int(algorithm);
            }
            resizeAlgorithm = selected.value_or(0);
            if (uint64_t(targetWidth) * targetHeight > request.limits.maximumDecodedBytes / sizeof(double) / 3)
                return Result<PreparedAssetImport>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "resized normal RGB staging exceeds budget",
                                      path, {}, "asset.import"));
            resized = normalResizeRgb(bulk, width, height, targetWidth, targetHeight, resizeAlgorithm);
            width   = targetWidth;
            height  = targetHeight;
        }
        uint32_t w = width, h = height, levels = 0;
        uint64_t total = 0;
        do {
            const uint64_t bytes = uint64_t(w) * h * 4;
            if (request.limits.maximumDecodedBytes < 28 || total > request.limits.maximumDecodedBytes - 28 ||
                bytes > request.limits.maximumDecodedBytes - 28 - total)
                return Result<PreparedAssetImport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "normal mip chain exceeds budget", path, {}, "asset.import"));
            total += bytes;
            ++levels;
            if (!mipmaps || (w == 1 && h == 1)) break;
            w = std::max(w / 2, 1u);
            h = std::max(h / 2, 1u);
        } while (true);
        if (total > request.limits.maximumSourceBytes ||
            uint64_t(width) * height > request.limits.maximumDecodedBytes / sizeof(double) / channels)
            return Result<PreparedAssetImport>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "normal source or channel staging exceeds budget",
                                  path, {}, "asset.import"));
        std::vector<double> heights(size_t(width) * height * channels);
        for (size_t i = 0; i < size_t(width) * height; ++i) {
            if (!resized.empty()) {
                if (fromHeight)
                    heights[i] = (resized[i * 3] + resized[i * 3 + 1] + resized[i * 3 + 2]) / 3;
                else {
                    heights[i * 2]     = resized[i * 3];
                    heights[i * 2 + 1] = resized[i * 3 + 1];
                }
                continue;
            }
            if (fromHeight)
                heights[i] = (double(bulk[28 + i * 4]) + bulk[29 + i * 4] + bulk[30 + i * 4]) / 765.0;
            else {
                heights[i * 2]     = double(bulk[28 + i * 4]) / 255.0;
                heights[i * 2 + 1] = double(bulk[29 + i * 4]) / 255.0;
            }
        }
        std::vector<uint8_t> rgba;
        rgba.reserve(size_t(total));
        w = width;
        h = height;
        for (uint32_t level = 0; level < levels; ++level) {
            auto sample = [&](int64_t x, int64_t y) {
                x = (x + w) % w;
                y = (y + h) % h;
                return heights[size_t(y) * w + size_t(x)];
            };
            const double factor = .04 * (double(w) + h) * (std::pow(10.0, strength) - 1.0);
            for (int64_t y = 0; y < h; ++y) {
                for (int64_t x = 0; x < w; ++x) {
                    if (!fromHeight) {
                        const auto at    = (size_t(y) * w + size_t(x)) * 2;
                        const auto green = flipGreen ? 1.0 - heights[at + 1] : heights[at + 1];
                        rgba.insert(rgba.end(), {uint8_t(std::lround(heights[at] * 255)),
                                                 uint8_t(std::lround(green * 255)), 255, 255});
                        continue;
                    }
                    double dx = sample(x - 1, y) - sample(x + 1, y);
                    double dy = sample(x, y + 1) - sample(x, y - 1);
                    if (sobel) {
                        dx = (sample(x - 1, y - 1) - sample(x + 1, y - 1) + 2 * dx + sample(x - 1, y + 1) -
                              sample(x + 1, y + 1)) *
                             .25;
                        dy = (sample(x - 1, y + 1) - sample(x - 1, y - 1) + 2 * dy + sample(x + 1, y + 1) -
                              sample(x + 1, y - 1)) *
                             .25;
                    }
                    dx *= factor;
                    dy *= flipGreen ? -factor : factor;
                    const double length = std::sqrt(dx * dx + dy * dy + 1.0);
                    rgba.insert(rgba.end(), {uint8_t(std::lround((dx / length * .5 + .5) * 255)),
                                             uint8_t(std::lround((dy / length * .5 + .5) * 255)), 255, 255});
                }
            }
            if (level + 1 == levels) break;
            const uint32_t      nextW = std::max(w / 2, 1u), nextH = std::max(h / 2, 1u);
            std::vector<double> next(size_t(nextW) * nextH * channels);
            for (uint32_t y = 0; y < nextH; ++y) {
                for (uint32_t x = 0; x < nextW; ++x) {
                    const double sx = (x + .5) * w / nextW - .5, sy = (y + .5) * h / nextH - .5;
                    const auto   ix = uint32_t(std::floor(sx)), iy = uint32_t(std::floor(sy));
                    const double fx = sx - ix, fy = sy - iy;
                    const auto   x1 = std::min(ix + 1, w - 1), y1 = std::min(iy + 1, h - 1);
                    for (size_t c = 0; c < channels; ++c)
                        next[(size_t(y) * nextW + x) * channels + c] =
                            (heights[(size_t(iy) * w + ix) * channels + c] * (1 - fx) +
                             heights[(size_t(iy) * w + x1) * channels + c] * fx) *
                                (1 - fy) +
                            (heights[(size_t(y1) * w + ix) * channels + c] * (1 - fx) +
                             heights[(size_t(y1) * w + x1) * channels + c] * fx) *
                                fy;
                }
            }
            heights = std::move(next);
            w       = nextW;
            h       = nextH;
        }
        auto parsed = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
        if (!parsed) return Result<PreparedAssetImport>::failure(parsed.status());
        auto&      object  = *parsed.value().getIf<Value::Object>();
        const auto oldBlob = object.at("blob").asString();
        const auto newBlob = definition.substr(0, definition.rfind('/') + 1) + "normal.rgba8-mips";
        object["encoding"] = Value("rgba8-mips");
        object["blob"]     = Value(newBlob);
        object["mipCount"] = Value(int64_t(levels));
        object["width"]    = Value(int64_t(width));
        object["height"]   = Value(int64_t(height));
        object["usage"]    = Value("normal");
        auto encoded       = parsed.value().toJson();
        if (!encoded) return Result<PreparedAssetImport>::failure(encoded.status());
        auto result = std::move(image).takeValue();
        for (auto& output : result.entries) {
            if (output.path == definition)
                output.bytes.assign(encoded.value().begin(), encoded.value().end());
            else if (output.path == oldBlob) {
                output.path  = newBlob;
                output.bytes = std::move(rgba);
            }
        }
        auto& asset = result.manifest.assets.front();
        asset.contentHash =
            detail::sha256({reinterpret_cast<const uint8_t*>(encoded.value().data()), encoded.value().size()});
        asset.tags = {"image", "usage:normal"};
        std::erase_if(result.entries, [](const auto& output) { return output.path == "reports/import.json"; });
        result.findings = {
            {path, fromHeight ? "TextureImporter.HeightNormal" : "TextureImporter.AuthoredNormal",
             ImportDisposition::Baked,
             fromHeight ? "height resampled and normals regenerated independently per mip"
                        : "linear source RG resampled independently per mip before green flip and quantization"}};
        auto report = detail::finalizeImportReport(result, identity, "TVE", "12.6.0",
                                                   {{"fromHeight", Value(fromHeight)},
                                                    {"npotScale", Value(int64_t(npot))},
                                                    {"resizeAlgorithm", Value(int64_t(resizeAlgorithm))},
                                                    {"heightScale", Value(strength)},
                                                    {"sobel", Value(sobel)},
                                                    {"flipGreen", Value(flipGreen)},
                                                    {"mipCount", Value(int64_t(levels))}});
        if (!report) return Result<PreparedAssetImport>::failure(report.status());
        return Result<PreparedAssetImport>::success(std::move(result));
    }
    return Result<PreparedAssetImport>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "normal image definition absent", path, {}, "asset.import"));
}
}  // namespace

Result<PreparedAssetImport> prepareUnityVegetationMaterial(const UnityProjectImportRequest& request,
                                                           const UnitySourceAsset&          source) {
    const auto&       bytes = request.files.at(source.path);
    const std::string text(bytes.begin(), bytes.end());
    try {
        const auto shader = match(text, R"(m_Shader: *\{fileID: *4800000, guid: *([0-9a-fA-F]{32}), type: *3\})");
        if (!shader ||
            (*shader != "7befaa6f41d00a6478d5f4af21d66518" && *shader != "a933075b367f9b24981408633f72ff34" &&
             *shader != "6e6307b56f9201d40ad738f21cf03495" && *shader != "d9a724745053dee46bf301b216cdd348"))
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "shader is not an admitted TVE 12.6.0 Plant/Prop material", source.path,
                {}, "asset.import"));
        auto unit = [](double v) {
            if (v < 0 || v > 1) throw Invalid{};
            return v;
        };
        const auto mode = scalar(text, "_RenderMode", 0), cull = scalar(text, "_RenderCull", 0),
                   clip = scalar(text, "_RenderClip", 1), coverage = scalar(text, "_RenderCoverage", 0),
                   specular = scalar(text, "_RenderSpecular", 1);
        if ((mode != 0 && mode != 1) || cull < 0 || cull > 2 || cull != std::floor(cull) || (clip != 0 && clip != 1) ||
            (coverage != 0 && coverage != 1))
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "TVE render mode, alpha clipping, or cull mode is invalid",
                source.path, {}, "asset.import"));
        auto       color  = vector(text, "_MainColor", {1, 1, 1, 1});
        auto       second = vector(text, "_MainColorTwo", {1, 1, 1, 1});
        const auto uv     = vector(text, "_MainUVs", {1, 1, 0, 0});
        for (unsigned c = 0; c < 4; ++c)
            if (color[c] < 0 || second[c] < 0 || (c == 3 && (color[c] > 1 || second[c] > 1))) throw Invalid{};
        // These source properties are [HDR] colors: Unity uploads their stored
        // values directly, including subunit RGB, even in linear color space.
        const auto dual = scalar(text, "_MainColorMode", 0);
        if (dual != 0 && dual != 1) throw Invalid{};
        const auto occlusion  = unit(scalar(text, "_MainOcclusionValue", 0)),
                   smoothness = unit(scalar(text, "_MainSmoothnessValue", 0));
        const auto albedo     = unit(scalar(text, "_MainAlbedoValue", 1)),
                   cutoff     = unit(scalar(text, "_AlphaClipValue", .5));
        const auto minimum    = unit(scalar(text, "_MainMaskMinValue", 0)),
                   maximum    = unit(scalar(text, "_MainMaskMaxValue", 0));
        if (float(maximum) - float(minimum) + .0001f == 0) throw Invalid{};
        auto manifest = detail::baseManifest(request.package, "eve.unity-tve-material/1");
        if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
        PreparedAssetImport out;
        out.manifest   = std::move(manifest).takeValue();
        const auto id  = request.package.packageId.child("unity:" + source.guid + ":2100000");
        auto       ref = detail::assetRef(id);
        if (!ref) return Result<PreparedAssetImport>::failure(ref.status());
        Value::Object definition{{"schema", "eve.material"},
                                 {"schemaVersion", 15},
                                 {"shadingModel", "pbr"},
                                 {"surfaceMode", mode == 1   ? "transparent"
                                                 : clip == 1 ? "masked"
                                                             : "opaque"},
                                 {"doubleSided", cull == 0},
                                 {"cullMode", cull == 0 ? "none" : (cull == 1 ? "back" : "front")},
                                 {"alphaToCoverage", coverage == 1 && clip == 1},
                                 {"baseColor", Value::Array{color[0], color[1], color[2], color[3]}},
                                 {"metallic", 0},
                                 {"roughness", 1},
                                 {"specularFactor", unit(specular)},
                                 {"alphaCutoff", cutoff},
                                 {"albedoTextureStrength", albedo}};
        const auto    extrasLayer = scalar(text, "_LayerExtrasValue", 0);
        if (extrasLayer < 0 || extrasLayer > 8 || extrasLayer != std::floor(extrasLayer)) throw Invalid{};
        const auto colorsLayer = scalar(text, "_LayerColorsValue", 0);
        if (colorsLayer < 0 || colorsLayer > 8 || colorsLayer != std::floor(colorsLayer)) throw Invalid{};
        const auto motionLayer = scalar(text, "_LayerMotionValue", 0);
        if (motionLayer < 0 || motionLayer > 8 || motionLayer != std::floor(motionLayer)) throw Invalid{};
        const auto vertexLayer = scalar(text, "_LayerVertexValue", 0);
        if (vertexLayer < 0 || vertexLayer > 8 || vertexLayer != std::floor(vertexLayer)) throw Invalid{};
        auto binary = [&](const char* name, double fallback) {
            const auto value = scalar(text, name, fallback);
            if (value != 0 && value != 1) throw Invalid{};
            return value;
        };
        definition["vegetationAlpha"] = Value::Object{{"global", unit(scalar(text, "_GlobalAlpha", 1))},
                                                      {"variation", unit(scalar(text, "_AlphaVariationValue", .5))},
                                                      {"detailFade", binary("_DetailFadeMode", 0) == 1},
                                                      {"glancing", unit(scalar(text, "_FadeGlancingValue", 0))},
                                                      {"camera", unit(scalar(text, "_FadeCameraValue", 1))},
                                                      {"constant", unit(scalar(text, "_FadeConstantValue", 0))}};
        const auto emissiveMode       = binary("_EmissiveMode", 0);
        if (emissiveMode == 1) {
            const auto emissiveColor = vector(text, "_EmissiveColor", {0, 0, 0, 0});
            for (size_t c = 0; c < 3; ++c)
                if (emissiveColor[c] < 0) throw Invalid{};
            const auto intensityMode = binary("_EmissiveIntensityMode", 0);
            const auto authoredPower = scalar(text, "_EmissiveIntensityValue", 1);
            const auto fallbackPower = intensityMode == 0 ? authoredPower : .125 * std::pow(2., authoredPower);
            const auto intensity     = scalar(text, "_emissive_intensity_value", fallbackPower);
            if (intensity < 0 || intensity > 1000000) throw Invalid{};
            const auto emissionMinimum = unit(scalar(text, "_EmissiveTexMinValue", 0));
            const auto emissionMaximum = unit(scalar(text, "_EmissiveTexMaxValue", 1));
            if (float(emissionMaximum) - float(emissionMinimum) + .0001f == 0) throw Invalid{};
            definition["emissive"]           = Value::Array{emissiveColor[0], emissiveColor[1], emissiveColor[2]};
            definition["emissiveStrength"]   = intensity;
            definition["vegetationEmission"] = Value::Object{{"minimum", emissionMinimum},
                                                             {"maximum", emissionMaximum},
                                                             {"phase", unit(scalar(text, "_EmissivePhaseValue", 1))},
                                                             {"global", unit(scalar(text, "_GlobalEmissive", 1))}};
        }
        const auto gradientOne = vector(text, "_GradientColorOne", {1, 1, 1, 1});
        const auto gradientTwo = vector(text, "_GradientColorTwo", {1, 1, 1, 1});
        for (size_t c = 0; c < 3; ++c)
            if (gradientOne[c] < 0 || gradientTwo[c] < 0) throw Invalid{};
        const auto gradientMinimum = unit(scalar(text, "_GradientMinValue", 0));
        const auto gradientMaximum = unit(scalar(text, "_GradientMaxValue", 1));
        if (float(gradientMaximum) - float(gradientMinimum) + .0001f == 0) throw Invalid{};
        definition["vegetationGradient"] =
            Value::Object{{"colorOne", Value::Array{gradientOne[0], gradientOne[1], gradientOne[2]}},
                          {"colorTwo", Value::Array{gradientTwo[0], gradientTwo[1], gradientTwo[2]}},
                          {"minimum", gradientMinimum},
                          {"maximum", gradientMaximum}};
        definition["vegetationFields"] =
            Value::Object{{"colorsLayer", int64_t(colorsLayer)},
                          {"colorsUsePivotPosition", binary("_ColorsPositionMode", 0) == 1},
                          {"extrasLayer", int64_t(extrasLayer)},
                          {"extrasUsePivotPosition", binary("_ExtrasPositionMode", 0) == 1},
                          {"motionLayer", int64_t(motionLayer)},
                          {"vertexLayer", int64_t(vertexLayer)},
                          {"globalSize", unit(scalar(text, "_GlobalSize", 1))},
                          {"sizeFadeStart", scalar(text, "_SizeFadeStartValue", 0)},
                          {"sizeFadeEnd", scalar(text, "_SizeFadeEndValue", 100)}};
        auto nonnegative = [&](const char* name, double fallback) {
            const auto value = scalar(text, name, fallback);
            if (value < 0 || value > 1000000) throw Invalid{};
            return value;
        };
        definition["vegetationMotion"] =
            Value::Object{{"dynamicMode", unit(scalar(text, "_VertexDynamicMode", 0))},
                          {"rigidity", unit(scalar(text, "_MotionPosition_10", .5))},
                          {"facing", unit(scalar(text, "_MotionFacingValue", .5))},
                          {"bending", nonnegative("_MotionAmplitude_10", .2)},
                          {"bendingSpeed", nonnegative("_MotionSpeed_10", 2)},
                          {"bendingScale", nonnegative("_MotionScale_10", 1)},
                          {"bendingVariation", nonnegative("_MotionVariation_10", 0)},
                          {"branch", nonnegative("_MotionAmplitude_20", .2)},
                          {"rolling", nonnegative("_MotionAmplitude_22", .2)},
                          {"branchSpeed", nonnegative("_MotionSpeed_20", 6)},
                          {"branchScale", nonnegative("_MotionScale_20", 3)},
                          {"branchVariation", nonnegative("_MotionVariation_20", 0)},
                          {"flutter", nonnegative("_MotionAmplitude_32", .2)},
                          {"flutterSpeed", nonnegative("_MotionSpeed_32", 20)},
                          {"flutterScale", nonnegative("_MotionScale_32", 10)},
                          {"flutterVariation", nonnegative("_MotionVariation_32", 0)},
                          {"interaction", nonnegative("_InteractionAmplitude", 1)},
                          {"interactionMask", unit(scalar(text, "_InteractionMaskValue", 1))},
                          {"perspectivePush", nonnegative("_PerspectivePushValue", 0)},
                          {"perspectiveNoise", nonnegative("_PerspectiveNoiseValue", 0)},
                          {"perspectiveAngle", nonnegative("_PerspectiveAngleValue", 1)}};
        const auto occlusionColor = vector(text, "_VertexOcclusionColor", {1, 1, 1, .5019608});
        for (size_t c = 0; c < 3; ++c)
            if (occlusionColor[c] < 0) throw Invalid{};
        const auto colorsOcclusionMode = scalar(text, "_VertexOcclusionColorsMode", 0);
        if (colorsOcclusionMode != 0 && colorsOcclusionMode != 1) throw Invalid{};
        const auto colorsOcclusionMinimum = unit(scalar(text, "_VertexOcclusionMinValue", 0));
        const auto colorsOcclusionMaximum = unit(scalar(text, "_VertexOcclusionMaxValue", 1));
        if (float(colorsOcclusionMaximum) - float(colorsOcclusionMinimum) + .0001f == 0) throw Invalid{};
        const auto colorsIntensity = scalar(text, "_ColorsIntensityValue", 1);
        if (colorsIntensity < 0 || colorsIntensity > 2) throw Invalid{};
        const auto backfaceNormalMode = scalar(text, "_RenderNormals", 0);
        if (backfaceNormalMode < 0 || backfaceNormalMode > 2 || backfaceNormalMode != std::floor(backfaceNormalMode))
            throw Invalid{};
        definition["vegetationSurface"] = Value::Object{
            {"sourceFamily", "tve-12"},
            {"overlay", unit(scalar(text, "_GlobalOverlay", 1))},
            {"wetness", unit(scalar(text, "_GlobalWetness", 1))},
            {"overlayVariation", unit(scalar(text, "_OverlayVariationValue", .5))},
            {"overlayProjection", unit(scalar(text, "_OverlayProjectionValue", .5))},
            {"vertexOcclusionAlpha", unit(occlusionColor[3])},
            {"vertexOcclusionColor", Value::Array{occlusionColor[0], occlusionColor[1], occlusionColor[2]}},
            {"invertVertexOcclusion", scalar(text, "_VertexOcclusionOverlayMode", 0) == 1},
            {"colors", unit(scalar(text, "_GlobalColors", 1))},
            {"colorsIntensity", colorsIntensity},
            {"colorsMask", unit(scalar(text, "_ColorsMaskValue", 1))},
            {"colorsVariation", unit(scalar(text, "_ColorsVariationValue", .5))},
            {"vertexOcclusionMinimum", colorsOcclusionMinimum},
            {"vertexOcclusionMaximum", colorsOcclusionMaximum},
            {"invertVertexOcclusionColors", colorsOcclusionMode == 1},
            {"backfaceNormalMode", int64_t(backfaceNormalMode)}};
        const auto detailMode = scalar(text, "_DetailMode", 0);
        if (detailMode != 0 && detailMode != 1) throw Invalid{};
        if (detailMode == 1) {
            const auto detailUv       = vector(text, "_SecondUVs", {1, 1, 0, 0});
            const auto detailColor    = vector(text, "_SecondColor", {1, 1, 1, 1});
            const auto detailColorTwo = vector(text, "_SecondColorTwo", {1, 1, 1, 1});
            const auto uvMode         = scalar(text, "_SecondUVsMode", 0);
            if (uvMode < 0 || uvMode > 2 || uvMode != std::floor(uvMode)) throw Invalid{};
            auto signedNormal = scalar(text, "_SecondNormalValue", 1);
            if (signedNormal < -8 || signedNormal > 8 || detailUv[0] == 0 || detailUv[1] == 0) throw Invalid{};
            definition["vegetationDetail"] = Value::Object{
                {"value", unit(scalar(text, "_DetailValue", 1))},
                {"uvMode", int64_t(uvMode)},
                {"inverseUvScale", binary("_SecondUVsScaleMode", 0) == 1},
                {"uvScale", Value::Array{detailUv[0], detailUv[1]}},
                {"uvOffset", Value::Array{detailUv[2], 1.0 - detailUv[1] - detailUv[3]}},
                {"color", Value::Array{detailColor[0], detailColor[1], detailColor[2], detailColor[3]}},
                {"colorTwo", Value::Array{detailColorTwo[0], detailColorTwo[1], detailColorTwo[2], detailColorTwo[3]}},
                {"colorMode", int64_t(binary("_SecondColorMode", 0))},
                {"albedoValue", unit(scalar(text, "_SecondAlbedoValue", 1))},
                {"normalValue", signedNormal},
                {"normalBlendValue", unit(scalar(text, "_DetailNormalValue", 1))},
                {"metallicValue", unit(scalar(text, "_SecondMetallicValue", 0))},
                {"occlusionValue", unit(scalar(text, "_SecondOcclusionValue", 1))},
                {"smoothnessValue", unit(scalar(text, "_SecondSmoothnessValue", 1))},
                {"blendMode", int64_t(binary("_DetailBlendMode", 0))},
                {"alphaMode", int64_t(binary("_DetailAlphaMode", 1))},
                {"maskMode", int64_t(binary("_DetailMaskMode", 0))},
                {"meshMode", int64_t(binary("_DetailMeshMode", 0))},
                {"blendMinimum", unit(scalar(text, "_DetailBlendMinValue", 0))},
                {"blendMaximum", unit(scalar(text, "_DetailBlendMaxValue", 1))},
                {"maskMinimum", unit(scalar(text, "_DetailMaskMinValue", 0))},
                {"maskMaximum", unit(scalar(text, "_DetailMaskMaxValue", 1))},
                {"meshMinimum", unit(scalar(text, "_DetailMeshMinValue", 0))},
                {"meshMaximum", unit(scalar(text, "_DetailMeshMaxValue", 1))}};
        }
        auto bindWithUv = [&](const std::string& role, const AssetRef& image, const std::array<double, 4>& transform) {
            definition[role] = image.format();
            definition[role + "Transform"] =
                Value::Object{{"scale", Value::Array{transform[0], transform[1]}},
                              {"offset", Value::Array{transform[2], 1.0 - transform[1] - transform[3]}}};
            out.manifest.dependencies.push_back(
                {ref.value(), image, asset::EvaDependencyKind::RuntimeRequired, role, {}, "eve.image/3", {}});
        };
        auto bind = [&](const std::string& role, const AssetRef& image) { bindWithUv(role, image, uv); };
        if (auto guid = texture(text, "_MainAlbedoTex")) {
            auto path = texturePath(request, *guid);
            if (!path) return Result<PreparedAssetImport>::failure(path.status());
            auto image = detail::assetRef(request.package.packageId.child("unity:" + *guid).child("image:default"));
            if (!image) return Result<PreparedAssetImport>::failure(image.status());
            bind("baseColorTexture", image.value());
            auto sampler = textureSampler(request, path.value());
            if (!sampler) return Result<PreparedAssetImport>::failure(sampler.status());
            definition["baseColorTextureSampler"] = std::move(sampler).takeValue();
        }
        if (emissiveMode == 1) {
            auto guid = texture(text, "_EmissiveTex");
            if (!guid) throw Invalid{};
            auto sourcePath = texturePath(request, *guid);
            if (!sourcePath) return Result<PreparedAssetImport>::failure(sourcePath.status());
            auto image = detail::assetRef(request.package.packageId.child("unity:" + *guid).child("image:default"));
            if (!image) return Result<PreparedAssetImport>::failure(image.status());
            const auto emissiveUv = vector(text, "_EmissiveUVs", {1, 1, 0, 0});
            if (emissiveUv[0] == 0 || emissiveUv[1] == 0) throw Invalid{};
            bindWithUv("emissiveTexture", image.value(), emissiveUv);
            auto sampler = textureSampler(request, sourcePath.value());
            if (!sampler) return Result<PreparedAssetImport>::failure(sampler.status());
            definition["emissiveTextureSampler"] = std::move(sampler).takeValue();
        }
        if (auto guid = texture(text, "_MainNormalTex")) {
            auto path = texturePath(request, *guid);
            if (!path) return Result<PreparedAssetImport>::failure(path.status());
            const auto&       bytes = request.files.at(path.value() + ".meta");
            const std::string meta(bytes.begin(), bytes.end());
            const auto        normalType = importerScalar(meta, "textureType", 0);
            const auto        fromHeight = importerScalar(meta, "convertToNormalMap", 0);
            const auto        flipGreen  = importerScalar(meta, "flipGreenChannel", 0);
            if (normalType == 1 && (fromHeight == 0 || fromHeight == 1) && (flipGreen == 0 || flipGreen == 1)) {
                auto sampler = textureSampler(request, path.value());
                if (!sampler) return Result<PreparedAssetImport>::failure(sampler.status());
                definition["normalTextureSampler"] = std::move(sampler).takeValue();
                const auto strength                = scalar(text, "_MainNormalValue", 1);
                if (strength < -8 || strength > 8) throw Invalid{};
                auto image = detail::assetRef(request.package.packageId.child("unity:" + *guid).child("image:default"));
                if (!image) return Result<PreparedAssetImport>::failure(image.status());
                {
                    const auto scale  = importerScalar(meta, "heightScale", .25);
                    const auto filter = importerScalar(meta, "normalMapFilter", 0);
                    if (scale < 0 || scale > 1 || (filter != 0 && filter != 1)) throw Invalid{};
                    auto generated = normalImage(request, path.value(), id, scale, filter == 1,
                                                 importerScalar(meta, "enableMipMap", 1) == 1, flipGreen == 1,
                                                 fromHeight == 1 ? NormalSource::Height : NormalSource::Authored);
                    if (!generated) return generated;
                    image = Result<AssetRef>::success(generated.value().manifest.assets.front().asset);
                    for (auto& a : generated.value().manifest.assets) out.manifest.assets.push_back(std::move(a));
                    for (auto& e : generated.value().entries)
                        if (e.path.starts_with("assets/")) out.entries.push_back(std::move(e));
                    for (auto& finding : generated.value().findings) out.findings.push_back(std::move(finding));
                }
                bind("normalTexture", image.value());
                definition["normalEncoding"] = "tve-rg";
                definition["normalScale"]    = strength;
                out.findings.push_back(
                    {source.path, "Material.TVE.normal", ImportDisposition::Translated,
                     "authored or height-generated RG linked as linear data; signed strength and fixed-Z "
                     "reconstruction run after filtering"});
            } else {
                out.findings.push_back(
                    {source.path, "Material.TVE.normalImporter", ImportDisposition::Unsupported,
                     "normal source retained; invalid settings or non-normal importer encoding requires conversion"});
            }
        }
        if (*shader == "a933075b367f9b24981408633f72ff34" || *shader == "d9a724745053dee46bf301b216cdd348") {
            auto tint = vector(text, "_SubsurfaceColor", {1, 1, 1, 1});
            for (size_t c = 0; c < 3; ++c) {
                if (tint[c] < 0) throw Invalid{};
            }
            const auto power = scalar(text, "_SubsurfaceScatteringValue", 2);
            const auto angle = scalar(text, "_SubsurfaceAngleValue", 8);
            if (power < 0 || power > 16 || angle < 1 || angle > 16) throw Invalid{};
            definition["translucency"] = Value::Object{
                {"color", Value::Array{tint[0], tint[1], tint[2]}},
                {"intensity", unit(scalar(text, "_SubsurfaceValue", 1))},
                {"strength", power},
                {"scattering", angle},
                {"normalDistortion", unit(scalar(text, "_SubsurfaceNormalValue", 0))},
                {"direct", unit(scalar(text, "_SubsurfaceDirectValue", 1))},
                {"ambient", unit(scalar(text, "_SubsurfaceAmbientValue", .2))},
                {"shadow", unit(scalar(text, "_SubsurfaceShadowValue", 1))},
                {"maskAmount",
                 *shader == "a933075b367f9b24981408633f72ff34" ? unit(scalar(text, "_SubsurfaceMaskValue", 1)) : 0},
                {"maskMinimum", minimum},
                {"maskMaximum", maximum}};
            out.findings.push_back({source.path, "Material.TVE.translucency", ImportDisposition::Translated,
                                    "visible subsurface controls mapped to per-light translucency; field wetness is "
                                    "composed by the vegetation runtime"});
        }
        if (*shader == "a933075b367f9b24981408633f72ff34" || *shader == "7befaa6f41d00a6478d5f4af21d66518") {
            auto highlight = vector(text, "_MotionHighlightColor", {0, 0, 0, 0});
            for (size_t c = 0; c < 3; ++c) {
                if (highlight[c] < 0) throw Invalid{};
            }
            definition["motionHighlightColor"] = Value::Array{highlight[0], highlight[1], highlight[2]};
            out.findings.push_back(
                {source.path, "Material.TVE.motionHighlight", ImportDisposition::Translated,
                 "Plant motion highlight RGB retained; runtime requires evaluated per-vertex highlight stream"});
        }
        if (detailMode == 1) {
            for (const auto& [property, role] :
                 std::array<std::pair<const char*, const char*>, 3>{{{"_SecondAlbedoTex", "detailAlbedoTexture"},
                                                                     {"_SecondNormalTex", "detailNormalTexture"},
                                                                     {"_SecondMaskTex", "detailMaskTexture"}}}) {
                auto guid = texture(text, property);
                if (!guid) continue;
                auto path = texturePath(request, *guid);
                if (!path) return Result<PreparedAssetImport>::failure(path.status());
                auto image = detail::assetRef(request.package.packageId.child("unity:" + *guid).child("image:default"));
                if (!image) return Result<PreparedAssetImport>::failure(image.status());
                definition[role] = image.value().format();
                auto sampler     = textureSampler(request, path.value());
                if (!sampler) return Result<PreparedAssetImport>::failure(sampler.status());
                definition[std::string(role) + "Sampler"] = std::move(sampler).takeValue();
                out.manifest.dependencies.push_back({ref.value(),
                                                     image.value(),
                                                     asset::EvaDependencyKind::RuntimeRequired,
                                                     role,
                                                     {},
                                                     "eve.image/3",
                                                     {}});
            }
        }
        auto orm = ormImage(request, text, id, occlusion, smoothness);
        if (!orm) return orm;
        const auto mask = orm.value().manifest.assets.front().asset;
        bind("metallicRoughnessTexture", mask);
        bind("occlusionTexture", mask);
        if (auto guid = texture(text, "_MainMaskTex")) {
            auto path = texturePath(request, *guid);
            if (!path) return Result<PreparedAssetImport>::failure(path.status());
            auto sampler = textureSampler(request, path.value());
            if (!sampler) return Result<PreparedAssetImport>::failure(sampler.status());
            definition["metallicRoughnessTextureSampler"] = sampler.value();
            definition["occlusionTextureSampler"]         = std::move(sampler).takeValue();
        }
        for (auto& a : orm.value().manifest.assets) out.manifest.assets.push_back(std::move(a));
        for (auto& e : orm.value().entries)
            if (e.path.starts_with("assets/")) out.entries.push_back(std::move(e));
        if (dual == 1)
            definition["colorMask"] = Value::Object{{"secondary", Value::Array{second[0], second[1], second[2]}},
                                                    {"minimum", minimum},
                                                    {"maximum", maximum}};
        const auto path = "assets/" + id.format() + "/asset.json";
        auto       json = Value(std::move(definition)).toJson();
        if (!json) return Result<PreparedAssetImport>::failure(json.status());
        std::vector<std::uint8_t> encoded(json.value().begin(), json.value().end());
        out.manifest.assets.push_back({ref.value(),
                                       "eve.material",
                                       SchemaVersion(15),
                                       path,
                                       detail::sha256(encoded),
                                       {"material", "source:unity", "vegetation"}});
        out.entries.push_back({path, std::move(encoded)});
        out.manifest.entrypoints.emplace("default", ref.value());
        out.sourceMappings.push_back({"2100000", ref.value()});
        out.findings.push_back({source.path, "Material.TVE.main", ImportDisposition::Translated,
                                "TVE 12.6.0 primary RGB, albedo, UV transform, AO/smoothness and RGB mask converted"});
        out.findings.push_back({source.path, "Material.TVE.renderState", ImportDisposition::Translated,
                                "specular, facing normal, culling and alpha-to-coverage state translated; "
                                "Direct/Ambient/Shadow and generated noise/rim declarations are inactive in the "
                                "inspected 12.6 shader calculations"});
        return Result<PreparedAssetImport>::success(std::move(out));
    } catch (const Invalid&) {
        return Result<PreparedAssetImport>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "invalid TVE material saved properties", source.path, {}, "asset.import"));
    }
}
}  // namespace eve::asset_import
