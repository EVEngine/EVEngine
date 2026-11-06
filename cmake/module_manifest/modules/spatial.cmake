# Host-owned opt-in numerical coordinates. The existing spatial module and its
# float indexes remain unchanged; no registration or per-frame service is added.
option(EVENGINE_MODULE_SPATIAL_MULTISCALE "Build explicit multiscale coordinate APIs" OFF)

# L0 -- spatial
eve_declare_module(NAME spatial_multiscale DIR spatial/multiscale LAYER 0)
