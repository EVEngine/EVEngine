import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from generate_binding_contracts import Contract, Parameter, SignatureIndex, write_dts, write_json


class SignatureIndexTests(unittest.TestCase):
    def test_free_function_prefers_named_parameters_independent_of_source_order(self) -> None:
        unnamed = (Path("unnamed.cpp"), "void bindingBridge(int, float);")
        named = (Path("named.cpp"), "void bindingBridge(int entityCount, float deltaTime);")

        for sources in (dict((unnamed, named)), dict((named, unnamed))):
            signature = SignatureIndex(sources).free("bindingBridge")
            self.assertIsNotNone(signature)
            self.assertEqual([parameter.name for parameter in signature[1]], ["entityCount", "deltaTime"])


class CatalogExportTests(unittest.TestCase):
    def test_json_and_dts_export_round_trip_fields(self) -> None:
        contract = Contract(
            module="graphics",
            script_class="Graphics",
            method="drawSolidRect",
            parameters=[
                Parameter("x", "float", "float"),
                Parameter("y", "float", "float"),
            ],
            return_type="void",
            return_nullable=False,
            ownership="value",
            thread_affinity="main",
            platforms=["linux"],
            documentation_id="graphics.Graphics.drawSolidRect",
            source="src/modules/graphics/Graphics.cpp:1",
        )
        with tempfile.TemporaryDirectory() as directory:
            json_path = Path(directory) / "eve-api.json"
            dts_path = Path(directory) / "eve-api.d.ts"
            write_json(json_path, [contract])
            write_dts(dts_path, [contract])
            payload = json_path.read_text(encoding="utf-8")
            self.assertIn('"schema": "eve.binding-api"', payload)
            self.assertIn('"key": "graphics/Graphics.drawSolidRect"', payload)
            self.assertIn("drawSolidRect(x: float, y: float): void;", dts_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
