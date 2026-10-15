# ---------------------------------------------------------------------------
# Host package: audio
# ---------------------------------------------------------------------------

# L7 -- audio
eve_declare_module(NAME audio_editor LAYER 7 SCRIPT AudioEditorModule SLOT audioEditor
                   DEPS audio_editing editor OPTIONAL_DEPS audio sound GROUP 3d web)
