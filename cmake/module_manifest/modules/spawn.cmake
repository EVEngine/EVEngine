# Host package: spawn. Runtime core has no gameplay/map/render dependency.
# L0 -- spawn
eve_declare_module(NAME spawn LAYER 0 GROUP 2d 3d)

# L0 -- spawn: asset admission stays optional for server-owned typed content.
eve_declare_module(NAME spawn_definitions DIR spawn/definitions LAYER 0
                   DEPS spawn definitions GROUP 2d 3d)
