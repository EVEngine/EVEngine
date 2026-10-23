# ---------------------------------------------------------------------------
# Host package: tensor
# ---------------------------------------------------------------------------

# L5 -- tensor
eve_declare_module(NAME tensor LAYER 5 LIB EVTensor SCRIPT TF SLOT tf
                   DEPS gpgpu
                   GROUP 3d web)
