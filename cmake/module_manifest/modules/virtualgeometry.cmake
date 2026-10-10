# ---------------------------------------------------------------------------
# Host package: virtualgeometry
# ---------------------------------------------------------------------------

# L5 -- virtualgeometry
# WGSL compute path is shared by native Dawn and the browser profile. No Assimp
# / Poco / OpenAL in the closure (only data + gpgpu + graphics).
eve_declare_module(NAME virtualgeometry LIB EVVirtualGeometry LAYER 5 SCRIPT VirtualGeometry
                   DEPS data gpgpu graphics
                   GROUP 3d web)

# L7 -- virtualgeometry
eve_declare_module(NAME virtualgeometry_editor LAYER 7 DEPS editor virtualgeometry_editing GROUP 3d)

# L6 -- virtualgeometry
eve_declare_module(NAME virtualgeometry_editing LAYER 6
                   DEPS editing virtualgeometry
                   GROUP 3d web)
