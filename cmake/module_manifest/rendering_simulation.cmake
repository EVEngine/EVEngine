# ---------------------------------------------------------------------------
# L4 -- rendering extensions and simulation
# ---------------------------------------------------------------------------

eve_declare_module(NAME material_graphics_editing LAYER 4
                   DEPS graphics material_editing
                   GROUP 3d web)
eve_declare_module(NAME graphics_editing LAYER 4
                   DEPS editing graphics image
                   GROUP 2d 3d web)

# Typed bridge from admitted runtime packages into backend-owned GPU resources.
eve_declare_module(NAME asset_graphics LAYER 4
                   DEPS asset graphics
                   GROUP minimal 2d 3d web)

eve_declare_module(NAME pixelworld_graphics LAYER 4 SCRIPT PixelWorldGraphics SLOT pixelworldGraphics
                   DEPS graphics pixelworld
                   GROUP 2d 3d web)

eve_declare_module(NAME camera LAYER 4 SCRIPT Camera SLOT camera
                   DEPS platform_event graphics scene
                   GROUP minimal 2d 3d web)
eve_declare_module(NAME gpgpu LAYER 4 SCRIPT Gpgpu SLOT gpgpu
                   DEPS data filesystem graphics
                   GROUP 2d 3d web)
eve_declare_module(NAME ui LIB EVUI LAYER 4 SCRIPT UI SLOT ui
                   DEPS platform_event filesystem graphics image property_access scriptmodel timer window
                   THIRDPARTY sdl2 poco
                   GROUP minimal 2d 3d web)
# The public Physics facade retains its interactive presentation dependencies;
# src/modules/CMakeLists.txt separately compiles the domain core (World/Body/
# Shape/Joint/query/fixed-step) without those dependencies for core profiles.
eve_declare_module(NAME physics LAYER 4 SCRIPT Physics SLOT physics
                   DEPS platform_event graphics gpgpu sensing
                   OPTIONAL_DEPS scene
                   THIRDPARTY box2d box3d
                   GROUP 2d 3d web)
eve_declare_module(NAME map LAYER 4 SCRIPT Map SLOT map
                   DEPS data filesystem graphics grid
                   THIRDPARTY poco
                   GROUP 2d 3d)
eve_declare_module(NAME map_editing LAYER 4
                   DEPS editing
                   OPTIONAL_DEPS map
                   GROUP 3d web)
eve_declare_module(NAME buildingfx LIB EVBuildingFx LAYER 4 SCRIPT BuildingFx SLOT buildingfx
                   DEPS building graphics)
# Weapon definitions, entities, mounts and fire logic. Standalone so buildings /
# vehicles / turrets all attach the same WeaponMount system.
eve_declare_module(NAME weapon LAYER 4 SCRIPT Weapon SLOT weapon
                   DEPS action attributes effects transaction definitions
                   GROUP 2d 3d)
# L5 -- vehicle adapter
eve_declare_module(NAME pixelworld_physics LAYER 5
                   DEPS pixelworld physics
                   GROUP 2d)
# Optional editing satellite. Runtime-only profiles can enable physics without
# pulling editing/editor contracts or AssetDB adapters.
eve_declare_module(NAME physics_editing LAYER 5
                   DEPS editing physics
                   GROUP 3d web)

# Vehicle entities, kinematic/tracked/wheeled mobility and the Vehicle adapter
# over the generic orders queue. Depends on weapon so definitions can declare
# weapon mounts.
eve_declare_module(NAME vehicle LAYER 5 SCRIPT Vehicle SLOT vehicle
                   DEPS attributes definitions effects orders weapon settlement game_event
                   OPTIONAL_DEPS physics
                   GROUP 2d 3d)
# L4 -- rendering extensions and simulation (continued)
eve_declare_module(NAME animation LAYER 4 SCRIPT Animation SLOT anim
                   DEPS data filesystem graphics image model3d
                   THIRDPARTY poco assimp
                   GROUP 2d 3d)
eve_declare_module(NAME daynight LIB EVDayNight LAYER 4 SCRIPT DayNight SLOT daynight
                   DEPS graphics
                   GROUP 3d web)
eve_declare_module(NAME weather LAYER 4 SCRIPT Weather SLOT weather
                   DEPS graphics
                   GROUP 3d web)
eve_declare_module(NAME decal LAYER 4 SCRIPT Decal SLOT decal
                   DEPS graphics
                   GROUP 3d)
eve_declare_module(NAME stylize LAYER 4 SCRIPT Stylize SLOT stylize
                   DEPS graphics image
                   GROUP 3d)
# L5 -- voxel aggregate
eve_declare_module(NAME voxel LAYER 5 SCRIPT Voxel
                   DEPS graphics procgen thread
                   GROUP 3d)
# L4 -- rendering extensions (continued)
eve_declare_module(NAME spritestack LIB EVSpriteStack LAYER 4 SCRIPT SpriteStack SLOT spritestack
                   DEPS graphics image model3d
                   GROUP 2d)
eve_declare_module(NAME housegen LIB EVHouseGen LAYER 4 SCRIPT HouseGen
                   DEPS data graphics image model3d
                   GROUP 3d)
eve_declare_module(NAME card LAYER 4 SCRIPT Card
                   DEPS attributes decision definitions effects graphics transaction)
eve_declare_module(NAME demo LAYER 4 SCRIPT Demo
                   DEPS graphics sound)
