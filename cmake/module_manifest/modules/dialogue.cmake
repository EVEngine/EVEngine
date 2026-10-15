# ---------------------------------------------------------------------------
# Host package: dialogue
# ---------------------------------------------------------------------------

# L7 -- dialogue
eve_declare_module(NAME dialogue_editor LAYER 7 DEPS audio_editor dialogue_editing editor GROUP 3d web)

# L6 -- dialogue
# Authoring documents stay available without the dialogue runtime (audio/avatar
# are trimmed on WASM). Runtime bridges drop out via OPTIONAL_DEPS.
eve_declare_module(NAME dialogue_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS dialogue
                   GROUP 3d web)

eve_declare_module(NAME dialogue LAYER 6
                   SCRIPT Dialogue DialogueUX DialogueVoice DialogueFlow
                   SLOT dialogue dialogueUX dialogueVoice dialogueFlow
                   DEPS avatar audio decision dnut_interpreter filesystem transaction)
