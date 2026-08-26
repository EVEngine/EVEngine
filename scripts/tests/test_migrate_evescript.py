from pathlib import Path
import importlib.util
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("migrate_evescript", ROOT / "scripts" / "migrate_evescript.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def migrate(source: str) -> str:
    return MODULE.migrate_source(source)[0]


class MigrateEveScriptTests(unittest.TestCase):
    def test_migrates_root_guard_with_nested_initializer(self):
        source = 'if (!("state" in getroottable())) state <- { items = [1, 2], label = ";" };\n'
        self.assertEqual(migrate(source), 'persist state = { items = [1, 2], label = ";" }\n')

    def test_migrates_multiline_guard(self):
        source = 'if (!("world" in getroottable()))\n    world <- make_world(\n        0.0, 980.0);\n'
        self.assertEqual(migrate(source), 'persist world = make_world(\n        0.0, 980.0)\n')

    def test_migrates_legacy_persist_call(self):
        source = '''game <- persist("game", function() {
    return {
        score = 0,
        title = "ready; go"
    };
});
'''
        self.assertEqual(migrate(source), '''persist game = {
        score = 0,
        title = "ready; go"
    }
''')

    def test_does_not_migrate_nested_dynamic_guard(self):
        source = '''function update() {
    if (!("yaw" in getroottable())) yaw <- 0.0;
}
'''
        self.assertEqual(migrate(source), source)

    def test_does_not_touch_capability_check(self):
        source = 'if ("gfx" in getroottable()) gfx.clear();\n'
        self.assertEqual(migrate(source), source)


if __name__ == "__main__":
    unittest.main()
