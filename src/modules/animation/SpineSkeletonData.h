#pragma once
#include "common/Export.h"


#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

/**
 * @brief Spine skeleton JSON data (bones / slots / skins / animations).
 *
 * Supports a practical subset for 2D region attachments:
 * - bones: name, parent, x, y, rotation, scaleX, scaleY
 * - slots: name, bone, attachment
 * - skins: region attachments (x/y/rotation/width/height/path)
 * - animations: bone translate/rotate/scale + slot attachment timelines
 *
 * Mesh / IK / path / clipping / deform are ignored (use official spine-cpp
 * plugin for full runtime). Script type: `SpineSkeletonData`.
 */
class EVENGINE_API_WORLD SpineSkeletonData {
public:
    /** @brief Spine skeleton data. */
    SpineSkeletonData() = default;
    /** @brief Spine skeleton data. */
    ~SpineSkeletonData() = default;

    SpineSkeletonData(const SpineSkeletonData &)            = delete;
    SpineSkeletonData &operator=(const SpineSkeletonData &) = delete;

    /** @brief Loads from json. */
    bool loadFromJson(const std::string &json, std::string *error = nullptr);
    /** @brief Loads from file. */
    bool loadFromFile(const std::string &path, std::string *error = nullptr);
    /** @brief Clears . */
    void clear();

    /** @brief Returns the spine version. */
    std::string getSpineVersion() const { return spineVersion_; }

    /** @brief Returns the bone count. */
    int         getBoneCount() const { return static_cast<int>(bones_.size()); }
    /** @brief Finds bone. */
    int         findBone(const std::string &name) const;
    /** @brief Returns the bone name. */
    std::string getBoneName(int index) const;
    /** @brief Returns the bone parent. */
    int         getBoneParent(int index) const;
    /** @brief Returns the bone x. */
    float       getBoneX(int index) const;
    /** @brief Returns the bone y. */
    float       getBoneY(int index) const;
    /** @brief Returns the bone rotation. */
    float       getBoneRotation(int index) const;
    /** @brief Returns the bone scale x. */
    float       getBoneScaleX(int index) const;
    /** @brief Returns the bone scale y. */
    float       getBoneScaleY(int index) const;

    /** @brief Returns the slot count. */
    int         getSlotCount() const { return static_cast<int>(slots_.size()); }
    /** @brief Finds slot. */
    int         findSlot(const std::string &name) const;
    /** @brief Returns the slot name. */
    std::string getSlotName(int index) const;
    /** @brief Returns the slot bone. */
    int         getSlotBone(int index) const;
    /** @brief Returns the slot attachment. */
    std::string getSlotAttachment(int index) const;

    /** @brief Returns the skin count. */
    int         getSkinCount() const { return static_cast<int>(skins_.size()); }
    /** @brief Finds skin. */
    int         findSkin(const std::string &name) const;
    /** @brief Returns the skin name. */
    std::string getSkinName(int index) const;
    /** @brief Returns the default skin. */
    int         getDefaultSkin() const { return defaultSkin_; }

    /** @brief Returns the animation count. */
    int         getAnimationCount() const { return static_cast<int>(anims_.size()); }
    /** @brief Finds animation. */
    int         findAnimation(const std::string &name) const;
    /** @brief Returns the animation name. */
    std::string getAnimationName(int index) const;
    /** @brief Returns the animation duration. */
    float       getAnimationDuration(int index) const;

    // --- Internal data accessed by SpineSkeleton / SpineAnim ---

    /** @brief BoneData public API. */
    struct BoneData {
        std::string name;
        int         parent = -1;
        float       x = 0.f, y = 0.f;
        float       rotation = 0.f;
        float       scaleX = 1.f, scaleY = 1.f;
    };

    struct SlotData {
        std::string name;
        int         bone = 0;
        std::string attachment;  // setup pose attachment name (may be empty)
    };

    struct RegionAttachment {
        std::string name;
        std::string path;  // atlas region key (defaults to name)
        float       x = 0.f, y = 0.f;
        float       rotation = 0.f;
        float       width = 0.f, height = 0.f;
        float       scaleX = 1.f, scaleY = 1.f;
    };

    /** @brief slotIndex → attachmentName → region */
    using SkinAttachments = std::unordered_map<int, std::unordered_map<std::string, RegionAttachment>>;

    struct SkinData {
        std::string     name;
        SkinAttachments attachments;
    };

    struct FloatKey {
        float time  = 0.f;
        float value = 0.f;
        bool  stepped = false;
    };

    struct TranslateKey {
        float time = 0.f;
        float x = 0.f, y = 0.f;
        bool  stepped = false;
    };

    struct ScaleKey {
        float time = 0.f;
        float x = 1.f, y = 1.f;
        bool  stepped = false;
    };

    struct AttachmentKey {
        float       time;
        std::string name;  // empty = detach
    };

    struct BoneTimeline {
        int                       boneIndex = -1;
        std::vector<FloatKey>     rotate;
        std::vector<TranslateKey> translate;
        std::vector<ScaleKey>     scale;
    };

    struct SlotTimeline {
        int                         slotIndex = -1;
        std::vector<AttachmentKey>  attachment;
    };

    struct AnimationData {
        std::string                name;
        float                      duration = 0.f;
        std::vector<BoneTimeline>  bones;
        std::vector<SlotTimeline>  slots;
    };

    const BoneData      &bone(int i) const { return bones_.at(static_cast<size_t>(i)); }
    const SlotData      &slot(int i) const { return slots_.at(static_cast<size_t>(i)); }
    const SkinData      &skin(int i) const { return skins_.at(static_cast<size_t>(i)); }
    const AnimationData &animation(int i) const { return anims_.at(static_cast<size_t>(i)); }

    const RegionAttachment *findAttachment(int skinIndex, int slotIndex,
                                           const std::string &name) const;

private:
    void checkBone(int index) const;
    void checkSlot(int index) const;
    void checkSkin(int index) const;
    void checkAnim(int index) const;

    std::string                                      spineVersion_;
    std::vector<BoneData>                            bones_;
    std::vector<SlotData>                            slots_;
    std::vector<SkinData>                            skins_;
    std::vector<AnimationData>                       anims_;
    std::unordered_map<std::string, int>             boneByName_;
    std::unordered_map<std::string, int>             slotByName_;
    std::unordered_map<std::string, int>             skinByName_;
    std::unordered_map<std::string, int>             animByName_;
    int                                              defaultSkin_ = -1;
};

}  // namespace eve::animation
