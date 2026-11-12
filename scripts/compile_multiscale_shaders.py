#!/usr/bin/env python3
"""Compile the dedicated multiscale sphere reference shaders."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from compile_graphics_water_shaders import spv_to_inc
import utf8_stdio


def main() -> None:
    shaders = Path(__file__).resolve().parents[1] / "src/modules/graphics/multiscale/shaders"
    compiler = shutil.which("glslangValidator")
    if not compiler:
        raise SystemExit("glslangValidator is required")
    with tempfile.TemporaryDirectory(prefix="eve-multiscale-shaders-") as temporary:
        for program in ("sphere", "sphere_batch", "far_flux"):
            for stage in ("vert", "frag"):
                output = Path(temporary) / f"{program}.{stage}.spv"
                subprocess.run([compiler, "-V", str(shaders / f"{program}.{stage}"), "-o", str(output)], check=True)
                spv_to_inc(output, f"{program}_{stage}_spv", shaders / f"{program}_{stage}_spv.inc")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
