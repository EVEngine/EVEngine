# ---------------------------------------------------------------------------
# Host package: scene
# ---------------------------------------------------------------------------

# L5 -- scene
eve_declare_module(NAME scene_physics DIR scene/physics LAYER 5 SCRIPT ScenePhysics SLOT scenePhysics
                   DEPS physics scene
                   GROUP 3d web)

# L1 -- scene

# Renderables, bodies and audio sources attach through registered link kinds
# (scene/SceneLink.h), so scene no longer depends on those modules. The picking
# entry points that take a Camera3D stay in scene and ask for the camera
# projection through the ISceneCameraProjection capability, which graphics
# provides (graphics/ScenePicking.cpp, excluded when scene is off).
eve_declare_module(NAME scene LAYER 1 SCRIPT Scene SLOT scene
                   DEPS spatial
                   THIRDPARTY poco
                   GROUP 3d web)

# L6 -- scene
eve_declare_module(NAME sceneloader_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS sceneloader
                   GROUP 3d web)

# L7 -- scene
eve_declare_module(NAME scene_editor LAYER 7 DEPS editor physics scene_editing OPTIONAL_DEPS scene
                   SCRIPT SceneEditorModule SLOT sceneEditor GROUP 3d web)

eve_declare_module(NAME sceneloader_editor LAYER 7 DEPS editor sceneloader_editing GROUP 3d web)

# L6 -- scene
eve_declare_module(NAME sceneloader LIB EVSceneLoader LAYER 6 SCRIPT SceneLoader
                   DEPS action animation data filesystem graphics image model3d scene thread
                   THIRDPARTY assimp
                   GROUP 3d)
