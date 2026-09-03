#include "stylize/SkillMeshEffect.h"

#include "common/Exception.h"

namespace eve::stylize {
namespace {
const char* styleFor(SkillMeshEffectKind kind) {
    switch (kind) {
        case SkillMeshEffectKind::WeaponSlash: return "slash";
        case SkillMeshEffectKind::ImpactFlash: return "rim";
        case SkillMeshEffectKind::ChargeAura: return "aura";
        case SkillMeshEffectKind::BurningBody: return "ember";
    }
    throw eve::Exception("SkillMeshEffect: unknown recipe kind");
}

MeshEffectPlayback playbackFor(SkillMeshEffectKind kind) {
    switch (kind) {
        case SkillMeshEffectKind::WeaponSlash: return {0.02f, 0.12f, 0.18f, false};
        case SkillMeshEffectKind::ImpactFlash: return {0.01f, 0.05f, 0.14f, false};
        case SkillMeshEffectKind::ChargeAura: return {0.18f, 0.55f, 0.18f, true};
        case SkillMeshEffectKind::BurningBody: return {0.12f, 0.7f, 0.2f, true};
    }
    throw eve::Exception("SkillMeshEffect: unknown recipe kind");
}
}  // namespace

SkillMeshEffect::SkillMeshEffect(SkillMeshEffectKind kind)
    : kind_(kind), effect_(std::make_unique<MeshEffectInstance>(styleFor(kind))) {
    effect_->setPlayback(playbackFor(kind));
    if (kind == SkillMeshEffectKind::WeaponSlash)
        trail_ = std::make_unique<TrailEmitter>(TrailSettings{64, 0.3f, 0.012f, 2.f});
}

TrailEmitter& SkillMeshEffect::trail() {
    if (!trail_) throw eve::Exception("SkillMeshEffect.trail: recipe has no ribbon");
    return *trail_;
}

const TrailEmitter& SkillMeshEffect::trail() const {
    if (!trail_) throw eve::Exception("SkillMeshEffect.trail: recipe has no ribbon");
    return *trail_;
}

void SkillMeshEffect::bindTarget(MeshEffectTargetHandle target) { effect_->bindTarget(target); }

void SkillMeshEffect::play() noexcept {
    if (trail_) trail_->clear();
    effect_->play();
}

void SkillMeshEffect::stop(float fadeOutSeconds) {
    effect_->stop(fadeOutSeconds);
    if (trail_) trail_->breakTrail();
}

void SkillMeshEffect::update(float dtSeconds) {
    effect_->update(dtSeconds);
    if (trail_) trail_->update(dtSeconds);
}

TrailAppendResult SkillMeshEffect::appendBlade(glm::vec3 root, glm::vec3 tip) {
    if (!trail_) throw eve::Exception("SkillMeshEffect.appendBlade: recipe has no ribbon");
    return trail_->append(root, tip);
}

}  // namespace eve::stylize
