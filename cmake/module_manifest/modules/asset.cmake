# ---------------------------------------------------------------------------
# Host package: asset
# ---------------------------------------------------------------------------

# L4 -- asset

# Typed bridge from admitted runtime packages into backend-owned GPU resources.
eve_declare_module(NAME asset_graphics LAYER 4
                   DEPS asset asset_scene graphics
                   GROUP minimal 2d 3d web)

# L5 -- asset
# Typed package bridge kept outside the physics domain core.
eve_declare_module(NAME asset_physics DIR asset/physics LAYER 5
                   DEPS asset physics_cloth
                   GROUP 3d web)


eve_declare_module(NAME asset_stylize DIR asset/stylize LAYER 5
                   DEPS asset asset_graphics asset_import stylize graphics data
                   GROUP 3d)

# L6 -- asset
# Runtime bridge from capability-selected packages into executable PointGraphs.
eve_declare_module(NAME asset_procgen LAYER 6
                   DEPS asset asset_graphics asset_import data graphics procgen
                   GROUP 3d)
