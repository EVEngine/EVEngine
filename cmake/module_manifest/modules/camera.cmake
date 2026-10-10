# ---------------------------------------------------------------------------
# Host package: camera
# ---------------------------------------------------------------------------

# L4 -- camera

eve_declare_module(NAME camera LAYER 4 SCRIPT Camera SLOT camera
                   DEPS action platform_event graphics scene
                   GROUP minimal 2d 3d web)

# L5 -- camera
eve_declare_module(NAME camera_editing LAYER 5
                   DEPS camera editing
                   GROUP 3d web)

# L7 -- camera
eve_declare_module(NAME camera_editor LAYER 7 DEPS camera_editing domain_gizmo_editor editor GROUP 3d web)
