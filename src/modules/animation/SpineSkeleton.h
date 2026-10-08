#pragma once
#include "common/Export.h"


#include "animation/SpineSkeletonData.h"

#include <string>
#include <vector>

namespace eve::animation {

/**
 * @brief Runtime Spine skeleton pose (local + world bone transforms, slot attachments).
 * Script type: `SpineSkeleton`.
 */
class EVENGINE_API_WORLD SpineSkeleton {
public:
    /** @brief Spine skeleton. */
    explicit SpineSkeleton(SpineSkeletonData *data);
    /** @brief Spine skeleton. */
    ~SpineSkeleton() = default;

    SpineSkeleton(const SpineSkeleton &)            = delete;
    SpineSkeleton &operator=(const SpineSkeleton &) = delete;

    /** @brief Returns the data. */
    SpineSkeletonData *getData() const { return data_; }

    /** @brief Sets the skin. */
    bool setSkin(const std::string &name);
    /** @brief Returns the skin. */
    int  getSkin() const { return skinIndex_; }

    /** @brief Sets the to setup pose. */
    void setToSetupPose();
    /** @brief Updates world transform. */
    void updateWorldTransform();

    /** @brief Returns the bone count. */
    int   getBoneCount() const;
    /** @brief Returns the bone local x. */
    float getBoneLocalX(int index) const;
    /** @brief Returns the bone local y. */
    float getBoneLocalY(int index) const;
    /** @brief Returns the bone local rotation. */
    float getBoneLocalRotation(int index) const;
    /** @brief Returns the bone local scale x. */
    float getBoneLocalScaleX(int index) const;
    /** @brief Returns the bone local scale y. */
    float getBoneLocalScaleY(int index) const;

    /** @brief Sets the bone local x. */
    void setBoneLocalX(int index, float x);
    /** @brief Sets the bone local y. */
    void setBoneLocalY(int index, float y);
    /** @brief Sets the bone local rotation. */
    void setBoneLocalRotation(int index, float degrees);
    /** @brief Sets the bone local scale x. */
    void setBoneLocalScaleX(int index, float sx);
    /** @brief Sets the bone local scale y. */
    void setBoneLocalScaleY(int index, float sy);

    /** @brief Returns the bone world x. */
    float getBoneWorldX(int index) const;
    /** @brief Returns the bone world y. */
    float getBoneWorldY(int index) const;
    /** @brief Returns the bone world rotation. */
    float getBoneWorldRotation(int index) const;
    /** @brief Returns the bone world scale x. */
    float getBoneWorldScaleX(int index) const;
    /** @brief Returns the bone world scale y. */
    float getBoneWorldScaleY(int index) const;
    /** @brief World 2x2 matrix columns (a,c) / (b,d) used for local→world offset. */
    void getBoneWorldMatrix(int index, float &a, float &b, float &c, float &d) const;

    /** @brief Returns the slot count. */
    int         getSlotCount() const;
    /** @brief Returns the slot attachment name. */
    std::string getSlotAttachmentName(int slotIndex) const;
    /** @brief Sets the slot attachment name. */
    void        setSlotAttachmentName(int slotIndex, const std::string &name);

    /** @brief Returns the slot region. */
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
