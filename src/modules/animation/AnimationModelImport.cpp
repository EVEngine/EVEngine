#include "animation/Animation.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimLattice.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::animation {

AnimSkeleton *Animation::newSkeletonFromModel(eve::model3d::ModelData *model) {
    return AnimImporter::loadSkeletonFromModel(model);
}

AnimClip *Animation::newClipFromModel(eve::model3d::ModelData *model, AnimSkeleton *skeleton,
                                      int animIndex) {
    return AnimImporter::loadClipFromModel(model, skeleton, animIndex);
}

AnimSkin *Animation::newSkinFromModel(eve::model3d::ModelData *model, int meshIndex,
                                      AnimSkeleton *skeleton) {
    return AnimSkin::fromModel(model, meshIndex, skeleton);
}

AnimLattice *Animation::newLatticeFromModel(eve::model3d::ModelData *model, int meshIndex, int divX,
                                            int divY, int divZ) {
    return AnimLattice::fromModel(model, meshIndex, divX, divY, divZ);
}

void exposeAnimationModelImportBindings(ssq::Class &cls) {
    cls.addFunc("newSkeletonFromModel", &Animation::newSkeletonFromModel);
    cls.addFunc("newClipFromModel", &Animation::newClipFromModel);
    cls.addFunc("newSkinFromModel", &Animation::newSkinFromModel);
    cls.addFunc("newLatticeFromModel", &Animation::newLatticeFromModel);
}

}  // namespace eve::animation
