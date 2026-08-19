#pragma once

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
class SpineSkeletonData {
public:
    SpineSkeletonData() = default;
    ~SpineSkeletonData() = default;

    SpineSkeletonData(const SpineSkeletonData &)            = delete;
    SpineSkeletonData &operator=(const SpineSkeletonData &) = delete;

    bool loadFromJson(const std::string &json, std::string *error = nullptr);
    bool loadFromFile(const std::string &path, std::string *error = nullptr);
    void clear();

    std::string getSpineVersion() const { return spineVersion_; }

    int         getBoneCount() const { return static_cast<int>(bones_.size()); }
    int         findBone(const std::string &name) const;
    std::string getBoneName(int index) const;
    int         getBoneParent(int index) const;
    float       getBoneX(int index) const;
    float       getBoneY(int index) const;
    float       getBoneRotation(int index) const;
    float       getBoneScaleX(int index) const;
    float       getBoneScaleY(int index) const;

    int         getSlotCount() const { return static_cast<int>(slots_.size()); }
    int         findSlot(const std::string &name) const;
    std::string getSlotName(int index) const;
    int         getSlotBone(int index) const;
    std::string getSlotAttachment(int index) const;

    int         getSkinCount() const { return static_cast<int>(skins_.size()); }
    int         findSkin(const std::string &name) const;
    std::string getSkinName(int index) const;
    int         getDefaultSkin() const { return defaultSkin_; }

    int         getAnimationCount() const { return static_cast<int>(anims_.size()); }
    int         findAnimation(const std::string &name) const;
    std::string getAnimationName(int index) const;
    float       getAnimationDuration(int index) const;

    // --- Internal data accessed by SpineSkeleton / SpineAnim ---

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
