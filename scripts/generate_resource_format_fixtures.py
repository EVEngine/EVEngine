#!/usr/bin/env python3
"""Regenerate original, tiny fixtures using independent Pillow/FFmpeg encoders.

Generation dependencies are not needed to run the C++ tests. Checked-in hashes
pin the actual inputs; differing encoder versions may produce different bytes.
"""
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import wave
import math
import struct

from PIL import Image, __version__ as pillow_version

ROOT = Path(__file__).resolve().parent.parent / 'test/fixtures/resource_formats'


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    pixels = [(17 + x * 37, 23 + y * 71, 11 + x * 13 + y * 29, (x + y * 5) * 17)
              for y in range(3) for x in range(5)]
    rgba = Image.new('RGBA', (5, 3))
    rgba.putdata(pixels)
    rgb = rgba.convert('RGB')
    for ext in ['png', 'tga']:
        rgba.save(ROOT / ('pattern.' + ext))
    rgb.save(ROOT / 'pattern.bmp')
    rgb.save(ROOT / 'pattern.jpg', quality=100, subsampling=0)
    (ROOT / 'pattern.jpeg').write_bytes((ROOT / 'pattern.jpg').read_bytes())
    rgb.save(ROOT / 'pattern.gif', optimize=False)
    rgba.save(ROOT / 'pattern.webp', lossless=True, exact=True)
    rgb.save(ROOT / 'pattern.lossy.webp', quality=100)
    second = Image.new('RGB', (5, 3), (240, 10, 200))
    for ext in ['gif', 'webp']:
        rgb.save(ROOT / ('pattern.animated.' + ext), save_all=True, append_images=[second],
                 duration=[100, 100], loop=0, lossless=True)

    commands = []
    def ffmpeg(*args):
        cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', *args]
        subprocess.run(cmd, cwd=ROOT, check=True)
        commands.append(cmd)
    ffmpeg('-i', 'pattern.png', '-vf', 'setsar=1', '-frames:v', '1', '-pix_fmt', 'gbrpf32le', 'pattern.exr')
    # Radiance RGBE header and scanline pixels are emitted explicitly because
    # FFmpeg has no Radiance encoder. Width < 8 uses the non-RLE representation.
    rgbe = bytearray(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 3 +X 5\n')
    for r, g, b, _ in pixels:
        rgbe.extend((r, g, b, 128))
    (ROOT / 'pattern.hdr').write_bytes(rgbe)
    with wave.open(str(ROOT / 'tone.wav'), 'wb') as out:
        out.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
        out.writeframes(b''.join(struct.pack('<hh', round(12000 * math.sin(2 * math.pi * 440 * n / 44100)),
                                              round(6000 * math.sin(2 * math.pi * 880 * n / 44100))) for n in range(11025)))
    for ext, codec in [('flac', 'flac'), ('ogg', 'libvorbis'), ('mp3', 'libmp3lame')]:
        ffmpeg('-i', 'tone.wav', '-c:a', codec, 'tone.' + ext)
    triangle = [(0., 0., 0.), (2., 0., 0.), (0., 3., 0.)]
    (ROOT / 'triangle.obj').write_text('v 0 0 0\nv 2 0 0\nv 0 3 0\nf 1 2 3\n')
    (ROOT / 'triangle.stl').write_text('solid triangle\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 2 0 0\nvertex 0 3 0\nendloop\nendfacet\nendsolid triangle\n')
    (ROOT / 'triangle.ply').write_text('ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n2 0 0\n0 3 0\n3 0 1 2\n')
    (ROOT / 'triangle.off').write_text('OFF\n3 1 0\n0 0 0\n2 0 0\n0 3 0\n3 0 1 2\n')
    (ROOT / 'triangle.x').write_text('xof 0303txt 0032\nMesh triangle {\n3;\n0.0;0.0;0.0;,\n2.0;0.0;0.0;,\n0.0;3.0;0.0;;\n1;\n3;0,1,2;;\n}\n')
    model_bytes = b''.join(struct.pack('<fff', *v) for v in triangle)
    gltf = {'asset': {'version': '2.0'}, 'buffers': [{'byteLength': len(model_bytes)}],
            'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': len(model_bytes)}],
            'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3', 'min': [0, 0, 0], 'max': [2, 3, 0]}],
            'meshes': [{'primitives': [{'attributes': {'POSITION': 0}}]}],
            'nodes': [{'mesh': 0}], 'scenes': [{'nodes': [0]}], 'scene': 0}
    glb_json = json.dumps(gltf, separators=(',', ':')).encode()
    glb_json += b' ' * (-len(glb_json) % 4)
    glb = struct.pack('<III', 0x46546c67, 2, 28 + len(glb_json) + len(model_bytes))
    glb += struct.pack('<II', len(glb_json), 0x4e4f534a) + glb_json
    glb += struct.pack('<II', len(model_bytes), 0x004e4942) + model_bytes
    (ROOT / 'triangle.glb').write_bytes(glb)
    gltf['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(model_bytes).decode()
    (ROOT / 'triangle.gltf').write_text(json.dumps(gltf, indent=2) + '\n')
    (ROOT / 'triangle.dae').write_text('''<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
<asset><created>2026-09-10T00:00:00Z</created><modified>2026-09-10T00:00:00Z</modified><unit meter="1"/><up_axis>Y_UP</up_axis></asset>
<library_geometries><geometry id="triangle"><mesh>
<source id="positions"><float_array id="positions-array" count="9">0 0 0 2 0 0 0 3 0</float_array><technique_common><accessor source="#positions-array" count="3" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
<vertices id="vertices"><input semantic="POSITION" source="#positions"/></vertices>
<triangles count="1"><input semantic="VERTEX" source="#vertices" offset="0"/><p>0 1 2</p></triangles>
</mesh></geometry></library_geometries>
<library_visual_scenes><visual_scene id="scene"><node id="node"><instance_geometry url="#triangle"/></node></visual_scene></library_visual_scenes><scene><instance_visual_scene url="#scene"/></scene>
</COLLADA>
''')
    version = subprocess.run(['ffmpeg', '-version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    manifest = {'schema': 'evengine.resource-format-fixtures', 'version': 1,
                'license': 'CC0-1.0', 'source': 'Original synthetic pixels and PCM generated by this repository script',
                'generators': {'Pillow': pillow_version, 'FFmpeg': version}, 'commands': commands,
                'image': {'width': 5, 'height': 3, 'rgba8': pixels},
                'audio': {'sampleRate': 44100, 'channels': 2, 'sampleFrames': 11025, 'leftHz': 440, 'rightHz': 880},
                'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(ROOT.glob('*')) if p.suffix in ['.png', '.tga', '.bmp', '.jpg', '.jpeg', '.gif', '.webp', '.exr', '.hdr', '.wav', '.flac', '.ogg', '.mp3', '.obj', '.stl', '.ply', '.off', '.x', '.gltf', '.glb', '.dae']}}
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
