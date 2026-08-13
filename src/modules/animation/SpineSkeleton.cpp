#include "animation/SpineSkeleton.h"

#include "common/Exception.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eve::animation {

namespace {
float degToRad(float deg) { return deg * static_cast<float>(M_PI / 180.0); }
}  // namespace

SpineSkeleton::SpineSkeleton(SpineSkeletonData *data) : data_(data) {
    if (!data_) throw Exception("SpineSkeleton: data is null");
    bones_.resize(static_cast<size_t>(data_->getBoneCount()));
    slotAttachments_.resize(static_cast<size_t>(data_->getSlotCount()));
    skinIndex_ = data_->getDefaultSkin();
    if (skinIndex_ < 0) skinIndex_ = 0;
    setToSetupPose();
    updateWorldTransform();
}

void SpineSkeleton::checkBone(int index) const {
    if (index < 0 || index >= getBoneCount())
        throw Exception("SpineSkeleton: bone index %d out of range", index);
}
void SpineSkeleton::checkSlot(int index) const {
    if (index < 0 || index >= getSlotCount())
        throw Exception("SpineSkeleton: slot index %d out of range", index);
}

int SpineSkeleton::getBoneCount() const { return static_cast<int>(bones_.size()); }
int SpineSkeleton::getSlotCount() const { return static_cast<int>(slotAttachments_.size()); }

bool SpineSkeleton::setSkin(const std::string &name) {
    int idx = data_->findSkin(name);
    if (idx < 0) return false;
    skinIndex_ = idx;
    return true;
}

void SpineSkeleton::setToSetupPose() {
    for (int i = 0; i < data_->getBoneCount(); ++i) {
        const auto &b = data_->bone(i);
        BonePose &p   = bones_[static_cast<size_t>(i)];
        p.x           = b.x;
        p.y           = b.y;
        p.rotation    = b.rotation;
        p.scaleX      = b.scaleX;
        p.scaleY      = b.scaleY;
    }
    for (int i = 0; i < data_->getSlotCount(); ++i)
        slotAttachments_[static_cast<size_t>(i)] = data_->slot(i).attachment;
}

void SpineSkeleton::updateWorldTransform() {
    for (int i = 0; i < getBoneCount(); ++i) {
        BonePose &p       = bones_[static_cast<size_t>(i)];
        const int parent  = data_->bone(i).parent;
        const float rad   = degToRad(p.rotation);
        const float cos   = std::cos(rad);
        const float sin   = std::sin(rad);
        const float la    = cos * p.scaleX;
        const float lb    = -sin * p.scaleY;
        const float lc    = sin * p.scaleX;
        const float ld    = cos * p.scaleY;

        if (parent < 0) {
            p.a       = la;
            p.b       = lb;
            p.c       = lc;
            p.d       = ld;
            p.worldX  = p.x;
            p.worldY  = p.y;
            p.worldRot = p.rotation;
            p.worldSX = p.scaleX;
            p.worldSY = p.scaleY;
        } else {
            const BonePose &pp = bones_[static_cast<size_t>(parent)];
            p.worldX           = pp.a * p.x + pp.b * p.y + pp.worldX;
            p.worldY           = pp.c * p.x + pp.d * p.y + pp.worldY;
            p.a                = pp.a * la + pp.b * lc;
            p.b                = pp.a * lb + pp.b * ld;
            p.c                = pp.c * la + pp.d * lc;
            p.d                = pp.c * lb + pp.d * ld;
            p.worldRot         = pp.worldRot + p.rotation;
            p.worldSX          = pp.worldSX * p.scaleX;
            p.worldSY          = pp.worldSY * p.scaleY;
        }
    }
}

float SpineSkeleton::getBoneLocalX(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].x;
}
float SpineSkeleton::getBoneLocalY(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].y;
}
float SpineSkeleton::getBoneLocalRotation(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].rotation;
}
float SpineSkeleton::getBoneLocalScaleX(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].scaleX;
}
float SpineSkeleton::getBoneLocalScaleY(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].scaleY;
}

void SpineSkeleton::setBoneLocalX(int index, float x) {
    checkBone(index);
    bones_[static_cast<size_t>(index)].x = x;
}
void SpineSkeleton::setBoneLocalY(int index, float y) {
    checkBone(index);
    bones_[static_cast<size_t>(index)].y = y;
}
void SpineSkeleton::setBoneLocalRotation(int index, float degrees) {
    checkBone(index);
    bones_[static_cast<size_t>(index)].rotation = degrees;
}
void SpineSkeleton::setBoneLocalScaleX(int index, float sx) {
    checkBone(index);
    bones_[static_cast<size_t>(index)].scaleX = sx;
}
void SpineSkeleton::setBoneLocalScaleY(int index, float sy) {
    checkBone(index);
    bones_[static_cast<size_t>(index)].scaleY = sy;
}

float SpineSkeleton::getBoneWorldX(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].worldX;
}
float SpineSkeleton::getBoneWorldY(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].worldY;
}
float SpineSkeleton::getBoneWorldRotation(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].worldRot;
}
float SpineSkeleton::getBoneWorldScaleX(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].worldSX;
}
float SpineSkeleton::getBoneWorldScaleY(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].worldSY;
}

void SpineSkeleton::getBoneWorldMatrix(int index, float &a, float &b, float &c, float &d) const {
    checkBone(index);
    const BonePose &p = bones_[static_cast<size_t>(index)];
    a = p.a;
    b = p.b;
    c = p.c;
    d = p.d;
}

std::string SpineSkeleton::getSlotAttachmentName(int slotIndex) const {
    checkSlot(slotIndex);
    return slotAttachments_[static_cast<size_t>(slotIndex)];
}

void SpineSkeleton::setSlotAttachmentName(int slotIndex, const std::string &name) {
    checkSlot(slotIndex);
    slotAttachments_[static_cast<size_t>(slotIndex)] = name;
}

const SpineSkeletonData::RegionAttachment *SpineSkeleton::getSlotRegion(int slotIndex) const {
    checkSlot(slotIndex);
    const std::string &name = slotAttachments_[static_cast<size_t>(slotIndex)];
    if (name.empty()) return nullptr;
    const auto *att = data_->findAttachment(skinIndex_, slotIndex, name);
    if (att) return att;
    // Fallback to default skin
    int def = data_->getDefaultSkin();
    if (def >= 0 && def != skinIndex_) return data_->findAttachment(def, slotIndex, name);
    return nullptr;
}

}  // namespace eve::animation
