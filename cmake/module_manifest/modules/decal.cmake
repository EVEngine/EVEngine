# ---------------------------------------------------------------------------
# Host package: decal
# ---------------------------------------------------------------------------

# L4 -- decal
# stylize / decal keep GROUP web: native Dawn and the browser profile already
# ship their WGSL paths, and several web-tagged editors hard-depend on them.
eve_declare_module(NAME decal LAYER 4 SCRIPT Decal SLOT decal
                   DEPS graphics stylize
                   GROUP 3d web)

# L5 -- decal
eve_declare_module(NAME decal_editing LAYER 5
                   DEPS decal editing
                   GROUP 3d web)

# L7 -- decal
eve_declare_module(NAME decal_editor LAYER 7 DEPS decal_editing editor GROUP 3d web)
