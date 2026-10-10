# ---------------------------------------------------------------------------
# Host package: stylize
# ---------------------------------------------------------------------------

# L4 -- stylize
eve_declare_module(NAME stylize LAYER 4 SCRIPT Stylize SLOT stylize
                   DEPS graphics image
                   GROUP 3d web)

# L5 -- stylize
eve_declare_module(NAME stylize_editing LAYER 5
                   DEPS editing stylize
                   GROUP 3d web)

# Optional Action adapter for layered AttackVfx recipes (presentation:attack-vfx*).
# audio/sound are optional: WASM/web profiles do not ship Wuff/OpenAL; the Audio
# AttackVfx executor compiles only when both runtime modules are present.
eve_declare_module(NAME stylize_action DIR stylize/action LAYER 5
                   SCRIPT StylizeAction SLOT stylizeAction
                   DEPS action filesystem stylize
                   OPTIONAL_DEPS audio sound
                   GROUP 3d web)

# L7 -- stylize
eve_declare_module(NAME stylize_editor LAYER 7 DEPS editor graphics_editor stylize_editing GROUP 3d web)
