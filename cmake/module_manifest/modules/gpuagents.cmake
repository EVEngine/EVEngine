# ---------------------------------------------------------------------------
# Host package: gpuagents
# ---------------------------------------------------------------------------

# L5 -- gpuagents
# GPU Agents FX: fish / life-network / bird / petal solvers with shared
# AgentState, World environment, SDF obstacles, and instance renderer.
# P0 is CPU reference; P1 mirrors kernels through gpgpu.
eve_declare_module(NAME gpuagents LAYER 5 SCRIPT GpuAgents SLOT gpuAgents
                   DEPS gpgpu graphics
                   GROUP 3d web)

# L7 -- gpuagents
eve_declare_module(NAME gpuagents_editor LAYER 7
                   SCRIPT GpuAgentsEditorModule SLOT gpuAgentsEditor
                   DEPS editor gpuagents gpuagents_editing
                   GROUP 3d web)

# L6 -- gpuagents
eve_declare_module(NAME gpuagents_editing LAYER 6
                   DEPS editing gpuagents
                   GROUP 3d web)
