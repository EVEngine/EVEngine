# ---------------------------------------------------------------------------
# Host package: rts
# ---------------------------------------------------------------------------

# L5 -- rts
# links; these are the direct implementation dependencies of the profile.
eve_declare_module(NAME rts LAYER 5 SCRIPT RTS SLOT rts
                   DEPS action attributes combat crowd definitions economy effects map orders production sensing transaction weapon
                   GROUP 2d 3d)
