import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from binding_architecture import attach_architecture
from generate_binding_contracts import contract_to_dict, extract_contracts, mask_comments, write_cpp


class BindingArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.sources = {}
        self.add("src/modules/scene/Scene.cpp", '''
            class SceneNodeRef { public: bool setVisible(bool value); };
            auto refCls = table.addClass<SceneNodeRef>("SceneNodeRef");
            refCls.addFunc("setVisible", &SceneNodeRef::setVisible);
            refCls.addFunc("visibilityAlias", &SceneNodeRef::setVisible);
            refCls.addFunc("dynamicVisibility", [](SceneNodeRef* self, bool value) {
                return self->setVisible(value);
            });
        ''')
        self.add("src/modules/scene/SceneNodeRef.cpp", "bool SceneNodeRef::setVisible(bool value) { return value; }")
        self.add("src/modules/scene/SceneHost.h", "class SceneHost {}; struct SceneNode { bool visible = true; };")
        self.add("test/scene.cpp", 'TEST_CASE("Scene.visibility") {}')
        self.entry = {
            "module": "scene", "symbol": "SceneNodeRef::setVisible",
            "implementationFile": "src/modules/scene/SceneNodeRef.cpp",
            "owner": "SceneHost", "ownerFile": "src/modules/scene/SceneHost.h",
            "ownerAnchor": "class SceneHost", "stateAnchor": "bool visible = true;",
            "writes": ["SceneNode.visible"],
            "tests": [{"file": "test/scene.cpp", "name": "Scene.visibility"}],
            "limitations": 'reviewed "owner"',
        }

    def add(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        self.sources[path] = text

    def extract(self, modules=None):
        with patch("generate_binding_contracts.ROOT", self.root):
            contracts, unresolved = extract_contracts(self.sources, modules, "win32")
        self.assertEqual(unresolved, [])
        return contracts

    def attach(self, contracts, modules=None):
        path = self.root / "metadata.json"
        path.write_text(json.dumps({"schema": "eve.binding-architecture-metadata", "schemaVersion": 1,
                                    "entries": [self.entry]}), encoding="utf-8")
        attach_architecture(contracts, self.sources, self.root, [path], mask_comments, modules)

    def test_alias_maps_actual_target_but_lambda_is_unknown(self):
        contracts = self.extract()
        self.attach(contracts)
        by_name = {item.method: contract_to_dict(item) for item in contracts}
        for name in ("setVisible", "visibilityAlias"):
            mapping = by_name[name]["architecture"]
            self.assertEqual(mapping["cppSymbol"], "SceneNodeRef::setVisible")
            self.assertEqual(mapping["stateOwner"], "SceneHost")
            self.assertEqual(mapping["tests"], ["test/scene.cpp#Scene.visibility"])
            self.assertEqual(len(mapping["inputHash"]), 64)
        self.assertEqual(by_name["dynamicVisibility"]["architecture"]["status"], "unknown")
        self.assertNotEqual(by_name["setVisible"]["source"], by_name["visibilityAlias"]["source"])
        output = self.root / "generated.cpp"
        write_cpp(output, contracts)
        self.assertIn("BindingArchitectureContract{", output.read_text(encoding="utf-8"))
        self.assertIn('reviewed \\"owner\\"', output.read_text(encoding="utf-8"))

    def test_trimmed_module_is_absent_and_does_not_claim_a_mapping(self):
        contracts = self.extract(set())
        self.attach(contracts, set())
        self.assertEqual(contracts, [])

    def test_removed_binding_in_enabled_module_is_rejected(self):
        self.add("src/modules/scene/Scene.cpp", "class SceneNodeRef {};")
        with self.assertRaisesRegex(ValueError, "No enabled direct binding"):
            self.attach(self.extract())

    def test_comment_is_not_an_implementation(self):
        self.add("src/modules/scene/SceneNodeRef.cpp", "// bool SceneNodeRef::setVisible(bool value) { return value; }")
        with self.assertRaisesRegex(ValueError, "Expected one implementation"):
            self.attach(self.extract())

    def test_commented_test_is_rejected(self):
        self.add("test/scene.cpp", '// TEST_CASE("Scene.visibility") {}')
        with self.assertRaisesRegex(ValueError, "Missing test"):
            self.attach(self.extract())

    def test_missing_state_and_escaped_evidence_are_rejected(self):
        self.entry["stateAnchor"] = "not a field"
        with self.assertRaisesRegex(ValueError, "Missing owner/state"):
            self.attach(self.extract())
        self.entry["implementationFile"] = "../outside.cpp"
        with self.assertRaisesRegex(ValueError, "escapes repository"):
            self.attach(self.extract())

    def test_evidence_change_invalidates_hash_and_output_is_repeatable(self):
        contracts = self.extract()
        self.attach(contracts)
        before = [contract_to_dict(item) for item in contracts]
        self.attach(contracts)
        self.assertEqual(before, [contract_to_dict(item) for item in contracts])
        self.add("src/modules/scene/SceneNodeRef.cpp", "bool SceneNodeRef::setVisible(bool value) { return !value; }")
        self.attach(contracts)
        self.assertNotEqual(before[1]["architecture"]["inputHash"], contracts[1].architecture["inputHash"])


    def test_same_cpp_spelling_in_other_module_does_not_inherit_owner(self):
        from dataclasses import replace
        contracts = self.extract()
        original = next(contract for contract in contracts if contract.method == "setVisible")
        other = replace(original, module="other")
        contracts.append(other)
        self.attach(contracts)
        self.assertIsNone(other.architecture)
        self.assertIsNotNone(original.architecture)

    def test_reviewed_reads_and_writes_remain_distinct(self):
        self.entry["reads"] = ["SceneNode.other"]
        contracts = self.extract()
        self.attach(contracts)
        mapping = next(contract for contract in contracts if contract.method == "setVisible").architecture
        self.assertEqual(mapping["reads"], ["SceneNode.other"])
        self.assertEqual(mapping["writes"], ["SceneNode.visible"])


if __name__ == "__main__":
    unittest.main()
