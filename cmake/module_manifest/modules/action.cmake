# ---------------------------------------------------------------------------
# Host package: action
# ---------------------------------------------------------------------------

# L7 -- action
# Domain-facing editor compatibility adapters. These modules preserve the
# eve::editor type spellings without forcing the editor core to link every
# domain editing implementation.
eve_declare_module(NAME action_editor LAYER 7 SCRIPT ActionEditorModule SLOT actionEditor
                   DEPS action animation editor GROUP 3d)
