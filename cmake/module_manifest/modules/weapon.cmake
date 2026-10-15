# ---------------------------------------------------------------------------
# Host package: weapon
# ---------------------------------------------------------------------------

# L4 -- weapon
# Weapon definitions, entities, mounts and fire logic. Standalone so buildings /
# vehicles / turrets all attach the same WeaponMount system.
eve_declare_module(NAME weapon LAYER 4 SCRIPT Weapon SLOT weapon
                   DEPS action attributes effects transaction definitions
                   GROUP 2d 3d web)
