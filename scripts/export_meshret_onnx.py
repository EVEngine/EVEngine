#!/usr/bin/env python3
"""Export a MeshRet-compatible ONNX stub matching SmrOnnxRunner's I/O contract.

Inputs:
  source_rot6d [1,T,J,6]
  source_geom  [1,S,7]
  target_geom  [1,S,7]
  source_dmi   [1,T,P,10]
Output:
  target_rot6d [1,T,J,6]

The default graph is an identity passthrough (target_rot6d = source_rot6d) so
engine integration can be validated without MeshRet research weights.
Trained MeshRet checkpoints can be converted to this contract offline.
"""

from __future__ import annotations

import argparse
from pathlib import Path


def build_identity_onnx(path: Path, joints: int = 8, sensors: int = 16, pairs: int = 8, frames: int = 4) -> None:
    import numpy as np
    import onnx
    from onnx import TensorProto, helper, numpy_helper

    T, J, S, P = frames, joints, sensors, pairs
    source_rot = helper.make_tensor_value_info("source_rot6d", TensorProto.FLOAT, [1, T, J, 6])
    source_geom = helper.make_tensor_value_info("source_geom", TensorProto.FLOAT, [1, S, 7])
    target_geom = helper.make_tensor_value_info("target_geom", TensorProto.FLOAT, [1, S, 7])
    source_dmi = helper.make_tensor_value_info("source_dmi", TensorProto.FLOAT, [1, T, P, 10])
    target_rot = helper.make_tensor_value_info("target_rot6d", TensorProto.FLOAT, [1, T, J, 6])

    # Keep unused inputs live so loaders accept the full MeshRet feed set.
    zero = numpy_helper.from_array(np.zeros((1,), dtype=np.float32), name="zero")
    reduce_g = helper.make_node("ReduceSum", ["source_geom"], ["geom_sum"], keepdims=0)
    reduce_t = helper.make_node("ReduceSum", ["target_geom"], ["tgeom_sum"], keepdims=0)
    reduce_d = helper.make_node("ReduceSum", ["source_dmi"], ["dmi_sum"], keepdims=0)
    add_gt = helper.make_node("Add", ["geom_sum", "tgeom_sum"], ["geom_t"])
    add_all = helper.make_node("Add", ["geom_t", "dmi_sum"], ["side_sum"])
    mul = helper.make_node("Mul", ["side_sum", "zero"], ["side_zero"])
    # Broadcast side_zero into a [1,T,J,6] additive identity via Shape+Expand is heavy;
    # identity Identity node on source_rot6d is the contract output.
    identity = helper.make_node("Identity", ["source_rot6d"], ["target_rot6d"])

    graph = helper.make_graph(
        [reduce_g, reduce_t, reduce_d, add_gt, add_all, mul, identity],
        "meshret_identity",
        [source_rot, source_geom, target_geom, source_dmi],
        [target_rot],
        [zero],
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
    model.ir_version = 8
    onnx.checker.check_model(model)
    path.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(path))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, default=Path("build/meshret_identity.onnx"))
    parser.add_argument("--joints", type=int, default=8)
    parser.add_argument("--sensors", type=int, default=16)
    parser.add_argument("--pairs", type=int, default=8)
    parser.add_argument("--frames", type=int, default=4)
    args = parser.parse_args()
    build_identity_onnx(args.output, args.joints, args.sensors, args.pairs, args.frames)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
