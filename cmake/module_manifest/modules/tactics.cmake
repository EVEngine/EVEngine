# ---------------------------------------------------------------------------
# Host package: tactics
# ---------------------------------------------------------------------------

# L5 -- tactics
# providers are introduced by adapters as their implementation slices land;
# the phase-one board/turn core depends only on common engine contracts.
eve_declare_module(NAME tactics LAYER 5 SCRIPT Tactics SLOT tactics
                   DEPS action sensing settlement
                   GROUP 2d 3d web)
