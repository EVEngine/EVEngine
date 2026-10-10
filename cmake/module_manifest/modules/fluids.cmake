# ---------------------------------------------------------------------------
# Host package: fluids
# ---------------------------------------------------------------------------

# L5 -- fluids
# Surface fluid simulation: particles constrained to mesh SDFs (flow down
# surfaces, droplet coalescence) with screen-space surface reconstruction. Its
# accelerator provider has an independent lifetime from physics_cloth.
eve_declare_module(NAME fluids LAYER 5 SCRIPT Fluids SLOT fluids
                   DEPS gpgpu graphics image physics physics_backend
                   OPTIONAL_DEPS model3d
                   GROUP 3d web)

# L7 -- fluids
eve_declare_module(NAME fluids_editor LAYER 7 SCRIPT FluidsEditorModule SLOT fluidsEditor
                   DEPS editor fluids fluids_editing graphics_editor GROUP 3d web)

# L6 -- fluids
eve_declare_module(NAME fluids_editing LAYER 6
                   DEPS editing fluids
                   GROUP 3d web)
