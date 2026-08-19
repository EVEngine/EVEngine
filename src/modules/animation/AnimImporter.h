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
 * Import AnimSkeleton / AnimClip from an Assimp scene (FBX/glTF/etc),
 * or from compact `.eva` text fixtures derived from those assets.
 */
class AnimImporter {
public:
    /** Build skeleton from the scene node hierarchy (depth-first). */
    static AnimSkeleton *loadSkeleton(const aiScene *scene);

    /** Load clip by index; maps node-name channels onto skeleton bones. */
    static AnimClip *loadClip(const aiScene *scene, const AnimSkeleton *skeleton, int animIndex = 0);

    /** Convenience: ModelData wrappers (implemented in AnimImporterModel.cpp). */
    static AnimSkeleton *loadSkeletonFromModel(const model3d::ModelData *model);
    static AnimClip *loadClipFromModel(const model3d::ModelData *model, const AnimSkeleton *skeleton,
                                       int animIndex = 0);
    static int         getAnimationCountFromModel(const model3d::ModelData *model);
    static std::string getAnimationNameFromModel(const model3d::ModelData *model, int animIndex);

    static int         getAnimationCount(const aiScene *scene);
    static std::string getAnimationName(const aiScene *scene, int animIndex);

    /**
     * Serialize skeleton + clip to a compact text format (no mesh).
     * Used to ship Mixamo-derived test fixtures without redistributing skins.
     */
    static std::string exportEva(const AnimSkeleton *skeleton, const AnimClip *clip);

    /** Load skeleton+clip from exportEva text (new'd; caller owns). */
    static void importEva(const std::string &text, AnimSkeleton **skeletonOut, AnimClip **clipOut);

    /** Load from `.eva` file path. */
    static void importEvaFile(const std::string &path, AnimSkeleton **skeletonOut,
                              AnimClip **clipOut);

private:
    static void collectBones(const aiNode *node, int parent, AnimSkeleton *skeleton,
                             std::vector<const aiNode *> &order);
};

}  // namespace eve::animation
