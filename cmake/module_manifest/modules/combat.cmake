# ---------------------------------------------------------------------------
# Host package: combat
# ---------------------------------------------------------------------------

# L5 -- combat
# Optional map-backed steering provider. Keeping this bridge above both owners
# lets headless/minimal combat builds omit the rendering-heavy map closure.
eve_declare_module(NAME combat_navigation DIR combat/navigation LAYER 5
                   DEPS combat map
                   GROUP 2d 3d)
