# ---------------------------------------------------------------------------
# Host package: hd2d
# ---------------------------------------------------------------------------

# L5 -- hd2d
# HD-2D: extrudes a 2D map::TileLayer into a 3D terrain mesh (TileMap3D) and
# renders 2D sprite sheets / characters as camera-facing 3D billboards
# (Sprite3D) via the ECS Renderable3D forward path.
eve_declare_module(NAME hd2d LIB EVHd2D LAYER 5 SCRIPT Hd2D SLOT hd2d
                   DEPS graphics map
                   GROUP 3d)

# L7 -- hd2d
eve_declare_module(NAME hd2d_editor LAYER 7 DEPS editor hd2d_editing GROUP 3d)

# L6 -- hd2d
eve_declare_module(NAME hd2d_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS hd2d
                   GROUP 3d)
