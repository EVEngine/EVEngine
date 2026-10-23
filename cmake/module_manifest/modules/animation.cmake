# ---------------------------------------------------------------------------
# Host package: animation
# ---------------------------------------------------------------------------

# L4 -- animation
# Runtime pose/clip/skin/lattice stay Assimp-free. ModelData/Assimp import
# (AnimImporterAssimp, *FromModel, AnimationModelImport) is an OPTIONAL_DEPS
# model3d bridge excluded when model3d is trimmed — mirrors spritestack.
eve_declare_module(NAME animation LAYER 4 SCRIPT Animation SLOT anim
                   DEPS action data filesystem graphics image
                   OPTIONAL_DEPS model3d
                   THIRDPARTY poco
                   GROUP 2d 3d)

# L5 -- animation
eve_declare_module(NAME animation_editing LAYER 5
                   DEPS editing
                   OPTIONAL_DEPS animation
                   GROUP 3d web)

# L6 -- animation
# Native 3d only: GROUP web would enable animation→model3d on the Emscripten
# profile, which still trims medialoader_model and then fails at link time.
eve_declare_module(NAME animation_tensor DIR animation/tensor LAYER 6
                   SCRIPT AnimationTensor SLOT animationTensor
                   DEPS animation tensor
                   GROUP 3d)

# L7 -- animation
eve_declare_module(NAME animation_editor LAYER 7 DEPS animation_editing editor
                   SCRIPT AnimationEditorModule SLOT animationEditor GROUP 3d web)
