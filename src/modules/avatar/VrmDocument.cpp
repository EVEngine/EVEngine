#include "avatar/VrmDocument.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include "common/Json.h"

namespace eve::avatar {
namespace {
using J = eve::json::Value;
struct Invalid : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void require(bool value, const char* message) {
    if (!value) throw Invalid(message);
}
uint32_t u32(std::span<const std::byte> bytes, size_t i) {
    require(i <= bytes.size() && bytes.size() - i >= 4, "Truncated GLB integer");
    return uint32_t(bytes[i]) | uint32_t(bytes[i + 1]) << 8 | uint32_t(bytes[i + 2]) << 16 |
           uint32_t(bytes[i + 3]) << 24;
}
int index(J value, size_t size) {
    require(value.isInt64(), "Reference must be an integer");
    const auto n = value.asInt64(-1);
    require(n >= 0 && static_cast<uint64_t>(n) < size, "Reference is out of range");
    return static_cast<int>(n);
}
float number(J value, float fallback) {
    if (!value) return fallback;
    require(value.isNumber() && std::isfinite(value.asDouble()) &&
                std::abs(value.asDouble()) <= std::numeric_limits<float>::max(),
            "Expected finite number");
    return value.asFloat();
}
template <size_t N>
std::array<float, N> vector(J value, std::array<float, N> fallback) {
    if (!value) return fallback;
    require(value.isArray() && value.size() == N, "Invalid vector size");
    for (size_t i = 0; i < N; ++i) fallback[i] = number(value.at(i), 0);
    return fallback;
}
std::string choice(J value, std::string fallback, std::initializer_list<const char*> allowed) {
    if (!value) return fallback;
    require(value.isString(), "Expected enum string");
    auto s = value.asString();
    for (auto x : allowed)
        if (s == x) return s;
    throw Invalid("Unknown enum value: " + s);
}
VrmTexture texture(J info, J root) {
    VrmTexture t;
    if (!info) return t;
    const auto textures   = root.get("textures");
    auto       definition = textures.at(index(info.get("index"), textures.size()));
    t.image               = index(definition.get("source"), root.get("images").size());
    require(info.getInt("texCoord", 0) == 0, "VRM texture requires unsupported UV channel");
    if (definition.has("sampler")) {
        auto sampler = root.get("samplers").at(index(definition.get("sampler"), root.get("samplers").size()));
        t.wrapS      = sampler.getInt("wrapS", 10497);
        t.wrapT      = sampler.getInt("wrapT", 10497);
    }
    auto x     = info.get("extensions").get("KHR_texture_transform");
    t.offset   = vector<2>(x.get("offset"), t.offset);
    t.scale    = vector<2>(x.get("scale"), t.scale);
    t.rotation = number(x.get("rotation"), 0);
    require(x.getInt("texCoord", 0) == 0, "Texture transform requires unsupported UV channel");
    return t;
}
void readMaterials(VrmDocument& d, J root) {
    auto materials = root.get("materials");
    for (size_t i = 0; i < materials.size(); ++i) {
        auto        j = materials.at(i), p = j.get("pbrMetallicRoughness");
        VrmMaterial m;
        m.name          = j.getString("name");
        m.color         = vector<4>(p.get("baseColorFactor"), m.color);
        m.emission      = vector<3>(j.get("emissiveFactor"), m.emission);
        m.alphaMode     = choice(j.get("alphaMode"), "OPAQUE", {"OPAQUE", "MASK", "BLEND"});
        m.cutoff        = number(j.get("alphaCutoff"), .5f);
        m.doubleSided   = j.getBool("doubleSided");
        m.textures[0]   = texture(p.get("baseColorTexture"), root);
        m.textures[2]   = texture(j.get("normalTexture"), root);
        m.normalScale   = number(j.get("normalTexture").get("scale"), 1);
        m.textures[4]   = texture(j.get("emissiveTexture"), root);
        auto extensions = j.get("extensions"), mt = extensions.get("VRMC_materials_mtoon");
        m.emissionStrength = number(extensions.get("KHR_materials_emissive_strength").get("emissiveStrength"), 1);
        if (mt) {
            require(mt.getString("specVersion") == "1.0", "Unknown MToon version");
            m.mtoon    = true;
            m.zWrite   = mt.getBool("transparentWithZWrite");
            m.queue    = mt.getInt("renderQueueOffsetNumber");
            m.shade    = vector<3>(mt.get("shadeColorFactor"), m.shade);
            m.matcap   = vector<3>(mt.get("matcapFactor"), m.matcap);
            m.rim      = vector<3>(mt.get("parametricRimColorFactor"), m.rim);
            m.outline  = vector<3>(mt.get("outlineColorFactor"), m.outline);
            m.shift    = number(mt.get("shadingShiftFactor"), 0);
            m.toony    = number(mt.get("shadingToonyFactor"), .9f);
            m.gi       = number(mt.get("giEqualizationFactor"), .9f);
            m.rimMix   = number(mt.get("rimLightingMixFactor"), 1);
            m.rimPower = number(mt.get("parametricRimFresnelPowerFactor"), 1);
            m.rimLift  = number(mt.get("parametricRimLiftFactor"), 0);
            m.outlineMode =
                choice(mt.get("outlineWidthMode"), "none", {"none", "worldCoordinates", "screenCoordinates"});
            m.outlineWidth      = number(mt.get("outlineWidthFactor"), 0);
            m.outlineMix        = number(mt.get("outlineLightingMixFactor"), 1);
            m.scrollX           = number(mt.get("uvAnimationScrollXSpeedFactor"), 0);
            m.scrollY           = number(mt.get("uvAnimationScrollYSpeedFactor"), 0);
            m.rotationSpeed     = number(mt.get("uvAnimationRotationSpeedFactor"), 0);
            m.textures[1]       = texture(mt.get("shadeMultiplyTexture"), root);
            m.textures[3]       = texture(mt.get("shadingShiftTexture"), root);
            m.shiftTextureScale = number(mt.get("shadingShiftTexture").get("scale"), 1);
            m.textures[5]       = texture(mt.get("matcapTexture"), root);
            m.textures[6]       = texture(mt.get("rimMultiplyTexture"), root);
            m.textures[7]       = texture(mt.get("outlineWidthMultiplyTexture"), root);
            m.textures[8]       = texture(mt.get("uvAnimationMaskTexture"), root);
        }
        d.materials.push_back(std::move(m));
    }
}
void readExpressions(VrmDocument& d, J vrm, J root) {
    auto expressions = vrm.get("expressions");
    for (auto group : {"preset", "custom"}) {
        auto items = expressions.get(group);
        for (const auto& name : items.keys()) {
            auto          j = items.get(name.c_str());
            VrmExpression e;
            e.name           = name;
            e.binary         = j.getBool("isBinary");
            e.overrideBlink  = choice(j.get("overrideBlink"), "none", {"none", "block", "blend"});
            e.overrideLookAt = choice(j.get("overrideLookAt"), "none", {"none", "block", "blend"});
            e.overrideMouth  = choice(j.get("overrideMouth"), "none", {"none", "block", "blend"});
            auto binds       = j.get("morphTargetBinds");
            for (size_t i = 0; i < binds.size(); ++i) {
                auto                 b = binds.at(i);
                VrmExpression::Morph m;
                m.node   = index(b.get("node"), d.nodes.size());
                int mesh = d.nodeMeshes[m.node];
                require(mesh >= 0, "Expression node has no mesh");
                auto primitives = root.get("meshes").at(mesh).get("primitives");
                require(primitives.size() > 0, "Expression mesh has no primitives");
                m.index = index(b.get("index"), primitives.at(0).get("targets").size());
                for (size_t p = 0; p < primitives.size(); ++p)
                    require(size_t(m.index) < primitives.at(p).get("targets").size(),
                            "Inconsistent primitive morph count");
                m.weight = number(b.get("weight"), 1);
                require(m.weight >= 0 && m.weight <= 1, "Invalid morph weight");
                e.morphs.push_back(m);
            }
            binds = j.get("materialColorBinds");
            for (size_t i = 0; i < binds.size(); ++i) {
                auto                 b = binds.at(i);
                VrmExpression::Color c;
                c.material = index(b.get("material"), d.materials.size());
                c.type     = choice(b.get("type"), "color",
                                    {"color", "emissionColor", "shadeColor", "matcapColor", "rimColor", "outlineColor"});
                c.target   = vector<4>(b.get("targetValue"), {});
                e.colors.push_back(c);
            }
            binds = j.get("textureTransformBinds");
            for (size_t i = 0; i < binds.size(); ++i) {
                auto              b = binds.at(i);
                VrmExpression::Uv u;
                u.material = index(b.get("material"), d.materials.size());
                u.scale    = vector<2>(b.get("scale"), u.scale);
                u.offset   = vector<2>(b.get("offset"), u.offset);
                e.uvs.push_back(u);
            }
            d.expressions.push_back(std::move(e));
        }
    }
}
void readSpring(VrmDocument& d, J root) {
    auto s = root.get("extensions").get("VRMC_springBone");
    if (!s) return;
    require(s.getString("specVersion") == "1.0", "Unknown spring bone version");
    auto cs = s.get("colliders");
    for (size_t i = 0; i < cs.size(); ++i) {
        auto        j = cs.at(i);
        VrmCollider c;
        c.node     = index(j.get("node"), d.nodes.size());
        auto shape = j.get("shape");
        c.capsule  = shape.has("capsule");
        require(c.capsule != shape.has("sphere"), "Collider needs exactly one shape");
        auto x   = shape.get(c.capsule ? "capsule" : "sphere");
        c.offset = vector<3>(x.get("offset"), {});
        c.tail   = vector<3>(x.get("tail"), {});
        c.radius = number(x.get("radius"), 0);
        require(c.radius >= 0, "Negative collider radius");
        d.colliders.push_back(c);
    }
    auto groups = s.get("colliderGroups"), springs = s.get("springs");
    for (size_t i = 0; i < springs.size(); ++i) {
        auto      j = springs.at(i);
        VrmSpring spring;
        spring.name = j.getString("name");
        if (j.has("center")) spring.center = index(j.get("center"), d.nodes.size());
        auto joints = j.get("joints");
        require(joints.isArray() && joints.size() > 0, "Spring has no joints");
        for (size_t n = 0; n < joints.size(); ++n) {
            auto             x = joints.at(n);
            VrmSpring::Joint q;
            q.node         = index(x.get("node"), d.nodes.size());
            q.radius       = number(x.get("hitRadius"), 0);
            q.stiffness    = number(x.get("stiffness"), 1);
            q.drag         = number(x.get("dragForce"), .5f);
            q.gravityPower = number(x.get("gravityPower"), 0);
            q.gravity      = vector<3>(x.get("gravityDir"), q.gravity);
            require(q.radius >= 0 && q.stiffness >= 0 && q.gravityPower >= 0 && q.drag >= 0 && q.drag <= 1,
                    "Invalid spring joint parameters");
            if (n) {
                int parent = d.parents[q.node];
                while (parent >= 0 && parent != spring.joints.back().node) parent = d.parents[parent];
                require(parent >= 0, "Spring joints must follow the node hierarchy");
            }
            spring.joints.push_back(q);
        }
        auto memberships = j.get("colliderGroups");
        for (size_t g = 0; g < memberships.size(); ++g) {
            auto members = groups.at(index(memberships.at(g), groups.size())).get("colliders");
            for (size_t c = 0; c < members.size(); ++c)
                spring.colliders.push_back(index(members.at(c), d.colliders.size()));
        }
        d.springs.push_back(std::move(spring));
    }
}
}  // namespace

eve::Result<VrmDocument> parseVrm(std::span<const std::byte> bytes) {
    try {
        require(bytes.size() >= 20 && u32(bytes, 0) == 0x46546c67 && u32(bytes, 4) == 2, "Expected GLB 2.0");
        require(u32(bytes, 8) == bytes.size(), "Invalid GLB length");
        const size_t length = u32(bytes, 12);
        require(u32(bytes, 16) == 0x4e4f534a && length % 4 == 0 && length <= bytes.size() - 20,
                "Invalid GLB JSON chunk");
        auto json = eve::json::Document::parse(std::string(reinterpret_cast<const char*>(bytes.data() + 20), length));
        require(json.valid(), "Invalid GLB JSON");
        auto                       root = json.root();
        std::span<const std::byte> bin;
        for (size_t at = 20 + length; at < bytes.size();) {
            const auto n = u32(bytes, at), type = u32(bytes, at + 4);
            at += 8;
            require(n % 4 == 0 && n <= bytes.size() - at, "Invalid GLB chunk");
            if (type == 0x004e4942) {
                require(bin.empty(), "Duplicate BIN chunk");
                bin = bytes.subspan(at, n);
            }
            at += n;
        }
        auto vrm = root.get("extensions").get("VRMC_vrm");
        if (!vrm || vrm.getString("specVersion") != "1.0")
            return eve::Result<VrmDocument>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::UnknownVersion, "Expected VRMC_vrm 1.0", "extensions.VRMC_vrm.specVersion"));
        VrmDocument d;
        d.version  = "1.0";
        auto nodes = root.get("nodes"), meshes = root.get("meshes");
        require(nodes.isArray(), "Missing nodes");
        d.parents.resize(nodes.size(), -1);
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto node = nodes.at(i);
            d.nodes.push_back(node.getString("name"));
            d.nodeMeshes.push_back(node.has("mesh") ? index(node.get("mesh"), meshes.size()) : -1);
            auto children = node.get("children");
            for (size_t k = 0; k < children.size(); ++k) {
                int child = index(children.at(k), nodes.size());
                require(d.parents[child] == -1 && child != int(i), "Invalid node hierarchy");
                d.parents[child] = int(i);
            }
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            int    p     = int(i);
            size_t steps = 0;
            while (p >= 0) {
                require(++steps <= nodes.size(), "Cyclic node hierarchy");
                p = d.parents[p];
            }
        }
        auto humans = vrm.get("humanoid").get("humanBones");
        require(humans.isObject(), "Missing humanoid mapping");
        for (const auto& name : humans.keys())
            d.humanoid[name] = index(humans.get(name.c_str()).get("node"), nodes.size());
        auto images = root.get("images"), views = root.get("bufferViews");
        for (size_t i = 0; i < images.size(); ++i) {
            auto v = views.at(index(images.at(i).get("bufferView"), views.size()));
            require(v.getInt("buffer", 0) == 0, "VRM image must use GLB buffer");
            auto start = v.get("byteOffset").asInt64(0), n = v.get("byteLength").asInt64(-1);
            require(start >= 0 && n > 0 && uint64_t(start) <= bin.size() && uint64_t(n) <= bin.size() - size_t(start),
                    "Image buffer view is out of range");
            auto image = bin.subspan(size_t(start), size_t(n));
            d.images.emplace_back(image.begin(), image.end());
        }
        // The decoder requires an explicit palette accessor; reject before entering it.
        auto skins = root.get("skins"), accessors = root.get("accessors");
        for (size_t i = 0; i < skins.size(); ++i) {
            auto skin = skins.at(i);
            if (!skin.has("inverseBindMatrices"))
                return eve::Result<VrmDocument>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Unsupported, "Skin requires explicit inverseBindMatrices", "skins"));
            auto accessor = accessors.at(index(skin.get("inverseBindMatrices"), accessors.size()));
            require(accessor.getString("type") == "MAT4" && accessor.getInt("componentType") == 5126,
                    "Invalid inverse bind matrix accessor");
            require(accessor.getInt("count") >= static_cast<int>(skin.get("joints").size()),
                    "Inverse bind matrix count does not cover joints");
            for (size_t j = 0; j < skin.get("joints").size(); ++j) (void)index(skin.get("joints").at(j), nodes.size());
        }
        readMaterials(d, root);
        for (size_t i = 0; i < meshes.size(); ++i) {
            std::vector<int> slots;
            auto             ps = meshes.at(i).get("primitives");
            for (size_t k = 0; k < ps.size(); ++k)
                slots.push_back(ps.at(k).has("material") ? index(ps.at(k).get("material"), d.materials.size()) : -1);
            d.meshMaterials.push_back(std::move(slots));
        }
        readExpressions(d, vrm, root);
        auto look = vrm.get("lookAt");
        if (look) {
            d.look.present    = true;
            d.look.expression = choice(look.get("type"), "bone", {"bone", "expression"}) == "expression";
            d.look.offset     = vector<3>(look.get("offsetFromHeadBone"), {});
            auto range        = [&](const char* name, VrmLookAt::Range& r) {
                auto x   = look.get(name);
                r.input  = number(x.get("inputMaxValue"), 90);
                r.output = number(x.get("outputScale"), 10);
                require(r.input >= 0 && r.output >= 0, "Negative gaze range");
            };
            range("rangeMapHorizontalInner", d.look.inner);
            range("rangeMapHorizontalOuter", d.look.outer);
            range("rangeMapVerticalDown", d.look.down);
            range("rangeMapVerticalUp", d.look.up);
        }
        readSpring(d, root);
        return eve::Result<VrmDocument>::success(std::move(d));
    } catch (const Invalid& e) {
        return eve::Result<VrmDocument>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, e.what(), "vrm", {}, "avatar.vrm"));
    }
}
}  // namespace eve::avatar
