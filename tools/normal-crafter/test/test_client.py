import io
import os
import tempfile
import zipfile

import numpy as np
import pytest

from normal_crafter.client import NormalCrafterClient, _pngs_to_video, unpack_normal_zip


def _fake_normal_zip() -> bytes:
    bio = io.BytesIO()
    with zipfile.ZipFile(bio, "w", zipfile.ZIP_STORED) as zf:
        for i in range(3):
            arr = (np.ones((16, 16, 3), dtype=np.uint8) * i * 80)
            ok, buf = _imencode(arr)
            zf.writestr(f"frame_{i + 1:06d}.png", buf.tobytes())
    return bio.getvalue()


def _imencode(arr):
    try:
        import cv2
        ok, buf = cv2.imencode(".png", arr)
        return ok, buf
    except Exception:
        from PIL import Image
        buf = io.BytesIO()
        Image.fromarray(arr).save(buf, format="PNG")
        return True, buf.getvalue()


def test_unpack_normal_zip():
    with tempfile.TemporaryDirectory() as root:
        out = os.path.join(root, "normals")
        n = unpack_normal_zip(_fake_normal_zip(), out)
        assert n == 3
        files = sorted(os.listdir(out))
        assert files == ["frame_000001.png", "frame_000002.png", "frame_000003.png"]
        for f in files:
            assert os.path.getsize(os.path.join(out, f)) > 0


def test_health_unreachable():
    client = NormalCrafterClient("http://127.0.0.1:1")
    assert client.health() is False


def test_pngs_to_video():
    with tempfile.TemporaryDirectory() as root:
        folder = os.path.join(root, "normals")
        os.makedirs(folder)
        from PIL import Image
        for i in range(1, 4):
            Image.fromarray(np.ones((16, 16, 3), dtype=np.uint8) * i * 60
                            ).save(os.path.join(folder, f"frame_{i:06d}.png"))
        mp4 = os.path.join(root, "n.mp4")
        _pngs_to_video(folder, mp4, 15)
        assert os.path.exists(mp4)
        assert os.path.getsize(mp4) > 0


def test_run_client_empty_dir_raises():
    from normal_crafter.client import run_client
    with tempfile.TemporaryDirectory() as root:
        p = os.path.join(root, "frames")
        os.makedirs(p)
        with pytest.raises(ValueError):
            run_client("http://127.0.0.1:1", p, os.path.join(root, "out"))
