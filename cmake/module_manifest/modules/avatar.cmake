# ---------------------------------------------------------------------------
# Host package: avatar
# ---------------------------------------------------------------------------

# L5 -- avatar
eve_declare_module(NAME avatar LAYER 5 SCRIPT Avatar SLOT avatar
                   DEPS animation graphics inventory model3d scene
                   GROUP 3d)

# L7 -- avatar
eve_declare_module(NAME avatar_editor LAYER 7 SCRIPT AvatarEditorModule SLOT avatarEditor
                   DEPS avatar_editing editor GROUP 3d)

# L6 -- avatar
eve_declare_module(NAME avatar_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS avatar
                   GROUP 3d)
