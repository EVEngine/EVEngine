"""Build the demo geometry-node converter's .blend inside Blender.

Run once with your Blender:
    blender --background --python modelconverter/scripts/build_nodegroup_blend.py

Creates `modelconverter/converters/gn_displace/gn_displace.blend` containing a
Geometry Node group named `EVE_Displace` (displaces a mesh along its normals by
a seeded noise field). The `gn_displace` converter appends and applies it.
"""

from __future__ import annotations

import os
import sys

import bpy


def make_displace_group(name: str = "EVE_Displace") -> None:
    if name in bpy.data.node_groups:
        bpy.data.node_groups.remove(bpy.data.node_groups[name])

    group = bpy.data.node_groups.new(name=name, type="GeometryNodeTree")
    group.is_modifier = True
    # Keep the group in the saved file even though no object references it yet.
    group.use_fake_user = True

    # --- node group interface (Blender 4.x) ---
    try:
        group.interface.new_socket(name="Geometry", in_out="INPUT",
                                   socket_type="NodeSocketGeometry")
        group.interface.new_socket(name="Strength", in_out="INPUT",
                                   socket_type="NodeSocketFloat")
        group.interface.new_socket(name="Seed", in_out="INPUT",
                                   socket_type="NodeSocketInt")
        group.interface.new_socket(name="Geometry", in_out="OUTPUT",
                                   socket_type="NodeSocketGeometry")
    except AttributeError:
        # Fallback for older Blender node-tree interface.
        in_geom = group.inputs.new("NodeSocketGeometry", "Geometry")
        in_strength = group.inputs.new("NodeSocketFloat", "Strength")
        in_seed = group.inputs.new("NodeSocketInt", "Seed")
        group.outputs.new("NodeSocketGeometry", "Geometry")
        in_strength.default_value = 0.4
        in_seed.default_value = 1

    nodes = group.nodes
    group_input = nodes.new("NodeGroupInput")
    group_output = nodes.new("NodeGroupOutput")
    group_input.location = (-800, 0)
    group_output.location = (600, 0)

    position = nodes.new("GeometryNodeInputPosition")
    position.location = (-500, 200)

    noise = nodes.new("ShaderNodeTexNoise")
    noise.location = (-500, -40)
    noise.inputs["Scale"].default_value = 3.0
    try:
        noise.seed = 1
    except AttributeError:
        pass

    # strength scalar = Strength * noise Fac
    mult = nodes.new("ShaderNodeMath")
    mult.operation = "MULTIPLY"
    mult.location = (-120, 20)

    # offset = position vector scaled by strength scalar
    scale_vec = nodes.new("ShaderNodeVectorMath")
    scale_vec.operation = "SCALE"
    scale_vec.location = (140, 0)

    set_pos = nodes.new("GeometryNodeSetPosition")
    set_pos.location = (380, 0)

    links = group.links
    # Use index-based sockets: group_input.outputs order follows the interface
    # (Geometry, Strength, Seed); node socket orders are stable in Blender 4.x/5.x.
    links.new(group_input.outputs[0], set_pos.inputs[0])                 # Geometry
    links.new(position.outputs[0], scale_vec.inputs[0])                  # Position -> Vector
    links.new(noise.outputs[0], mult.inputs[0])                          # noise Fac -> value
    links.new(group_input.outputs[1], mult.inputs[1])                    # Strength -> value
    links.new(mult.outputs[0], scale_vec.inputs[1])                      # value -> Scale
    links.new(scale_vec.outputs[0], set_pos.inputs[3])                   # Vector -> Offset
    links.new(group_input.outputs[2], noise.inputs[1])                   # Seed -> W
    links.new(set_pos.outputs[0], group_output.inputs[0])                # Geometry


def main() -> int:
    make_displace_group("EVE_Displace")

    here = os.path.dirname(os.path.abspath(__file__))
    converter_dir = os.path.abspath(
        os.path.join(here, os.pardir, "converters", "gn_displace"))
    os.makedirs(converter_dir, exist_ok=True)
    out = os.path.join(converter_dir, "gn_displace.blend")
    bpy.ops.wm.save_as_mainfile(filepath=out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
