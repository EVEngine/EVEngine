import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from generate_binding_contracts import (
    Contract,
    Parameter,
    SignatureIndex,
    extract_contracts,
    class_blocks,
    write_dts,
    write_json,
)


class ClassBlocksTests(unittest.TestCase):
    def test_export_macro_between_class_key_and_name_is_not_the_class_name(self) -> None:
        source = (
            "class EVENGINE_API_DOMAINS Widget {\n"
            "public:\n"
            "    void render(int width);\n"
            "};\n"
            "struct EVENGINE_API_FOUNDATION Gadget {\n"
            "    int size;\n"
            "};\n"
        )

        blocks = class_blocks(source)

        self.assertIn("Widget", blocks)
        self.assertIn("Gadget", blocks)
        # Regression: the macro used to be captured as the class name, which
        # reported every member binding of the class as unresolved.
        self.assertNotIn("EVENGINE_API_DOMAINS", blocks)
        self.assertNotIn("EVENGINE_API_FOUNDATION", blocks)

    def test_macro_free_declarations_still_parse(self) -> None:
        blocks = class_blocks("class Plain {\n    void run();\n};\n")

        self.assertIn("Plain", blocks)
        self.assertEqual(len(blocks["Plain"]), 1)


class SignatureIndexTests(unittest.TestCase):
    def test_free_function_prefers_named_parameters_independent_of_source_order(self) -> None:
        unnamed = (Path("unnamed.cpp"), "void bindingBridge(int, float);")
        named = (Path("named.cpp"), "void bindingBridge(int entityCount, float deltaTime);")

        for sources in (dict((unnamed, named)), dict((named, unnamed))):
            signature = SignatureIndex(sources).free("bindingBridge")
            self.assertIsNotNone(signature)
            self.assertEqual([parameter.name for parameter in signature[1]], ["entityCount", "deltaTime"])

    def test_member_signature_resolves_through_the_export_macro(self) -> None:
        source = (
            Path("widget.cpp"),
            "class EVENGINE_API_DOMAINS Widget {\n"
            "public:\n"
            "    void render(int pixelWidth);\n"
            "};\n",
        )

        signature = SignatureIndex(dict([source])).member("Widget", "render")

        self.assertIsNotNone(signature)
        self.assertEqual([parameter.name for parameter in signature[1]], ["pixelWidth"])

    def test_member_signature_resolves_nodiscard_inline_getters(self) -> None:
        source = (
            Path("effect.h"),
            "class ParticleEffect {\n"
            "public:\n"
            "    [[nodiscard]] float getTimelineSeconds() const { return 0.f; }\n"
            '    [[nodiscard("check me")]] bool isTimelinePlaying() const;\n'
            "};\n",
        )

        index = SignatureIndex(dict([source]))
        seconds = index.member("ParticleEffect", "getTimelineSeconds")
        playing = index.member("ParticleEffect", "isTimelinePlaying")

        self.assertIsNotNone(seconds)
        self.assertEqual(seconds[0], "float")
        self.assertEqual(seconds[1], [])
        self.assertIsNotNone(playing)
        self.assertEqual(playing[0], "bool")
        self.assertEqual(playing[1], [])

    def test_nodiscard_message_paren_is_not_the_parameter_list(self) -> None:
        """[[nodiscard("...")]] contains '('; that must not become arg0."""
        source = (
            Path("renderer.h"),
            "class PixelWorldGraphics {\n"
            "public:\n"
            '    [[nodiscard("retain and delete the returned PixelWorldAtlasRenderer")]]\n'
            "    PixelWorldAtlasRenderer* newRenderer(int originX, int originY, int width, int height);\n"
            "};\n",
        )

        signature = SignatureIndex(dict([source])).member("PixelWorldGraphics", "newRenderer")

        self.assertIsNotNone(signature)
        self.assertEqual(signature[0], "PixelWorldAtlasRenderer*")
        self.assertEqual(
            [parameter.name for parameter in signature[1]],
            ["originX", "originY", "width", "height"],
        )


class SharedBinderTests(unittest.TestCase):
    def test_template_binder_methods_follow_both_real_domain_classes(self):
        paths = [
            "src/modules/editor/presentation/WorkspacePresentationBindings.h",
            "src/modules/audio/editor/AudioEditorModule.cpp",
            "src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp",
        ]
        sources = {ROOT / path: (ROOT / path).read_text(encoding="utf-8") for path in paths}
        contracts, _ = extract_contracts(sources)
        keys = {contract.key: contract for contract in contracts}
        for module, cls in (("audioEditor", "AudioSourceEditor"), ("animationEditor", "AnimationClipEditor")):
            for method in ("presentationState", "presentationSnapshot", "presentationEdit",
                           "presentationCommit", "presentationCancel", "presentationAction", "presentationClose"):
                self.assertIn(f"{module}/{cls}.{method}", keys)
            edit = keys[f"{module}/{cls}.presentationEdit"]
            self.assertEqual(["path", "input", "staged"], [param.name for param in edit.parameters])
            self.assertEqual("Table<string, dynamic>", edit.return_type)
        self.assertNotIn("editor/presentationEdit", keys)

    def test_trimmed_consumer_does_not_publish_its_shared_methods(self):
        paths = [
            "src/modules/editor/presentation/WorkspacePresentationBindings.h",
            "src/modules/audio/editor/AudioEditorModule.cpp",
            "src/modules/animation/editor/AnimationClipEditorScriptBindings.cpp",
        ]
        sources = {ROOT / path: (ROOT / path).read_text(encoding="utf-8") for path in paths}
        contracts, _ = extract_contracts(sources, enabled_modules={"editor", "audio"})
        keys = {contract.key for contract in contracts}
        self.assertIn("audioEditor/AudioSourceEditor.presentationEdit", keys)
        self.assertNotIn("animationEditor/AnimationClipEditor.presentationEdit", keys)


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


class LambdaCaptureTests(unittest.TestCase):
    def test_getHandle_paren_inside_capture_is_not_the_parameter_list(self) -> None:
        """Regression: `[vm = cls.getHandle()](T* self, const std::string& id)` used to
        parse getHandle()'s empty argument list and publish zero parameters."""
        from generate_binding_contracts import callable_signature

        expression = (
            "[vm = cls.getHandle()](DialogueFlow* value, const std::string& id, ssq::Object bindings) {\n"
            "    return eve::script::projectResult(vm, value->startChecked(id, std::move(bindings)));\n"
            "}"
        )
        signature = callable_signature(expression, SignatureIndex({}))
        self.assertIsNotNone(signature)
        self.assertEqual([parameter.name for parameter in signature[2]], ["value", "id", "bindings"])


class BindMethodScrapeTests(unittest.TestCase):
    def test_factory_types_use_declared_script_class_aliases(self) -> None:
        from generate_binding_contracts import extract_contracts

        sources = {
            ROOT / "src/modules/particles/Particles.h": (
                "class ParticleEmitter {};\n"
                "class Particles { public: ParticleEmitter* newEmitter(ParticleEmitter* copy); ssq::Object state(); };\n"
            ),
            ROOT / "src/modules/particles/Particles.cpp": (
                "void bind(ssq::Table& table, ssq::Class& cls) {\n"
                'auto emitter = table.addClass<ParticleEmitter>("Emitter");\n'
                'auto object = table.addClass<ssq::Object>("ObjectProxy");\n'
                'cls.addFunc("newEmitter", &Particles::newEmitter);\n'
                'cls.addFunc("state", &Particles::state);\n'
                "}\n"
            ),
        }
        contracts, unresolved = extract_contracts(sources)
        self.assertEqual(unresolved, [])
        self.assertEqual(len(contracts), 2)
        self.assertEqual(contracts[0].return_type, "Emitter")
        self.assertEqual(contracts[0].parameters[0].script_type, "Emitter")
        self.assertTrue(contracts[0].return_nullable)
        self.assertEqual(contracts[0].parameters[0].cpp_type, "ParticleEmitter*")
        self.assertEqual(contracts[1].return_type, "dynamic")

        # Two script aliases provide no unique projection for the C++ type.
        sources[ROOT / "src/modules/particles/Other.cpp"] = (
            'auto other = table.addClass<ParticleEmitter>("OtherEmitter");\n'
        )
        contracts, unresolved = extract_contracts(sources)
        self.assertEqual(unresolved, [])
        self.assertEqual(contracts[0].return_type, "ParticleEmitter")

    def test_bind_method_literal_names_are_scraped(self) -> None:
        from generate_binding_contracts import extract_contracts

        root = Path(__file__).resolve().parents[2]
        sources = {
            root / "src/modules/animation/OrientationWarping.h": (
                "namespace eve::animation {\n"
                "class OrientationWarping {\n"
                "public:\n"
                "    void setEnabled(bool enabled);\n"
                "    bool isEnabled() const;\n"
                "};\n"
                "}\n"
            ),
            root / "src/modules/animation/OrientationWarpingBindings.cpp": (
                "namespace eve::animation {\n"
                "void expose(ssq::Table& table) {\n"
                "    auto cls = table.addClass<OrientationWarping>(\n"
                '        "OrientationWarping", std::function<OrientationWarping*()>([]() { return nullptr; }), true);\n'
                '    script::bindMethod(cls, "setEnabled", &OrientationWarping::setEnabled);\n'
                '    eve::script::bindPointerMethod(cls, "isEnabled", &OrientationWarping::isEnabled);\n'
                "}\n"
                "}\n"
            ),
        }

        contracts, unresolved = extract_contracts(sources)
        keys = {contract.key for contract in contracts}
        self.assertEqual(unresolved, [])
        self.assertIn("animation/OrientationWarping.setEnabled", keys)
        self.assertIn("animation/OrientationWarping.isEnabled", keys)


class SlotAwareKeyTests(unittest.TestCase):
    def test_module_reg_roots_use_manifest_slot(self) -> None:
        """addClass(name, …) leaves scriptClass empty; keys must still match SLOT."""
        from generate_binding_contracts import SlotIndex, extract_contracts

        root = Path(__file__).resolve().parents[2]
        sources = {
            root / "src/modules/dialogue/DialogueFlow.h": (
                "namespace eve::dialogue {\n"
                "class DialogueFlow {\n"
                "public:\n"
                "    eve::Result<void> publishGameplay(const std::string& instanceId,\n"
                "                                      const std::string& ownerId);\n"
                "};\n"
                "}\n"
            ),
            root / "src/modules/dialogue/DialogueFlow.cpp": (
                "namespace eve::dialogue {\n"
                "void DialogueFlow::expose(ssq::Table& table) {\n"
                "    auto cls = table.addClass(name, DialogueFlow::create, false);\n"
                '    cls.addFunc("publishGameplay",\n'
                "                [](DialogueFlow* self, const std::string& instanceId,\n"
                "                   const std::string& ownerId) {\n"
                "                    return self->publishGameplay(instanceId, ownerId);\n"
                "                });\n"
                "}\n"
                "}\n"
            ),
            root / "src/modules/map/level/editor/LevelEditorModule.h": (
                "namespace eve::level_editing { class TileBuffer { public: int width() const; }; }\n"
                "namespace eve::level_editor {\n"
                "class LevelEditorModule {\n"
                "public:\n"
                "    std::unique_ptr<level_editing::TileBuffer> newTileBuffer(int width, int height) const;\n"
                "};\n"
                "}\n"
            ),
            root / "src/modules/map/level/editor/LevelEditorModule.cpp": (
                "namespace eve::level_editor {\n"
                "void LevelEditorModule::expose(ssq::Table& table) {\n"
                "    auto buffer = table.addClass<level_editing::TileBuffer>(\n"
                '        "TileBuffer", std::function<level_editing::TileBuffer*()>([]() { return nullptr; }), true);\n'
                "    buffer.addFunc(\"getWidth\", &level_editing::TileBuffer::width);\n"
                "    auto module = table.addClass(name, LevelEditorModule::create, false);\n"
                "    expose(module);\n"
                "}\n"
                "void LevelEditorModule::expose(ssq::Class& cls) {\n"
                '    cls.addFunc("newTileBuffer", [](LevelEditorModule* self, int width, int height) {\n'
                "        return self->newTileBuffer(width, height).release();\n"
                "    });\n"
                "}\n"
                "}\n"
            ),
        }
        slots = SlotIndex(
            script_to_slot={
                "DialogueFlow": "dialogueFlow",
                "LevelEditorModule": "levelEditor",
            },
            dir_slots=(("map/level/editor", "levelEditor"),),
        )

        contracts, unresolved = extract_contracts(sources, slot_index=slots)
        keys = {contract.key for contract in contracts}
        self.assertEqual(unresolved, [])
        self.assertIn("dialogueFlow/publishGameplay", keys)
        self.assertNotIn("dialogue/publishGameplay", keys)
        self.assertIn("levelEditor/newTileBuffer", keys)
        self.assertIn("levelEditor/TileBuffer.getWidth", keys)
        self.assertNotIn("map/newTileBuffer", keys)


if __name__ == "__main__":
    unittest.main()
