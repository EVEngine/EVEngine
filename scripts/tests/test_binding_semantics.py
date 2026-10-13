import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from binding_semantics import attach_semantics
from generate_binding_contracts import Contract, Parameter, contract_to_dict, mask_comments, write_cpp


class BindingSemanticsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.directory = self.root / "src/modules/scene"
        self.directory.mkdir(parents=True)
        self.source = self.directory / "Scene.cpp"
        self.sources = {self.source: "Module_REG(Scene);"}
        self.rules = {"schemaVersion": 1, "factory": "Scene", "bindings": {
            "lookup": {"role": "scene_ref", "resultClass": "SceneNodeRef", "identityArgument": 0}}}
        self.contracts = [Contract("scene", "", "lookup", [Parameter("id", "std::string", "string")],
                                   "SceneNodeRef", False, "value", "main", [], "scene.lookup", "Scene.cpp:1"),
                          Contract("scene", "SceneNodeRef", "setVisible", [], "bool", False,
                                   "value", "main", [], "scene.ref.setVisible", "Scene.cpp:2")]

    def attach(self):
        path = self.directory / "binding_semantics.json"
        path.write_text(json.dumps(self.rules), encoding="utf-8")
        attach_semantics(self.contracts, self.sources, self.root, [path], mask_comments)

    def test_catalogue_and_native_generation_keep_semantic_schema(self):
        self.rules["bindings"]["lookup"]["schema"] = {
            "kind": "entity", "identityField": "id", "fields": {"visible": "boolean"}}
        self.attach()
        self.assertEqual(contract_to_dict(self.contracts[0])["semantics"]["schema"]["kind"], "entity")
        target = self.root / "generated.cpp"
        write_cpp(target, self.contracts)
        generated = target.read_text(encoding="utf-8")
        self.assertIn('BindingSemanticContract{"Scene", "scene_ref", "SceneNodeRef", 0, -1,', generated)
        self.assertIn('\\"visible\\":\\"boolean\\"', generated)

    def test_wrong_factory_and_role_rejected(self):
        self.rules["factory"] = "InventedFactory"
        with self.assertRaises(ValueError):
            self.attach()
        self.rules["factory"] = "Scene"
        self.rules["bindings"]["lookup"]["role"] = "invented"
        with self.assertRaises(ValueError):
            self.attach()

    def test_missing_method_argument_and_return_class_rejected(self):
        for rule in [{"role": "scene_ref", "identityArgument": 1},
                     {"role": "scene_ref", "resultClass": "InventedType"}]:
            self.rules["bindings"]["lookup"] = rule
            with self.assertRaises(ValueError):
                self.attach()
        self.rules["bindings"] = {"missing": {"role": "scene_ref"}}
        with self.assertRaises(ValueError):
            self.attach()

    def test_trimmed_module_never_adds_phantom_contracts(self):
        self.contracts = []
        self.attach()
        self.assertEqual(self.contracts, [])

    def test_invalid_schema_shape_rejected(self):
        self.rules["bindings"]["lookup"]["schema"] = {"kind": "entity", "identityField": "id", "fields": {"x": "invented"}}
        with self.assertRaises(ValueError):
            self.attach()
