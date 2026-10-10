# ---------------------------------------------------------------------------
# Host package: climbing
# ---------------------------------------------------------------------------

# L5 -- climbing
# Deterministic climbing/parkour planning and capsule-constrained execution.
eve_declare_module(NAME climbing LAYER 5 SCRIPT Climbing SLOT climbing
                   DEPS animation physics
                   GROUP 3d)
