# ---------------------------------------------------------------------------
# Host package: growth (farm plant/animal simulation)
# ---------------------------------------------------------------------------

# L0 -- deterministic organism growth: crops, livestock, seasons, disease, breeding.
# Presentation (hex decorations / scene meshes) stays outside this package.
eve_declare_module(NAME growth LAYER 0 SCRIPT Growth SLOT growth
                   GROUP minimal 2d 3d web)
