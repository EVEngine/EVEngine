# ---------------------------------------------------------------------------
# Host package: voxel
# ---------------------------------------------------------------------------

# L5 -- voxel
# WebGPU face instances live in graphics/webgpu/GraphicsVoxel.cpp, but the
# public VoxelWorld API embeds procgen::TerrainStreamingCache / TerrainSampler.
# Soft-dep + web membership wait on that API split (procgen still pulls map).
eve_declare_module(NAME voxel LAYER 5 SCRIPT Voxel
                   DEPS graphics procgen thread
                   GROUP 3d)

# L6 -- voxel
eve_declare_module(NAME voxel_editing LAYER 6
                   DEPS editing
                   OPTIONAL_DEPS voxel
                   GROUP 3d)

# L7 -- voxel
eve_declare_module(NAME voxelworld_target DIR voxel/voxelworld_target LAYER 7
                   SCRIPT VoxelWorldTargetModule SLOT voxelWorldTarget
                   DEPS editor voxel voxel_editing
                   GROUP 3d)

eve_declare_module(NAME voxel_editor LAYER 7 DEPS editor voxel_editing
                   SCRIPT VoxelEditorModule SLOT voxelEditor GROUP 3d)
