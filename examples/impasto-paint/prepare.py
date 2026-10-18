"""Regenerate demo SPIR-V/parameter exports from the canonical impasto shaders."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def main():
    for stage in ("vert", "frag"):
        subprocess.run(["glslc", f"-fshader-stage={stage}",
                        str(ROOT / f"src/modules/stylize/shaders/impasto_mesh.{stage}"),
                        "-o", str(HERE / f"assets/impasto.{stage}.spv")], check=True)
    source = (ROOT / "src/modules/stylize/StyleShaders.cpp").read_text(encoding="utf-8")
    table = source.split("kImpastoParams[] = {", 1)[1].split("};", 1)[0]
    rows = re.findall(r'\{"([^"]+)",\s*([-0-9.]+)f,', table)
    if len(rows) != 32:
        raise RuntimeError("Expected the complete 32-float impasto ABI")
    text = "// Generated from kImpastoParams in StyleShaders.cpp; run prepare.py.\nimpastoParameters <- [\n"
    text += "".join(f'    ["{name}",{value}],\n' for name, value in rows)
    (HERE / "parameters.nut").write_text(text + "];\n", encoding="utf-8")

if __name__ == "__main__":
    main()
