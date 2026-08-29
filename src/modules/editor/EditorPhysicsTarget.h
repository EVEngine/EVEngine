#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "editor/EditorTargetV2.h"
#include "physics_editing/PhysicsTarget.h"

namespace eve::editor {

using PhysicsColliderTarget           = eve::physics_editing::PhysicsColliderTarget;
using IPhysicsColliderRuntimeSink     = eve::physics_editing::IPhysicsColliderRuntimeSink;
using PhysicsColliderPublishingTarget = eve::physics_editing::PhysicsColliderPublishingTarget;
using PhysicsJointTarget              = eve::physics_editing::PhysicsJointTarget;
using PhysicsColliderRuntimeBuilder   = eve::physics_editing::PhysicsColliderRuntimeBuilder;
using PhysicsCollider3DRuntimeSink    = eve::physics_editing::PhysicsCollider3DRuntimeSink;
using PhysicsCollider2DRuntimeSink    = eve::physics_editing::PhysicsCollider2DRuntimeSink;
using IPhysicsColliderAssetResolver   = eve::physics_editing::IPhysicsColliderAssetResolver;

}  // namespace eve::editor
