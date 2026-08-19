#pragma once

#include "animation/SpineSkeletonData.h"

#include <string>
#include <vector>

namespace eve::animation {

/**
 * Runtime Spine skeleton pose (local + world bone transforms, slot attachments).
 * Script type: `SpineSkeleton`.
 */
class SpineSkeleton {
public:
    explicit SpineSkeleton(SpineSkeletonData *data);
    ~SpineSkeleton() = default;

    SpineSkeleton(const SpineSkeleton &)            = delete;
    SpineSkeleton &operator=(const SpineSkeleton &) = delete;

    SpineSkeletonData *getData() const { return data_; }

    bool setSkin(const std::string &name);
    int  getSkin() const { return skinIndex_; }

    void setToSetupPose();
    void updateWorldTransform();

    int   getBoneCount() const;
    float getBoneLocalX(int index) const;
    float getBoneLocalY(int index) const;
    float getBoneLocalRotation(int index) const;
    float getBoneLocalScaleX(int index) const;
    float getBoneLocalScaleY(int index) const;

    void setBoneLocalX(int index, float x);
    void setBoneLocalY(int index, float y);
    void setBoneLocalRotation(int index, float degrees);
    void setBoneLocalScaleX(int index, float sx);
    void setBoneLocalScaleY(int index, float sy);

    float getBoneWorldX(int index) const;
    float getBoneWorldY(int index) const;
    float getBoneWorldRotation(int index) const;
    float getBoneWorldScaleX(int index) const;
    float getBoneWorldScaleY(int index) const;
    /** World 2x2 matrix columns (a,c) / (b,d) used for local→world offset. */
    void getBoneWorldMatrix(int index, float &a, float &b, float &c, float &d) const;

    int         getSlotCount() const;
    std::string getSlotAttachmentName(int slotIndex) const;
    void        setSlotAttachmentName(int slotIndex, const std::string &name);

    const SpineSkeletonData::RegionAttachment *getSlotRegion(int slotIndex) const;

private:
    friend class SpineAnim;

    struct BonePose {
        float x = 0.f, y = 0.f;
        float rotation = 0.f;
        float scaleX = 1.f, scaleY = 1.f;
        float worldX = 0.f, worldY = 0.f;
        float worldRot = 0.f;
        float worldSX = 1.f, worldSY = 1.f;
        float a = 1.f, b = 0.f, c = 0.f, d = 1.f;  // 2x2 world matrix
    };

    void checkBone(int index) const;
    void checkSlot(int index) const;

    SpineSkeletonData       *data_      = nullptr;
    int                      skinIndex_ = 0;
    std::vector<BonePose>    bones_;
    std::vector<std::string> slotAttachments_;
};

}  // namespace eve::animation
