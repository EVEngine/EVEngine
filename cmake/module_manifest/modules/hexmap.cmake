# ---------------------------------------------------------------------------
# Host package: hexmap
# ---------------------------------------------------------------------------

# L4 -- hexmap
# Interactive 3D hex map: an editable pointy-top cell grid plus per-chunk mesh
# generation (ground fans, blend strips, terraces, cliffs, water, rivers, roads,
# city walls, decorations and the fog overlay).
eve_declare_module(NAME hexmap LAYER 4 SCRIPT HexMap SLOT hexmap
                   DEPS graphics
                   GROUP 3d web)
