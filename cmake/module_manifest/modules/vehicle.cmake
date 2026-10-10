# ---------------------------------------------------------------------------
# Host package: vehicle
# ---------------------------------------------------------------------------

# L5 -- vehicle

# Vehicle entities, kinematic/tracked/wheeled mobility and the Vehicle adapter
# over the generic orders queue. Depends on weapon so definitions can declare
# weapon mounts.
eve_declare_module(NAME vehicle LAYER 5 SCRIPT Vehicle SLOT vehicle
                   DEPS attributes definitions effects orders weapon settlement game_event
                   OPTIONAL_DEPS physics
                   GROUP 2d 3d web)
