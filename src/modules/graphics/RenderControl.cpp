#include "graphics/RenderControl.h"

namespace eve::graphics {
namespace {

const char *kKnownFeatures[] = {"depthTest", "shadow",     "gbuffer", "gbufferAlbedo",
                                "forward",   "hair",       "clustered", "ao", "gi", "aa", "msaa",
                                "outline",   "gpuDriven", "visResolve", "frustumCull", "decal",
                                "atmosphere", "volumetricFog", "fogLocalVolumes", "fogTemporal"};

bool isKnownFeature(const std::string &feature) {
    for (const char *f : kKnownFeatures) {
        if (feature == f) return true;
    }
    return false;
}

}  // namespace

RenderControl::RenderControl() {
    features_["depthTest"] = true;
    features_["shadow"] = true;
    features_["gbuffer"] = true;
    features_["gbufferAlbedo"] = true;
    features_["forward"] = true;
    features_["hair"] = true;
    features_["clustered"] = true;
    features_["ao"] = true;
    features_["gi"] = true;
    features_["aa"] = true;
    features_["msaa"] = true;
    features_["outline"] = false;
    features_["gpuDriven"] = false;  // stage 1 opt-in; off until runtime-verified
    features_["visResolve"] = false; // stage 3 opt-in; visibility-buffer resolve
    features_["frustumCull"] = false;  // opt-in: conservative sphere culling in frame prep
    features_["atmosphere"] = true;
    features_["volumetricFog"] = false;
    features_["fogLocalVolumes"] = false;
    features_["fogTemporal"] = false;
    dirty_ = true;
    compiled_ = false;
}

void RenderControl::attach(Graphics *gfx) {
    gfx_ = gfx;
    gbuffer_.attach(gfx);
}

bool RenderControl::supports(const std::string &feature) const { return isKnownFeature(feature); }

void RenderControl::setFeature(const std::string &feature, bool enabled) {
    if (!isKnownFeature(feature)) return;
    auto it = features_.find(feature);
    const bool cur = it == features_.end() ? false : it->second;
    if (cur == enabled) return;
    features_[feature] = enabled;
    if (feature == "gbufferAlbedo" && enabled) features_["gbuffer"] = true;
    if (feature == "ao" && enabled) features_["gbuffer"] = true;
    if (feature == "outline" && enabled) features_["gbuffer"] = true;
    if (feature == "decal" && enabled) features_["gbuffer"] = true;
    // Stage 2 GPU cull needs the GBuffer depth as its HZB source.
    if (feature == "gpuDriven" && enabled) features_["gbuffer"] = true;
    if ((feature == "volumetricFog" || feature == "fogLocalVolumes" ||
         feature == "fogTemporal") && enabled) {
        features_["atmosphere"] = true;
        features_["volumetricFog"] = true;
        features_["gbuffer"] = true;
    }
    if (feature == "volumetricFog" && !enabled) {
        features_["fogLocalVolumes"] = false;
        features_["fogTemporal"] = false;
    }
    if (feature == "gi" && enabled) {
        features_["gbuffer"] = true;
        features_["gbufferAlbedo"] = true;
    }
    if (feature == "gbuffer" && !enabled) {
        features_["gbufferAlbedo"] = false;
        features_["ao"] = false;
        features_["gi"] = false;
        features_["outline"] = false;
        features_["decal"] = false;
    }
    dirty_ = true;
}

void RenderControl::enable(const std::string &feature) { setFeature(feature, true); }

void RenderControl::disable(const std::string &feature) { setFeature(feature, false); }

bool RenderControl::isEnabled(const std::string &feature) const {
    auto it = features_.find(feature);
    return it != features_.end() && it->second;
}

void RenderControl::compile() {
    passes_.clear();
    if (isEnabled("shadow")) passes_.push_back("shadow");
    if (isEnabled("gbuffer") || isEnabled("gbufferAlbedo") || isEnabled("ao") || isEnabled("gi"))
        passes_.push_back("gbuffer");
    if (isEnabled("decal")) passes_.push_back("decal");
    if (isEnabled("forward")) passes_.push_back("forward");
    if (isEnabled("atmosphere")) passes_.push_back("atmosphere");
    if (isEnabled("volumetricFog")) {
        passes_.push_back("fogMedia");
        passes_.push_back("fogLighting");
        if (isEnabled("fogTemporal")) passes_.push_back("fogTemporal");
        passes_.push_back("fogIntegrate");
        passes_.push_back("fogComposite");
    }
    if (isEnabled("hair")) passes_.push_back("hair");
    dirty_ = false;
    compiled_ = true;
}

void RenderControl::ensureCompiled() {
    if (!isCompiled()) compile();
}

std::string RenderControl::getPassName(int index) const {
    if (index < 0 || index >= int(passes_.size())) return {};
    return passes_[size_t(index)];
}

bool RenderControl::hasPass(const std::string &name) const {
    for (const auto &p : passes_) {
        if (p == name) return true;
    }
    return false;
}

}  // namespace eve::graphics
