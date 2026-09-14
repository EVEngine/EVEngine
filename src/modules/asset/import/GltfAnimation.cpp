#include "asset/import/GltfAnimation.h"
#include "asset/import/GltfDecode.h"
#include "asset/import/ImportCommon.h"

#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>

namespace eve::asset_import::gltf {
namespace {
// Local parser unwinding only. The public boundary returns a checked Result;
// callers never observe a partially prepared archive.
struct Rejection {
    DiagnosticCode code;
    std::string    message;
};
void require(bool condition, std::string message, DiagnosticCode code = DiagnosticCode::ParseError) {
    if (!condition) throw Rejection{code, std::move(message)};
}
const Value::Object& object(const Value& value) {
    const auto* result = value.getIf<Value::Object>();
    require(result != nullptr, "glTF skeletal field must be an object");
    return *result;
}
const Value::Object& object(const Value* value) {
    require(value != nullptr, "required glTF skeletal object is missing");
    return object(*value);
}
const Value::Array& array(const Value* value) {
    const auto* result = value ? value->getIf<Value::Array>() : nullptr;
    require(result != nullptr, "glTF skeletal field must be an array");
    return *result;
}
std::uint32_t index(const Value* value, std::size_t bound) {
    require(value && value->isInt64() && value->asInt() >= 0 && std::uint64_t(value->asInt()) < bound,
            "glTF skeletal index is out of range");
    return static_cast<std::uint32_t>(value->asInt());
}
float number(const Value& value) {
    require(value.isInt64() || value.isDouble(), "glTF transform component must be numeric");
    const auto result = static_cast<float>(value.isInt64() ? double(value.asInt()) : value.asDouble());
    require(std::isfinite(result), "glTF transform component must be finite");
    return result;
}
std::string name(const Value::Object& value, std::string fallback, const AssetImportLimits& limits) {
    const auto* field = member(value, "name");
    if (!field) return fallback;
    require(field->isString() && field->asString().size() <= limits.maximumStringBytes,
            "glTF name exceeds string budget");
    return field->asString();
}
void putString(std::vector<std::uint8_t>& bytes, const std::string& text) {
    put32(bytes, static_cast<std::uint32_t>(text.size()));
    bytes.insert(bytes.end(), text.begin(), text.end());
}
Accessor accessor(const Value::Object& root, const std::vector<std::span<const std::uint8_t>>& buffers,
                  const Value* value, const AssetImportLimits& limits) {
    const auto i      = index(value, array(member(root, "accessors")).size());
    auto       parsed = accessorAt(root, buffers, i, limits);
    if (!parsed) throw Rejection{DiagnosticCode::ParseError, "invalid glTF skeletal accessor " + std::to_string(i)};
    return std::move(parsed).takeValue();
}
float component(const Accessor& a, std::uint32_t row, std::uint32_t column) {
    auto value = readFloat(a, row, column);
    if (!value) throw Rejection{DiagnosticCode::ParseError, "invalid glTF skeletal FLOAT component"};
    return value.value();
}
std::uint32_t integerComponent(const Accessor& a, std::uint32_t row, std::uint32_t column) {
    require(a.componentType == 5121 || a.componentType == 5123, "JOINTS must use unsigned bytes or shorts");
    auto offset = a.offset + row * a.stride + column * componentSize(a.componentType);
    return a.componentType == 5121 ? a.buffer[offset]
                                   : std::uint32_t(a.buffer[offset]) | (std::uint32_t(a.buffer[offset + 1]) << 8);
}
AssetRef reference(PersistentId id) {
    auto ref = detail::assetRef(id);
    require(ref.ok(), "invalid generated asset identity");
    return std::move(ref).takeValue();
}
void publish(const GltfImportRequest& request, PreparedAssetImport& output, const AssetRef& ref, std::string type,
             Value::Object definition, std::vector<std::uint8_t> blob, std::string source) {
    require(output.manifest.assets.size() < request.limits.maximumAssets, "glTF asset budget exceeded");
    std::uint64_t total = blob.size();
    for (const auto& entry : output.entries) {
        require(entry.bytes.size() <= request.limits.maximumDecodedBytes &&
                    total <= request.limits.maximumDecodedBytes - entry.bytes.size(),
                "glTF decoded budget exceeded");
        total += entry.bytes.size();
    }
    require(total <= request.limits.maximumDecodedBytes, "glTF decoded budget exceeded");
    const auto base             = "assets/" + ref.id().format() + "/";
    definition["schema"]        = Value(type);
    definition["schemaVersion"] = Value(std::int64_t(1));
    definition["blob"]          = Value(base + "data.bin");
    auto json                   = Value(std::move(definition)).toJson();
    require(json.ok(), "cannot encode canonical skeletal definition");
    std::vector<std::uint8_t> bytes(json.value().begin(), json.value().end());
    output.manifest.assets.push_back(
        {ref, type, SchemaVersion(1), base + "asset.json", detail::sha256(bytes), {"source:gltf2"}});
    output.entries.push_back({base + "asset.json", std::move(bytes)});
    output.entries.push_back({base + "data.bin", std::move(blob)});
    output.sourceMappings.push_back({source, ref});
    output.findings.push_back({request.sourceName, source, ImportDisposition::Translated, "converted to " + type});
}
void dependency(PreparedAssetImport& output, const AssetRef& from, const AssetRef& to, std::string role,
                std::string type) {
    output.manifest.dependencies.push_back(
        {from, to, asset::EvaDependencyKind::RuntimeRequired, std::move(role), {}, std::move(type)});
}

void append(const GltfImportRequest& request, const Value::Object& root,
            const std::vector<std::span<const std::uint8_t>>& buffers, PreparedAssetImport& output) {
    const auto* skinValue      = member(root, "skins");
    const auto* animationValue = member(root, "animations");
    if (!skinValue && !animationValue) return;
    const auto& nodes = array(member(root, "nodes"));
    require(!nodes.empty() && nodes.size() <= request.limits.maximumAssets, "glTF node budget exceeded");
    std::vector<int> parents(nodes.size(), -1), mapped(nodes.size(), -1);
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const auto& node = object(nodes[n]);
        require(!member(node, "matrix"), "skeletal matrix nodes require TRS conversion", DiagnosticCode::Unsupported);
        if (auto* children = member(node, "children"))
            for (const auto& child : array(children)) {
                const auto c = index(&child, nodes.size());
                require(c != n && parents[c] == -1, "glTF nodes contain a cycle or multiple parents");
                parents[c] = static_cast<int>(n);
            }
    }
    // Breadth-first order includes non-joint ancestors. Bind and animated local
    // transforms therefore retain their original parent space without baking.
    std::vector<std::uint32_t> order;
    for (std::uint32_t n = 0; n < nodes.size(); ++n)
        if (parents[n] == -1) order.push_back(n);
    for (std::size_t cursor = 0; cursor < order.size(); ++cursor) {
        auto n    = order[cursor];
        mapped[n] = static_cast<int>(cursor);
        if (auto* children = member(object(nodes[n]), "children"))
            for (const auto& child : array(children)) order.push_back(index(&child, nodes.size()));
    }
    require(order.size() == nodes.size(), "glTF node hierarchy contains a cycle");
    const auto                skeleton = reference(request.package.packageId.child("gltf:skeleton"));
    std::vector<std::uint8_t> skeletonBytes{'E', 'V', 'S', 'K', 'E', 'L', 0, 1};
    put32(skeletonBytes, static_cast<std::uint32_t>(nodes.size()));
    for (auto n : order) {
        const auto&         node      = object(nodes[n]);
        const auto          boneName  = name(node, "node_" + std::to_string(n), request.limits);
        const std::uint64_t boneBytes = 48 + boneName.size();
        require(boneBytes <= request.limits.maximumDecodedBytes &&
                    skeletonBytes.size() <= request.limits.maximumDecodedBytes - boneBytes,
                "skeleton decoded budget exceeded");
        put32(skeletonBytes, parents[n] < 0 ? std::uint32_t(-1) : std::uint32_t(mapped[parents[n]]));
        putString(skeletonBytes, boneName);
        for (const auto& field : {"translation", "rotation", "scale"}) {
            const int   count = std::string_view(field) == "rotation" ? 4 : 3;
            const auto* value = member(node, field);
            if (value) require(array(value).size() == std::size_t(count), "invalid glTF TRS shape");
            if (value && count == 4) {
                double length = 0;
                for (const auto& part : array(value)) {
                    const auto x = number(part);
                    length += double(x) * x;
                }
                require(std::abs(length - 1.0) < 0.001, "bind rotation must be a unit quaternion");
            }
            for (int c = 0; c < count; ++c) {
                const float fallback = std::string_view(field) == "scale" || c == 3 ? 1.f : 0.f;
                putFloat(skeletonBytes, value ? number(array(value)[c]) : fallback);
            }
        }
    }
    publish(request, output, skeleton, "eve.skeleton",
            {{"boneCount", Value(std::int64_t(nodes.size()))},
             {"coordinateSystem", Value("right-handed-x-right-y-up-minus-z-forward")},
             {"lengthUnit", Value("meter")}},
            std::move(skeletonBytes), "nodes");

    if (skinValue) {
        const auto& skins  = array(skinValue);
        const auto& meshes = array(member(root, "meshes"));
        for (std::uint32_t n = 0; n < nodes.size(); ++n) {
            const auto& node = object(nodes[n]);
            if (!member(node, "skin")) continue;
            const auto  skinIndex = index(member(node, "skin"), skins.size());
            const auto  meshIndex = index(member(node, "mesh"), meshes.size());
            const auto& skin      = object(skins[skinIndex]);
            const auto& joints    = array(member(skin, "joints"));
            require(!joints.empty() && joints.size() <= nodes.size(), "invalid glTF joint count");
            std::set<std::uint32_t> unique;
            for (const auto& j : joints) require(unique.insert(index(&j, nodes.size())).second, "duplicate glTF joint");
            std::optional<Accessor> inverse;
            if (auto* ibm = member(skin, "inverseBindMatrices")) {
                inverse = accessor(root, buffers, ibm, request.limits);
                require(inverse->components == 16 && inverse->componentType == 5126 && inverse->count == joints.size(),
                        "invalid inverse bind matrices");
            }
            const auto& primitives = array(member(object(meshes[meshIndex]), "primitives"));
            for (std::uint32_t p = 0; p < primitives.size(); ++p) {
                const auto& primitive = object(primitives[p]);
                const auto& attrs     = object(member(primitive, "attributes"));
                auto        position  = accessor(root, buffers, member(attrs, "POSITION"), request.limits);
                std::vector<std::pair<Accessor, Accessor>> sets;
                for (std::uint32_t set = 0;; ++set) {
                    const auto* j = member(attrs, "JOINTS_" + std::to_string(set));
                    const auto* w = member(attrs, "WEIGHTS_" + std::to_string(set));
                    if (!j && !w) break;
                    require(j && w && set < 64, "incomplete or excessive glTF influence sets");
                    auto ja = accessor(root, buffers, j, request.limits),
                         wa = accessor(root, buffers, w, request.limits);
                    require(ja.components == 4 && wa.components == 4 && ja.count == position.count &&
                                wa.count == position.count,
                            "glTF skin attributes must match vertices");
                    require(!ja.normalized, "JOINTS cannot be normalized");
                    require((wa.componentType == 5126 && !wa.normalized) ||
                                ((wa.componentType == 5121 || wa.componentType == 5123) && wa.normalized),
                            "WEIGHTS must use FLOAT or normalized unsigned bytes/shorts");
                    sets.emplace_back(ja, wa);
                }
                require(!sets.empty(), "skinned primitive has no joint weights");
                for (const auto& [key, value] : attrs) {
                    (void)value;
                    if (key.starts_with("JOINTS_") || key.starts_with("WEIGHTS_")) {
                        bool found = false;
                        for (std::size_t s = 0; s < sets.size(); ++s)
                            found |= key == "JOINTS_" + std::to_string(s) || key == "WEIGHTS_" + std::to_string(s);
                        require(found, "glTF influence sets must be contiguous");
                    }
                }
                const auto influences = static_cast<std::uint32_t>(sets.size() * 4);
                require(std::uint64_t(position.count) * influences * 8 + joints.size() * 68 + 24 <=
                            request.limits.maximumDecodedBytes,
                        "skin decoded budget exceeded");
                std::vector<std::uint8_t> bytes{'E', 'V', 'S', 'K', 'I', 'N', 0, 1};
                put32(bytes, position.count);
                put32(bytes, static_cast<std::uint32_t>(joints.size()));
                put32(bytes, influences);
                put32(bytes, static_cast<std::uint32_t>(mapped[n]));
                for (std::uint32_t j = 0; j < joints.size(); ++j) {
                    put32(bytes, static_cast<std::uint32_t>(mapped[index(&joints[j], nodes.size())]));
                    for (std::uint32_t c = 0; c < 16; ++c)
                        putFloat(bytes, inverse ? component(*inverse, j, c) : (c % 5 == 0 ? 1.f : 0.f));
                }
                std::uint32_t normalizedVertices = 0;
                for (std::uint32_t v = 0; v < position.count; ++v) {
                    const auto rowStart = bytes.size();
                    float      sum      = 0;
                    for (const auto& [j, w] : sets)
                        for (std::uint32_t c = 0; c < 4; ++c) {
                            auto joint  = integerComponent(j, v, c);
                            auto weight = w.componentType == 5126 ? component(w, v, c)
                                                                  : float(integerComponent(w, v, c)) /
                                                                        (w.componentType == 5121 ? 255.f : 65535.f);
                            require(joint < joints.size() && weight >= 0, "invalid glTF joint index or weight");
                            sum += weight;
                            put32(bytes, joint);
                            putFloat(bytes, weight);
                        }
                    require(std::isfinite(sum) && sum > 0, "glTF vertex weights must have a positive finite sum");
                    if (std::abs(sum - 1.f) > 1e-6f) {
                        ++normalizedVertices;
                        for (std::uint32_t i = 0; i < influences; ++i) {
                            const auto offset     = rowStart + i * 8 + 4;
                            const auto normalized = std::bit_cast<float>(little32(bytes, offset)) / sum;
                            const auto bits       = std::bit_cast<std::uint32_t>(normalized);
                            for (int byte = 0; byte < 4; ++byte)
                                bytes[offset + byte] = static_cast<std::uint8_t>(bits >> (byte * 8));
                        }
                    }
                }
                auto meshRef      = reference(request.package.packageId.child("gltf:mesh:" + std::to_string(meshIndex) +
                                                                              ":primitive:" + std::to_string(p)));
                const auto source = "nodes[" + std::to_string(n) + "].skin.primitives[" + std::to_string(p) + "]";
                auto       skinRef = reference(request.package.packageId.child("gltf:" + source));
                if (normalizedVertices)
                    output.findings.push_back({request.sourceName, source + ".weights", ImportDisposition::Baked,
                                               "renormalized all influence sets for " +
                                                   std::to_string(normalizedVertices) +
                                                   " vertices with non-unit source weight sums"});
                publish(request, output, skinRef, "eve.skin",
                        {{"skeleton", Value(skeleton.format())},
                         {"mesh", Value(meshRef.format())},
                         {"vertexCount", Value(std::int64_t(position.count))},
                         {"jointCount", Value(std::int64_t(joints.size()))},
                         {"influencesPerVertex", Value(std::int64_t(influences))}},
                        std::move(bytes), source);
                dependency(output, skinRef, skeleton, "skeleton", "eve.skeleton/1");
                dependency(output, skinRef, meshRef, "mesh", "eve.mesh/2");
                output.manifest.entrypoints.emplace("skin:" + std::to_string(n) + ":" + std::to_string(p), skinRef);
            }
        }
    }
    if (!animationValue) return;
    const auto& animations = array(animationValue);
    for (std::uint32_t a = 0; a < animations.size(); ++a) {
        const auto& animation = object(animations[a]);
        const auto& samplers  = array(member(animation, "samplers"));
        const auto& channels  = array(member(animation, "channels"));
        struct Track {
            std::array<std::vector<std::uint8_t>, 3> keys;
            std::array<std::uint32_t, 3>             counts{};
        };
        std::map<std::uint32_t, Track> tracks;
        float                          duration     = 0;
        float                          minimumStep  = std::numeric_limits<float>::max();
        std::uint64_t                  decodedBytes = 28;
        for (const auto& c : channels) {
            const auto& channel = object(c);
            const auto& target  = object(member(channel, "target"));
            auto        node    = index(member(target, "node"), nodes.size());
            const auto* path    = member(target, "path");
            require(path && path->isString(), "animation target path is missing");
            const auto& pathName = path->asString();
            int kind = pathName == "translation" ? 0 : pathName == "rotation" ? 1 : pathName == "scale" ? 2 : -1;
            require(kind >= 0, "animation target " + pathName + " is unsupported", DiagnosticCode::Unsupported);
            const auto& sampler = object(samplers[index(member(channel, "sampler"), samplers.size())]);
            if (auto* interpolation = member(sampler, "interpolation"))
                require(interpolation->isString() && interpolation->asString() == "LINEAR",
                        "only LINEAR skeletal interpolation is supported", DiagnosticCode::Unsupported);
            auto input  = accessor(root, buffers, member(sampler, "input"), request.limits);
            auto values = accessor(root, buffers, member(sampler, "output"), request.limits);
            require(input.components == 1 && input.count == values.count && values.components == (kind == 1 ? 4u : 3u),
                    "invalid animation sampler shape");
            auto& track = tracks[static_cast<std::uint32_t>(mapped[node])];
            require(track.counts[kind] == 0, "duplicate animation channel target");
            const auto size = std::uint64_t(input.count) * (values.components + 1) * 4;
            require(
                size <= request.limits.maximumDecodedBytes && decodedBytes <= request.limits.maximumDecodedBytes - size,
                "animation decoded budget exceeded");
            decodedBytes += size;
            track.counts[kind] = input.count;
            float previous     = -1;
            for (std::uint32_t key = 0; key < input.count; ++key) {
                auto time = component(input, key, 0);
                require(time >= 0 && time > previous, "animation times must be strictly increasing and non-negative");
                if (key > 0) minimumStep = std::min(minimumStep, time - previous);
                previous = time;
                duration = std::max(duration, time);
                putFloat(track.keys[kind], time);
                if (kind == 1) {
                    double length = 0;
                    for (std::uint32_t i = 0; i < 4; ++i) {
                        auto x = component(values, key, i);
                        length += double(x) * x;
                    }
                    require(std::abs(length - 1.0) < 0.001, "animation rotation must be a unit quaternion");
                }
                for (std::uint32_t componentIndex = 0; componentIndex < values.components; ++componentIndex)
                    putFloat(track.keys[kind], component(values, key, componentIndex));
            }
        }
        std::vector<std::uint8_t> bytes{'E', 'V', 'A', 'N', 'I', 'M', 0, 1};
        const float               rate = minimumStep == std::numeric_limits<float>::max() ? 1.f : 1.f / minimumStep;
        require(std::isfinite(rate), "animation sample rate is not representable");
        putFloat(bytes, duration);
        putFloat(bytes, rate);
        put32(bytes, 0);
        put32(bytes, 0);
        put32(bytes, static_cast<std::uint32_t>(tracks.size()));
        for (auto& [bone, track] : tracks) {
            put32(bytes, bone);
            for (auto count : track.counts) put32(bytes, count);
            for (const auto& keys : track.keys) bytes.insert(bytes.end(), keys.begin(), keys.end());
        }
        const auto source = "animations[" + std::to_string(a) + "]";
        const auto ref    = reference(request.package.packageId.child("gltf:" + source));
        publish(request, output, ref, "eve.animation-clip",
                {{"name", Value(name(animation, source, request.limits))},
                 {"durationSeconds", Value(double(duration))},
                 {"sampleRate", Value(double(rate))},
                 {"loop", Value(false)},
                 {"skeleton", Value(skeleton.format())}},
                std::move(bytes), source);
        dependency(output, ref, skeleton, "skeleton", "eve.skeleton/1");
        output.manifest.entrypoints.emplace("animation:" + std::to_string(a), ref);
    }
}
}  // namespace

Result<void> appendAnimation(const GltfImportRequest& request, const Value::Object& root,
                             const std::vector<std::span<const std::uint8_t>>& buffers, PreparedAssetImport& output) {
    try {
        append(request, root, buffers, output);
    } catch (const Rejection& rejected) {
        return detail::failure<void>(rejected.code, rejected.message, request.sourceName);
    }
    return Result<void>::success();
}
}  // namespace eve::asset_import::gltf
