#pragma once

/** @file @brief Compatibility include for canonical animation clip authoring. */
#include "animation_editing/AnimationClip.h"
#include "editor/EditorAuthority.h"

namespace eve::editor {
using AnimationTransformKey        = animation_editing::AnimationTransformKey;
using AnimationBoneTrack           = animation_editing::AnimationBoneTrack;
using AnimationEventRecord         = animation_editing::AnimationEventRecord;
using AnimationMaskEntry           = animation_editing::AnimationMaskEntry;
using AnimationSampledBone         = animation_editing::AnimationSampledBone;
using AnimationClipPreview         = animation_editing::AnimationClipPreview;
using AnimationRetargetPreview     = animation_editing::AnimationRetargetPreview;
using IAnimationClipEditTarget     = animation_editing::IAnimationClipEditTarget;
using AnimationClipDocumentTarget  = animation_editing::AnimationClipDocumentTarget;
using AnimationClipRuntimeBuilder  = animation_editing::AnimationClipRuntimeBuilder;
}  // namespace eve::editor
