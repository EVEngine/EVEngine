# ---------------------------------------------------------------------------
# Host package: archspace
# ---------------------------------------------------------------------------

# L4 -- archspace
eve_declare_module(NAME archspace LIB EVArchSpace LAYER 4 SCRIPT ArchSpace SLOT archspace
                   DEPS data
                   GROUP 3d web)

# L5 -- archspace
# TileLayer ortho/iso adapter. Kept above archspace+map so headless/minimal
# ArchSpace builds omit the map closure; core only exposes bakeGridProjection.
eve_declare_module(NAME archspace_map DIR archspace/map LAYER 5
                   DEPS archspace map
                   GROUP 3d)

# HD-2D TileMap3D adapter composed on archspace_map. Mesh upload stays host-driven
# (buildMesh/buildRenderable) so unit tests can assert TileLayer without Graphics.
eve_declare_module(NAME archspace_hd2d DIR archspace/hd2d LAYER 5
                   DEPS archspace_map hd2d
                   GROUP 3d)

eve_declare_module(NAME archspace_editing LAYER 5
                   DEPS archspace editing
                   GROUP 3d)

# L7 -- archspace
eve_declare_module(NAME archspace_editor LAYER 7
                   DEPS archspace_editing editor
                   SCRIPT ArchSpaceEditorModule SLOT archspaceEditor
                   GROUP 3d)
