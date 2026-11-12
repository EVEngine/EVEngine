#!/usr/bin/env python3
"""Compile the opt-in physics field GPU kernels into checked-in SPIR-V arrays."""

from pathlib import Path
import shutil
import subprocess
import tempfile

from compile_graphics_water_shaders import spv_to_inc
import utf8_stdio


def main() -> None:
    shaders = Path(__file__).resolve().parents[1] / "src/modules/physics/field/gpu"
    compiler = shutil.which("glslangValidator")
    if not compiler:
        raise SystemExit("glslangValidator is required")
    with tempfile.TemporaryDirectory(prefix="eve-gpu-field-shaders-") as temporary:
        for stage in (
            "reduce", "final", "evaluate", "project", "copy_appearances", "morton_keys",
            "radix_local", "radix_groups", "radix_chunks", "radix_scatter",
        ):
            output = Path(temporary) / f"{stage}.spv"
            subprocess.run(
                [compiler, "-V", "-S", "comp", str(shaders / f"{stage}.comp"), "-o", str(output)],
                check=True,
            )
            spv_to_inc(output, f"gpu_field_{stage}_spv", shaders / f"{stage}_spv.inc")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
