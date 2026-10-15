# ---------------------------------------------------------------------------
# Host package: card
# ---------------------------------------------------------------------------

# L4 -- card
eve_declare_module(NAME card LAYER 4 SCRIPT Card
                   DEPS attributes decision definitions effects graphics settlement transaction
                   GROUP minimal 2d 3d web)
