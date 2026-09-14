"""Rebuild the authored, license-free VRM contract fixture (no external dependencies)."""
import json
from pathlib import Path
import struct

root = Path(__file__).parent
schema = json.loads((root / "contract.gltf.json").read_text(encoding="utf-8"))
chunks = [
    struct.pack("<9f", -.3,0,0, .3,0,0, 0,.6,0),
    struct.pack("<9f", *([0,0,1]*3)),
    struct.pack("<6f", 0,1,1,1,.5,0),
    struct.pack("<3H", 0,1,2),
    struct.pack("<12H", *([0]*12)),
    struct.pack("<12f", *([1,0,0,0]*3)),
    struct.pack("<9f", *([0,0,.1]*3)),
    struct.pack("<16f", 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1),
]
data = bytearray()
for view, chunk in zip(schema["bufferViews"], chunks):
    data.extend(bytes((-len(data)) % 4))
    assert view["byteOffset"] == len(data)
    assert view["byteLength"] == len(chunk)
    data.extend(chunk)
assert schema["buffers"][0]["byteLength"] == len(data)
text = json.dumps(schema, separators=(",", ":")).encode()
text += b" " * ((-len(text)) % 4)
result = struct.pack("<III", 0x46546C67, 2, 28 + len(text) + len(data))
result += struct.pack("<II", len(text), 0x4E4F534A) + text
result += struct.pack("<II", len(data), 0x004E4942) + data
(root / "contract.vrm").write_bytes(result)

# The runnable example shares this deterministic fixture; no private model is required.
example = root.parents[2] / "examples/vrm-avatar/assets/contract.vrm"
example.parent.mkdir(parents=True, exist_ok=True)
example.write_bytes(result)
