#pragma once

#include <string>
#include <vector>

struct aiScene;
struct aiNode;

namespace eve {
namespace model3d {
class ModelData;
}
}  // namespace eve

namespace eve::animation {

class AnimSkeleton;
class AnimClip;

/**
 * @brief Import AnimSkeleton / AnimClip from an Assimp scene (FBX/glTF/etc),
 * or from compact `*.anim.txt` fixtures derived from those assets.
 */
class AnimImporter {
public:
    /** @brief Build skeleton from the scene node hierarchy (depth-first). */
    static AnimSkeleton *loadSkeleton(const aiScene *scene);

    /** @brief Load clip by index; maps node-name channels onto skeleton bones. */
    static AnimClip *loadClip(const aiScene *scene, const AnimSkeleton *skeleton, int animIndex = 0);

    /** @brief Convenience: ModelData wrappers (implemented in AnimImporterModel.cpp). */
    static AnimSkeleton *loadSkeletonFromModel(const model3d::ModelData *model);
    static AnimClip *loadClipFromModel(const model3d::ModelData *model, const AnimSkeleton *skeleton,
                                       int animIndex = 0);
    static int         getAnimationCountFromModel(const model3d::ModelData *model);
    static std::string getAnimationNameFromModel(const model3d::ModelData *model, int animIndex);

    static int         getAnimationCount(const aiScene *scene);
    static std::string getAnimationName(const aiScene *scene, int animIndex);

    /**
     * @brief Serialize skeleton + clip to a compact text format (no mesh).
     * Used to ship Mixamo-derived test fixtures without redistributing skins.
     */
    static std::string exportAnimationFixtureText(const AnimSkeleton *skeleton, const AnimClip *clip);

    /** @brief Load skeleton+clip from fixture text (new'd; caller owns). */
    static void importAnimationFixtureText(const std::string &text, AnimSkeleton **skeletonOut,
                                           AnimClip **clipOut);

    /** @brief Load from an `*.anim.txt` fixture path. */
    static void importAnimationFixtureTextFile(const std::string &path, AnimSkeleton **skeletonOut,
                                               AnimClip **clipOut);

private:
    static void collectBones(const aiNode *node, int parent, AnimSkeleton *skeleton,
                             std::vector<const aiNode *> &order);
};

}  // namespace eve::animation
