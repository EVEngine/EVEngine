# ---------------------------------------------------------------------------
# Host package: archspace
# ---------------------------------------------------------------------------

# L4 -- archspace
eve_declare_module(NAME archspace LIB EVArchSpace LAYER 4 SCRIPT ArchSpace SLOT archspace
                   DEPS data
                   GROUP 3d web)

# L5 -- archspace
eve_declare_module(NAME archspace_editing LAYER 5
                   DEPS archspace editing
                   GROUP 3d)

# L7 -- archspace
eve_declare_module(NAME archspace_editor LAYER 7
                   DEPS archspace_editing editor
                   SCRIPT ArchSpaceEditorModule SLOT archspaceEditor
                   GROUP 3d)
