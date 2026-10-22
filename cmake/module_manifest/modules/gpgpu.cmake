# ---------------------------------------------------------------------------
# Host package: gpgpu
# ---------------------------------------------------------------------------

# L4 -- gpgpu
eve_declare_module(NAME gpgpu LAYER 4 SCRIPT Gpgpu SLOT gpgpu
                   DEPS data filesystem graphics
                   GROUP 2d 3d web)
