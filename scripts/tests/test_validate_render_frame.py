import tempfile
import unittest
from pathlib import Path

from PIL import Image

from scripts.validate_render_frame import validate


class ValidateRenderFrameTest(unittest.TestCase):
    def test_accepts_varied_pixels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.png"
            image = Image.new("RGB", (64, 64))
            for y in range(64):
                for x in range(64):
                    image.putpixel((x, y), (x * 4, y * 4, (x + y) * 2))
            image.save(path)
            mean, deviation = validate(path)
            self.assertGreater(mean, 2)
            self.assertGreater(deviation, 3)

    def test_rejects_blank(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            for value in (0, 255):
                path = Path(directory) / f"blank-{value}.png"
                Image.new("RGB", (64, 64), (value, value, value)).save(path)
                with self.assertRaises(ValueError):
                    validate(path)


if __name__ == "__main__":
    unittest.main()
