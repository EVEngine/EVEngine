import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "tile_pipeline.py"
SPEC = importlib.util.spec_from_file_location("tile_pipeline", MODULE_PATH)
tile_pipeline = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = tile_pipeline
SPEC.loader.exec_module(tile_pipeline)


class TilePipelineTests(unittest.TestCase):
    def test_pipeline_trims_packs_and_emits_runtime_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            source.mkdir()
            pixels = bytearray(8 * 8 * 4)
            for y in range(2, 7):
                for x in range(1, 6):
                    pixels[(y * 8 + x) * 4:(y * 8 + x) * 4 + 4] = b"\xff\x80\x20\xff"
            tile_pipeline.write_png(source / "tile_0.png", 8, 8, bytes(pixels))
            tile_pipeline.write_png(source / "tile_1.png", 8, 8, bytes(pixels))
            config = {
                "source": "source",
                "output": "generated",
                "pipeline": [
                    {"use": "scan", "options": {"gidRegex": "tile_(\\d+)", "firstGid": 1}},
                    {"use": "analyze", "options": {"pivot": [0.5, 0.75]}},
                    {"use": "pack", "options": {"maxWidth": 32, "padding": 1}},
                    {"use": "emit", "options": {"runtimeImage": "assets/tiles.atlas.png"}},
                ],
                "overrides": {"tile_1": {"walkable": False, "footprint": [2, 1]}},
            }
            config_path = root / "pipeline.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            context = tile_pipeline.run(config_path)
            manifest = json.loads(context.artifacts["manifest"].read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema"], "eve.tileset/1")
            self.assertEqual(manifest["tileset"]["image"], "assets/tiles.atlas.png")
            self.assertEqual(manifest["tileset"]["tiles"][0]["region"][2:], [5, 5])
            self.assertFalse(manifest["tileset"]["tiles"][1]["walkable"])
            self.assertEqual(manifest["tileset"]["tiles"][1]["footprint"], [2, 1])

    def test_project_plugin_can_add_a_stage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source"
            source.mkdir()
            tile_pipeline.write_png(source / "wall_3.png", 1, 1, b"\xff\xff\xff\xff")
            plugin = root / "plugin.py"
            plugin.write_text(
                "def register(registry):\n"
                "  def mark(context, options):\n"
                "    context.tiles[0].metadata['walkable'] = False\n"
                "  registry.register('project.mark', mark)\n",
                encoding="utf-8",
            )
            config = {
                "source": "source",
                "output": "out",
                "plugins": ["plugin.py"],
                "pipeline": ["scan", "analyze", "project.mark", "pack", "emit"],
            }
            config_path = root / "pipeline.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            context = tile_pipeline.run(config_path)
            manifest = json.loads(context.artifacts["manifest"].read_text(encoding="utf-8"))
            self.assertFalse(manifest["tileset"]["tiles"][0]["walkable"])


if __name__ == "__main__":
    unittest.main()
