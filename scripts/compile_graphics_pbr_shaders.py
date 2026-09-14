"""Compile extended PBR vertex/fragment shaders and their embedded SPIR-V arrays."""
import subprocess
from compile_graphics_water_shaders import SHADER_DIR, spv_to_inc
for stage in ('vert','frag'):
    source=SHADER_DIR / f'pbr_surface.{stage}'
    binary=source.with_suffix(source.suffix+'.spv')
    subprocess.run(['glslc',str(source),'-o',str(binary)],check=True)
    spv_to_inc(binary,f'pbr_surface_{stage}_spv',SHADER_DIR / f'pbr_surface_{stage}_spv.inc')
