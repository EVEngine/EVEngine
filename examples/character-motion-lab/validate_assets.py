"""Offline provenance and glTF dependency validation; no third-party packages."""
from pathlib import Path
import hashlib
import json
import struct

root = Path(__file__).resolve().parent / "assets" / "quaternius"
manifest = json.loads((root / "sha256.json").read_text())
for name, expected in manifest.items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, name
for path in root.glob("*.gltf"):
    doc = json.loads(path.read_text())
    assert doc["skins"] and doc["meshes"]
    for ref in doc.get("buffers", []) + doc.get("images", []):
        assert (root / ref["uri"]).is_file(), ref["uri"]
    assert len(doc["skins"][0]["joints"]) == 65
blob = (root / "AnimationLibrary.glb").read_bytes()
assert blob[:4] == b"glTF"
size = struct.unpack_from("<I", blob, 12)[0]
doc = json.loads(blob[20:20 + size])
required = {"Idle_Loop", "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop",
            "Crouch_Fwd_Loop", "Dance_Loop", "Punch_Jab", "Sword_Attack"}
assert required <= {a["name"] for a in doc["animations"]}
assert len(doc["skins"][0]["joints"]) == 53
print(f"PASS: {len(manifest)} hashes, both character dependency closures, eight required actions")
