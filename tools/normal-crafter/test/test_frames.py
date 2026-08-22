import os
import tempfile

import numpy as np

from normal_crafter.frames import (
    _frame_number,
    is_video,
    list_frame_paths,
    probe_sequence,
)


def _make_frame_dir(root, count=4, prefix="frame_%06d"):
    folder = os.path.join(root, "frames")
    os.makedirs(folder, exist_ok=True)
    try:
        from PIL import Image
    except Exception:
        import cv2  # noqa: F401

        Image = None

    for i in range(1, count + 1):
        path = os.path.join(folder, prefix % i + ".png")
        if Image is not None:
            Image.fromarray(
                (np.ones((8, 8, 3), dtype=np.uint8) * i * 40)
            ).save(path)
        else:
            with open(path, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n")
    return folder


def test_is_video():
    assert is_video("a.mp4")
    assert is_video("b.MOV")
    assert not is_video("frame_0001.png")
    assert not is_video("dir")


def test_list_frame_paths_sorted():
    with tempfile.TemporaryDirectory() as root:
        folder = _make_frame_dir(root)
        paths = list_frame_paths(folder)
        assert len(paths) == 4
        bases = [os.path.basename(p) for p in paths]
        assert bases == sorted(bases)
        assert "frame_000001.png" in bases


def test_frame_number():
    assert _frame_number("frame_0007.png") == "0007"
    assert _frame_number("shot12.png") == "12"


def test_probe_sequence():
    with tempfile.TemporaryDirectory() as root:
        folder = _make_frame_dir(root)
        count, fps, _shape = probe_sequence(folder)
        assert count == 4
        assert fps == 30
