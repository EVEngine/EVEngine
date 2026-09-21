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
    auto payload = reader.read(ref, "eve.material/15", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/14", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/13", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/12", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/11", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/10", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/9", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/8", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/7", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/6", caps, 1024 * 1024);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(ref, "eve.material/5", caps, 1024 * 1024);
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
        if (string(o, "schema", "") != "eve.material" ||
            (version != 5 && version != 6 && version != 7 && version != 8 && version != 9 && version != 10 &&
             version != 11 && version != 12 && version != 13 && version != 14 && version != 15))
            throw Invalid{};
        if (version != payload.value().schemaVersion.value()) throw Invalid{};
        if (version >= 15 && !o.contains("alphaToCoverage")) throw Invalid{};
        if (version == 5 && o.contains("vegetationSurface")) throw Invalid{};
        vector(o, "motionHighlightColor", s.motionHighlightColor);
        if (auto* encoded = field(o, "vegetationSurface")) {
            const auto* vegetation               = encoded->getIf<Value::Object>();
            const auto  expectedVegetationFields = version == 6 ? 7u : (version >= 14 ? 16u : 14u);
            if (!vegetation || vegetation->size() != expectedVegetationFields) throw Invalid{};
            for (const auto& [key, value] : *vegetation)
                if (key != "overlay" && key != "wetness" && key != "overlayVariation" && key != "overlayProjection" &&
                    key != "vertexOcclusionAlpha" && key != "invertVertexOcclusion" && key != "sourceFamily" &&
                    key != "colors" && key != "colorsIntensity" && key != "colorsMask" && key != "colorsVariation" &&
                    key != "vertexOcclusionMinimum" && key != "vertexOcclusionMaximum" &&
                    key != "invertVertexOcclusionColors" && key != "vertexOcclusionColor" &&
                    key != "backfaceNormalMode")
                    throw Invalid{};
            if (string(*vegetation, "sourceFamily", "") != "tve-12") throw Invalid{};
            graphics::VegetationSurface result;
            result.overlayCoverage      = number(*vegetation, "overlay", 1);
            result.wetnessCoverage      = number(*vegetation, "wetness", 1);
            result.overlayVariation     = number(*vegetation, "overlayVariation", .5f);
            result.overlayProjection    = number(*vegetation, "overlayProjection", .5f);
            result.vertexOcclusionAlpha = number(*vegetation, "vertexOcclusionAlpha", .5019608f);
            if (version >= 14) vector(*vegetation, "vertexOcclusionColor", result.vertexOcclusionColor);
            if (version >= 14)
                result.backfaceNormalMode =
                    graphics::PbrVegetationBackfaceNormalMode(integer(*vegetation, "backfaceNormalMode", 0));
            result.invertVertexOcclusion       = boolean(*vegetation, "invertVertexOcclusion", false);
            result.colorsCoverage              = number(*vegetation, "colors", 1);
            result.colorsIntensity             = number(*vegetation, "colorsIntensity", 1);
            result.colorsMask                  = number(*vegetation, "colorsMask", 1);
            result.colorsVariation             = number(*vegetation, "colorsVariation", .5f);
            result.vertexOcclusionMinimum      = number(*vegetation, "vertexOcclusionMinimum", 0);
            result.vertexOcclusionMaximum      = number(*vegetation, "vertexOcclusionMaximum", 1);
            result.invertVertexOcclusionColors = boolean(*vegetation, "invertVertexOcclusionColors", false);
            if (result.overlayCoverage < 0 || result.overlayCoverage > 1 || result.wetnessCoverage < 0 ||
                result.wetnessCoverage > 1 || result.overlayVariation < 0 || result.overlayVariation > 1 ||
                result.overlayProjection < 0 || result.overlayProjection > 1 || result.vertexOcclusionAlpha < 0 ||
                result.vertexOcclusionAlpha > 1 || result.colorsCoverage < 0 || result.colorsCoverage > 1 ||
                result.colorsIntensity < 0 || result.colorsIntensity > 2 || result.colorsMask < 0 ||
                result.colorsMask > 1 || result.colorsVariation < 0 || result.colorsVariation > 1 ||
                result.vertexOcclusionMinimum < 0 || result.vertexOcclusionMinimum > 1 ||
                result.vertexOcclusionMaximum < 0 || result.vertexOcclusionMaximum > 1 ||
                result.vertexOcclusionMaximum - result.vertexOcclusionMinimum + .0001f == 0)
                throw Invalid{};
            for (const auto value : result.vertexOcclusionColor)
                if (!std::isfinite(value) || value < 0) throw Invalid{};
            if (uint32_t(result.backfaceNormalMode) > uint32_t(graphics::PbrVegetationBackfaceNormalMode::Same))
                throw Invalid{};
            m.vegetationSurface = result;
        }
        if (auto* encoded = field(o, "vegetationDetail")) {
            if (version < 8) throw Invalid{};
            const auto* detail = encoded->getIf<Value::Object>();
            if (!detail || detail->size() != 24) throw Invalid{};
            auto& d          = s.vegetationDetail;
            d.value          = number(*detail, "value", 0);
            d.uvMode         = integer(*detail, "uvMode", 0);
            d.inverseUvScale = boolean(*detail, "inverseUvScale", false);
            vector(*detail, "uvScale", d.uvScale);
            vector(*detail, "uvOffset", d.uvOffset);
            vector(*detail, "color", d.color);
            vector(*detail, "colorTwo", d.colorTwo);
            d.colorMode        = integer(*detail, "colorMode", 0);
            d.albedoValue      = number(*detail, "albedoValue", 1);
            d.normalValue      = number(*detail, "normalValue", 1);
            d.normalBlendValue = number(*detail, "normalBlendValue", 1);
            d.metallicValue    = number(*detail, "metallicValue", 0);
            d.occlusionValue   = number(*detail, "occlusionValue", 1);
            d.smoothnessValue  = number(*detail, "smoothnessValue", 1);
            d.blendMode        = integer(*detail, "blendMode", 0);
            d.alphaMode        = integer(*detail, "alphaMode", 1);
            d.maskMode         = integer(*detail, "maskMode", 0);
            d.meshMode         = integer(*detail, "meshMode", 0);
            d.blendMinimum     = number(*detail, "blendMinimum", 0);
            d.blendMaximum     = number(*detail, "blendMaximum", 1);
            d.maskMinimum      = number(*detail, "maskMinimum", 0);
            d.maskMaximum      = number(*detail, "maskMaximum", 1);
            d.meshMinimum      = number(*detail, "meshMinimum", 0);
            d.meshMaximum      = number(*detail, "meshMaximum", 1);
        }
        if (auto* encoded = field(o, "vegetationAlpha")) {
            if (version < 9 || version > 15) throw Invalid{};
            const auto* alpha = encoded->getIf<Value::Object>();
            if (!alpha || alpha->size() != (version < 12 ? 3u : 6u)) throw Invalid{};
            auto& a      = s.vegetationAlpha;
            a.enabled    = true;
            a.global     = number(*alpha, "global", 1);
            a.variation  = number(*alpha, "variation", .5f);
            a.detailFade = boolean(*alpha, "detailFade", false);
            if (version >= 12) {
                a.glancing = number(*alpha, "glancing", 0);
                a.camera   = number(*alpha, "camera", 1);
                a.constant = number(*alpha, "constant", 0);
            }
        }
        if (auto* encoded = field(o, "vegetationFields")) {
            if (version < 9 || version > 15) throw Invalid{};
            const auto* fields = encoded->getIf<Value::Object>();
            if (!fields || fields->size() != (version == 9 ? 4u : 9u)) throw Invalid{};
            s.vegetationColors.layer            = integer(*fields, "colorsLayer", 0);
            s.vegetationColors.usePivotPosition = boolean(*fields, "colorsUsePivotPosition", false);
            s.vegetationExtras.layer            = integer(*fields, "extrasLayer", 0);
            s.vegetationExtras.usePivotPosition = boolean(*fields, "extrasUsePivotPosition", false);
            if (version >= 10) {
                s.vegetationMotion.layer         = integer(*fields, "motionLayer", 0);
                s.vegetationVertex.layer         = integer(*fields, "vertexLayer", 0);
                s.vegetationVertex.globalSize    = number(*fields, "globalSize", 1);
                s.vegetationVertex.sizeFadeStart = number(*fields, "sizeFadeStart", 0);
                s.vegetationVertex.sizeFadeEnd   = number(*fields, "sizeFadeEnd", 100);
            }
        }
        if (auto* encoded = field(o, "vegetationMotion")) {
            if (version < 11 || version > 15) throw Invalid{};
            const auto* motion = encoded->getIf<Value::Object>();
            if (!motion || motion->size() != 21) throw Invalid{};
            for (const auto& [key, value] : *motion)
                if (key != "dynamicMode" && key != "rigidity" && key != "facing" && key != "bending" &&
                    key != "bendingSpeed" && key != "bendingScale" && key != "bendingVariation" && key != "branch" &&
                    key != "rolling" && key != "branchSpeed" && key != "branchScale" && key != "branchVariation" &&
                    key != "flutter" && key != "flutterSpeed" && key != "flutterScale" && key != "flutterVariation" &&
                    key != "interaction" && key != "interactionMask" && key != "perspectivePush" &&
                    key != "perspectiveNoise" && key != "perspectiveAngle")
                    throw Invalid{};
            auto& m            = s.vegetationMotion;
            m.dynamicMode      = number(*motion, "dynamicMode", 0);
            m.rigidity         = number(*motion, "rigidity", .5f);
            m.facing           = number(*motion, "facing", .5f);
            m.bending          = number(*motion, "bending", .2f);
            m.bendingSpeed     = number(*motion, "bendingSpeed", 2);
            m.bendingScale     = number(*motion, "bendingScale", 1);
            m.bendingVariation = number(*motion, "bendingVariation", 0);
            m.branch           = number(*motion, "branch", .2f);
            m.rolling          = number(*motion, "rolling", .2f);
            m.branchSpeed      = number(*motion, "branchSpeed", 6);
            m.branchScale      = number(*motion, "branchScale", 3);
            m.branchVariation  = number(*motion, "branchVariation", 0);
            m.flutter          = number(*motion, "flutter", .2f);
            m.flutterSpeed     = number(*motion, "flutterSpeed", 20);
            m.flutterScale     = number(*motion, "flutterScale", 10);
            m.flutterVariation = number(*motion, "flutterVariation", 0);
            m.interaction      = number(*motion, "interaction", 1);
            m.interactionMask  = number(*motion, "interactionMask", 1);
            m.perspectivePush  = number(*motion, "perspectivePush", 0);
            m.perspectiveNoise = number(*motion, "perspectiveNoise", 0);
            m.perspectiveAngle = number(*motion, "perspectiveAngle", 1);
            if (m.dynamicMode < 0 || m.dynamicMode > 1 || m.rigidity < 0 || m.rigidity > 1 || m.facing < 0 ||
                m.facing > 1 || m.interactionMask < 0 || m.interactionMask > 1)
                throw Invalid{};
            for (const float value :
                 {m.bending, m.bendingSpeed, m.bendingScale, m.bendingVariation, m.branch, m.rolling, m.branchSpeed,
                  m.branchScale, m.branchVariation, m.flutter, m.flutterSpeed, m.flutterScale, m.flutterVariation,
                  m.interaction, m.perspectivePush, m.perspectiveNoise, m.perspectiveAngle})
                if (value < 0 || value > 1000000) throw Invalid{};
        }
        if (auto* encoded = field(o, "vegetationEmission")) {
            if (version < 13) throw Invalid{};
            const auto* emission = encoded->getIf<Value::Object>();
            if (!emission || emission->size() != 4) throw Invalid{};
            for (const auto& [key, value] : *emission)
                if (key != "minimum" && key != "maximum" && key != "phase" && key != "global") throw Invalid{};
            auto& e   = s.vegetationEmission;
            e.minimum = number(*emission, "minimum", 0);
            e.maximum = number(*emission, "maximum", 1);
            e.phase   = number(*emission, "phase", 1);
            e.global  = number(*emission, "global", 1);
            e.enabled = true;
        }
        if (auto* encoded = field(o, "vegetationGradient")) {
            if (version < 14) throw Invalid{};
            const auto* gradient = encoded->getIf<Value::Object>();
            if (!gradient || gradient->size() != 4) throw Invalid{};
            for (const auto& [key, value] : *gradient)
                if (key != "colorOne" && key != "colorTwo" && key != "minimum" && key != "maximum") throw Invalid{};
            auto& g = s.vegetationGradient;
            vector(*gradient, "colorOne", g.colorOne);
            vector(*gradient, "colorTwo", g.colorTwo);
            g.minimum = number(*gradient, "minimum", 0);
            g.maximum = number(*gradient, "maximum", 1);
            g.enabled = true;
        }
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
        const auto cullMode = string(o, "cullMode", m.doubleSided ? "none" : "back");
        if (cullMode != "none" && cullMode != "back" && cullMode != "front") throw Invalid{};
        if ((cullMode == "none") != m.doubleSided) throw Invalid{};
        s.cullMode        = cullMode == "none"    ? graphics::PbrCullMode::None
                            : cullMode == "front" ? graphics::PbrCullMode::Front
                                                  : graphics::PbrCullMode::Back;
        s.alphaToCoverage = version >= 15 && boolean(o, "alphaToCoverage", false);
        m.alphaCutoff = number(o, "alphaCutoff", .5f);
        std::array<float, 4> color{1, 1, 1, 1};
        vector(o, "baseColor", color);
        for (std::size_t i = 0; i < color.size(); ++i)
            if (color[i] < 0 || (i == 3 && color[i] > 1)) throw Invalid{};
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
        s.albedoTextureStrength       = number(o, "albedoTextureStrength", 1);
        if (auto* encoded = field(o, "translucency")) {
            const auto* trans = encoded->getIf<Value::Object>();
            if (!trans) throw Invalid{};
            for (const auto& [key, value] : *trans)
                if (key != "color" && key != "intensity" && key != "strength" && key != "normalDistortion" &&
                    key != "scattering" && key != "direct" && key != "ambient" && key != "shadow" &&
                    key != "maskAmount" && key != "maskMinimum" && key != "maskMaximum")
                    throw Invalid{};
            auto& t = s.translucency;
            vector(*trans, "color", t.color);
            t.intensity        = number(*trans, "intensity", 0);
            t.strength         = number(*trans, "strength", 1);
            t.normalDistortion = number(*trans, "normalDistortion", .5f);
            t.scattering       = number(*trans, "scattering", 2);
            t.direct           = number(*trans, "direct", .9f);
            t.ambient          = number(*trans, "ambient", .1f);
            t.shadow           = number(*trans, "shadow", .5f);
            t.maskAmount       = number(*trans, "maskAmount", 0);
            t.maskMinimum      = number(*trans, "maskMinimum", 0);
            t.maskMaximum      = number(*trans, "maskMaximum", 0);
        }
        auto normalMode = graphics::PbrNormalMode::TangentXYZ;
        if (auto* encoded = field(o, "normalEncoding")) {
            if (!encoded->isString()) throw Invalid{};
            const auto& name = encoded->asString();
            if (name == "tangent-xyz")
                normalMode = graphics::PbrNormalMode::TangentXYZ;
            else if (name == "tve-rg")
                normalMode = graphics::PbrNormalMode::VegetationRG;
            else if (name == "tve-rag")
                normalMode = graphics::PbrNormalMode::VegetationRAG;
            else if (name == "tve-ag")
                normalMode = graphics::PbrNormalMode::VegetationAG;
            else
                throw Invalid{};
        }
        const bool colorMaskEnabled = o.contains("colorMask");
        if (colorMaskEnabled) {
            const auto* mask = o.at("colorMask").getIf<Value::Object>();
            if (!mask || mask->size() != 3 || !mask->contains("secondary") || !mask->contains("minimum") ||
                !mask->contains("maximum"))
                throw Invalid{};
            vector(*mask, "secondary", s.colorMaskSecondary);
            s.colorMaskMin = number(*mask, "minimum", 0);
            s.colorMaskMax = number(*mask, "maximum", 0);
        }
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
        constexpr const char* detailRoles[] = {"detailAlbedoTexture", "detailNormalTexture", "detailMaskTexture"};
        for (size_t i = 0; i < 3; ++i) {
            const std::string role  = detailRoles[i];
            auto*             image = field(o, role);
            if (!image) continue;
            if (version < 8 || !image->isString()) throw Invalid{};
            auto ref = AssetRef::parse(image->asString());
            if (!ref) return Result<CookedMaterial>::failure(ref.status());
            m.detailImages[i] = std::move(ref).takeValue();
            auto& b           = s.vegetationDetail.textures[i];
            b.srgbDecode      = i == 0;
            if (auto* sampler = field(o, role + "Sampler")) {
                auto* t = sampler->getIf<Value::Object>();
                if (!t) throw Invalid{};
                b.wrapS     = integer(*t, "wrapS", 10497);
                b.wrapT     = integer(*t, "wrapT", 10497);
                b.minFilter = integer(*t, "minFilter", 9987);
                b.magFilter = integer(*t, "magFilter", 9729);
            }
        }
        const auto transMask = s.translucency.maskAmount;
        if (!std::isfinite(transMask) || transMask < 0 || transMask > 1) throw Invalid{};
        s.translucency.maskAmount = 0;
        const auto detailValue    = s.vegetationDetail.value;
        s.vegetationDetail.value  = 0;
        auto validated = graphics::validatePbrSurface(s);
        if (!validated) return Result<CookedMaterial>::failure(validated.status());
        // This owning CPU definition has no GPU resources yet. Validate logical identity
        // here; setMesh3DPbrSurface validates the resolved texture after upload.
        if (colorMaskEnabled && !m.images[1]) throw Invalid{};
        if (s.translucency.intensity > 0 && transMask > 0 && !m.images[1]) throw Invalid{};
        s.translucency.maskAmount = transMask;
        if (detailValue > 0 && (!m.detailImages[0] || !m.detailImages[1])) throw Invalid{};
        s.vegetationDetail.value = detailValue;
        if (normalMode != graphics::PbrNormalMode::TangentXYZ &&
            (!m.images[2] || s.normalScale < -8.f || s.normalScale > 8.f))
            throw Invalid{};
        if (s.vegetationEmission.enabled && !m.images[4]) throw Invalid{};
        s.normalMode       = normalMode;
        s.colorMaskEnabled = colorMaskEnabled;
        if (m.vegetationSurface) {
            auto& vegetation       = *m.vegetationSurface;
            vegetation.albedo      = {m.color.r, m.color.g, m.color.b, m.color.a};
            vegetation.emission    = s.emissive;
            vegetation.roughness   = m.roughness;
            vegetation.alphaCutoff = m.alphaCutoff;
        }
        return Result<CookedMaterial>::success(std::move(m));
    } catch (const Invalid&) {
        return Result<CookedMaterial>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid cooked PBR material", {}, {}, "asset.graphics.material"));
    }
}
}  // namespace eve::asset_graphics::detail
