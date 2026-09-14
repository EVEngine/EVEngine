#include "asset/graphics/CookedMaterial.h"
#include <cmath>
#include <limits>
#include "asset/RuntimeDefinition.h"
namespace eve::asset_graphics::detail {
namespace {
struct Invalid {};
const Value* field(const Value::Object& object, const std::string& key) {
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}
float number(const Value::Object& object, const std::string& key, float fallback) {
    auto* v = field(object, key);
    if (!v) return fallback;
    if (!v->isNumeric()) throw Invalid{};
    double n = v->isInt64() ? double(v->asInt()) : v->asDouble();
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) throw Invalid{};
    return float(n);
}
uint32_t integer(const Value::Object& object, const std::string& key, uint32_t fallback) {
    auto* v = field(object, key);
    if (!v) return fallback;
    if (!v->isInt64() || v->asInt() < 0 || uint64_t(v->asInt()) > UINT32_MAX) throw Invalid{};
    return uint32_t(v->asInt());
}
bool boolean(const Value::Object& o, const char* key, bool fallback) {
    auto* v = field(o, key);
    if (!v) return fallback;
    if (!v->isBool()) throw Invalid{};
    return v->asBool();
}
std::string string(const Value::Object& o, const char* key, std::string fallback) {
    auto* v = field(o, key);
    if (!v) return fallback;
    if (!v->isString()) throw Invalid{};
    return v->asString();
}
template <size_t N>
void vector(const Value::Object& o, const char* key, std::array<float, N>& out) {
    auto* v = field(o, key);
    if (!v) return;
    auto* a = v->getIf<Value::Array>();
    if (!a || a->size() != N) throw Invalid{};
    for (size_t i = 0; i < N; i++) {
        Value::Object scalar{{"v", (*a)[i]}};
        out[i] = number(scalar, "v", 0);
    }
}
}  // namespace
Result<CookedMaterial> readCookedMaterial(const asset::EvpackResourceReader& reader, const AssetRef& ref,
                                          const asset::EvpackCapabilities& caps) {
    auto payload = reader.read(ref, "eve.material/2", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/1", caps, 1024 * 1024);
    if (!payload) return Result<CookedMaterial>::failure(payload.status());
    try {
        if (payload.value().chunks.size() != 1) throw Invalid{};
        auto decoded = asset::decodeRuntimeDefinition(payload.value().chunks.front().bytes);
        if (!decoded) return Result<CookedMaterial>::failure(decoded.status());
        auto* object = decoded.value().getIf<Value::Object>();
        if (!object) throw Invalid{};
        auto&          o = *object;
        CookedMaterial m;
        auto&          s       = m.surface;
        auto           version = integer(o, "schemaVersion", 0);
        if (string(o, "schema", "") != "eve.material" || (version != 1 && version != 2)) throw Invalid{};
        const auto shading = string(o, "shadingModel", "");
        if (shading != "pbr" && shading != "unlit") throw Invalid{};
        s.unlit   = shading == "unlit";
        auto mode = string(o, "surfaceMode", "opaque");
        if (mode != "opaque" && mode != "masked" && mode != "transparent") throw Invalid{};
        m.transparent = mode == "transparent";
        m.masked      = mode == "masked";
        auto blend    = string(o, "blendMode", "alpha");
        if (blend != "alpha" && blend != "premultiplied") throw Invalid{};
        m.blend       = blend == "alpha" ? graphics::BlendMode::Alpha : graphics::BlendMode::Premultiplied;
        m.doubleSided = boolean(o, "doubleSided", false);
        m.alphaCutoff = number(o, "alphaCutoff", .5f);
        std::array<float, 4> color{1, 1, 1, 1};
        vector(o, "baseColor", color);
        for (float c : color)
            if (c < 0 || c > 1) throw Invalid{};
        m.color     = {color[0], color[1], color[2], color[3]};
        m.metallic  = number(o, "metallic", 0);
        m.roughness = number(o, "roughness", 1);
        if (m.metallic < 0 || m.metallic > 1 || m.roughness < 0 || m.roughness > 1 || m.alphaCutoff < 0 ||
            m.alphaCutoff > 1)
            throw Invalid{};
        vector(o, "emissive", s.emissive);
        vector(o, "specularColorFactor", s.specularColor);
        s.normalScale                 = number(o, "normalScale", 1);
        s.occlusionStrength           = number(o, "occlusionStrength", 1);
        s.emissiveStrength            = number(o, "emissiveStrength", 1);
        s.specularFactor              = number(o, "specularFactor", 1);
        s.ior                         = number(o, "ior", 1.5f);
        s.anisotropyStrength          = number(o, "anisotropyStrength", 0);
        s.anisotropyRotation          = number(o, "anisotropyRotation", 0);
        s.clearcoatFactor             = number(o, "clearcoatFactor", 0);
        s.clearcoatRoughness          = number(o, "clearcoatRoughnessFactor", 0);
        s.clearcoatNormalScale        = number(o, "clearcoatNormalScale", 1);
        constexpr const char* roles[] = {"baseColorTexture",          "metallicRoughnessTexture", "normalTexture",
                                         "occlusionTexture",          "emissiveTexture",          "specularTexture",
                                         "specularColorTexture",      "anisotropyTexture",        "clearcoatTexture",
                                         "clearcoatRoughnessTexture", "clearcoatNormalTexture"};
        for (size_t i = 0; i < 11; i++) {
            std::string role  = roles[i];
            auto*       image = field(o, role);
            if (!image) continue;
            if (!image->isString()) throw Invalid{};
            auto ref = AssetRef::parse(image->asString());
            if (!ref) return Result<CookedMaterial>::failure(ref.status());
            m.images[i]  = std::move(ref).takeValue();
            auto& b      = s.textures[i];
            b.srgbDecode = false;
            b.texcoord   = integer(o, role + "TexCoord", 0);
            if (auto* transform = field(o, role + "Transform")) {
                auto* t = transform->getIf<Value::Object>();
                if (!t) throw Invalid{};
                vector(*t, "offset", b.offset);
                vector(*t, "scale", b.scale);
                b.rotation = number(*t, "rotation", 0);
                b.texcoord = integer(*t, "texCoord", b.texcoord);
            }
            if (auto* sampler = field(o, role + "Sampler")) {
                auto* t = sampler->getIf<Value::Object>();
                if (!t) throw Invalid{};
                b.wrapS     = integer(*t, "wrapS", 10497);
                b.wrapT     = integer(*t, "wrapT", 10497);
                b.minFilter = integer(*t, "minFilter", 9987);
                b.magFilter = integer(*t, "magFilter", 9729);
            }
        }
        auto validated = graphics::validatePbrSurface(s);
        if (!validated) return Result<CookedMaterial>::failure(validated.status());
        return Result<CookedMaterial>::success(std::move(m));
    } catch (const Invalid&) {
        return Result<CookedMaterial>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid cooked PBR material", {}, {}, "asset.graphics.material"));
    }
}
}  // namespace eve::asset_graphics::detail
