#include "graphics/Material.h"

#include "graphics/ClusteredLight.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"

namespace eve::graphics {

void Material::setShadingModel(const std::string &model) {
    if (model == "unlit" || model == "hair" || model == "custom" || model == "pbr") {
        shadingModel_ = model;
    } else {
        shadingModel_ = "pbr";
    }
    if (shadingModel_ == "unlit") receiveLight_ = false;
    if (shadingModel_ == "hair") isHair_ = true;
}

void Material::setTint(float r, float g, float b, float a) {
    r_ = r;
    g_ = g;
    b_ = b;
    a_ = a;
}

void Material::setMetallic(float metallic) {
    metallic_ = metallic < 0.f ? 0.f : (metallic > 1.f ? 1.f : metallic);
}

void Material::setRoughness(float roughness) {
    roughness_ = roughness < 0.04f ? 0.04f : (roughness > 1.f ? 1.f : roughness);
}

void Material::setTexCellBomb(float cellScale, float strength, float rotAmount) {
    texBombScale_ = cellScale > 1e-3f ? cellScale : 1e-3f;
    texBombStrength_ = strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength);
    texBombRot_ = rotAmount < 0.f ? 0.f : (rotAmount > 1.f ? 1.f : rotAmount);
}

void Material::setParallax(float scale, float minLayers, float maxLayers) {
    parallaxScale_ = scale < 0.f ? 0.f : (scale > 0.25f ? 0.25f : scale);
    float minL = minLayers < 1.f ? 1.f : minLayers;
    float maxL = maxLayers < minL ? minL : maxLayers;
    if (maxL > 64.f) maxL = 64.f;
    parallaxMinLayers_ = minL;
    parallaxMaxLayers_ = maxL;
}

void Material::setHair(bool hair) {
    isHair_ = hair;
    if (hair) shadingModel_ = "hair";
}

bool Material::hasParam(const std::string &name) const { return params_.count(name) > 0; }

void Material::setFloat(const std::string &name, float value) { params_[name] = value; }

float Material::getFloat(const std::string &name) const {
    auto it = params_.find(name);
    return it == params_.end() ? 0.f : it->second;
}

Shader *Material::effectiveShader() const {
    if (shadingModel_ == "custom") return shader_;
    return shader_;
}

bool Material::isTransparentHair() const {
    return isHair_ || shadingModel_ == "hair";
}

void Material::bind(Graphics &gfx) const {
    gfx.setMesh3DMaterial(metallic_, roughness_);
    gfx.setMesh3DTexCellBomb(texBombScale_, texBombStrength_, texBombRot_);
    gfx.setMesh3DNormalTexture(normal_);
    gfx.setMesh3DHeightTexture(height_);
    gfx.setMesh3DParallax(parallaxScale_, parallaxMinLayers_, parallaxMaxLayers_);
    gfx.setMesh3DShadowReceive(receiveShadow_);

    if (!receiveLight_ || shadingModel_ == "unlit") {
        ClusteredLightingUpload off{};
        off.active = false;
        gfx.setMesh3DClusteredLighting(off);
        Lighting3DPack none{};
        none.count = 0;
        none.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
        gfx.setMesh3DLighting(none);
    }

    if (shader_ && !params_.empty()) {
        for (const auto &kv : params_) {
            if (shader_->hasUniform(kv.first)) shader_->sendFloat(kv.first, kv.second);
        }
    }
}

}  // namespace eve::graphics
