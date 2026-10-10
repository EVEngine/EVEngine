# ---------------------------------------------------------------------------
# Host package: editor
# ---------------------------------------------------------------------------

# L7 -- editor
eve_declare_module(NAME domain_gizmo_editor LAYER 7 DEPS domain_gizmo_editing editor GROUP 3d web)

eve_declare_module(NAME input_editor LAYER 7 DEPS editor input_editing GROUP 2d 3d web)

eve_declare_module(NAME queue_editor LAYER 7 DEPS editor queue_editing GROUP 2d 3d web)

# L6 -- editor
eve_declare_module(NAME domain_gizmo_editing LAYER 6
                   DEPS audio_editing editing lighting_editing physics_editing
                   GROUP 3d web)
