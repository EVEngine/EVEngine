# ---------------------------------------------------------------------------
# Host package: agent
# ---------------------------------------------------------------------------

# L6 -- agent
eve_declare_module(NAME agent_tensor DIR agent/tensor LAYER 6
                   SCRIPT AgentTensor SLOT agentTensor
                     DEPS agent tensor gpgpu
                   GROUP 3d web)
