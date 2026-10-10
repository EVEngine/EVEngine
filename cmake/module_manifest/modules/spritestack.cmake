# ---------------------------------------------------------------------------
# Host package: spritestack
# ---------------------------------------------------------------------------

# L4 -- spritestack
# Pure-2D stacks + WGSL card path need only graphics/image. Assimp-backed
# sliceModel lives in SpriteStackModel.cpp and follows OPTIONAL_DEPS model3d.
eve_declare_module(NAME spritestack LIB EVSpriteStack LAYER 4 SCRIPT SpriteStack SLOT spritestack
                   DEPS graphics image
                   OPTIONAL_DEPS model3d
                   GROUP 2d web)

# L5 -- spritestack
eve_declare_module(NAME spritestack_editing LAYER 5
                   DEPS editing
                   OPTIONAL_DEPS spritestack
                   GROUP 2d 3d)

# L7 -- spritestack
eve_declare_module(NAME spritestack_editor LAYER 7 DEPS editor spritestack_editing GROUP 2d 3d)
