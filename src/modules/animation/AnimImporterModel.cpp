#include "animation/AnimImporter.h"

#include "common/Exception.h"
#include "model3d/ModelData.h"

namespace eve::animation {

AnimSkeleton *AnimImporter::loadSkeletonFromModel(const model3d::ModelData *model) {
    if (!model) throw Exception("AnimImporter.loadSkeletonFromModel: model is null");
    return loadSkeleton(model->getScene());
}

AnimClip *AnimImporter::loadClipFromModel(const model3d::ModelData *model,
                                          const AnimSkeleton *skeleton, int animIndex) {
    if (!model) throw Exception("AnimImporter.loadClipFromModel: model is null");
    return loadClip(model->getScene(), skeleton, animIndex);
}

int AnimImporter::getAnimationCountFromModel(const model3d::ModelData *model) {
    return model ? getAnimationCount(model->getScene()) : 0;
}

std::string AnimImporter::getAnimationNameFromModel(const model3d::ModelData *model,
                                                    int animIndex) {
    return model ? getAnimationName(model->getScene(), animIndex) : std::string();
}

}  // namespace eve::animation
