import unittest
from pathlib import Path

from scripts.generate_binding_contracts import SignatureIndex


class SignatureIndexTests(unittest.TestCase):
    def test_free_function_prefers_named_parameters_independent_of_source_order(self) -> None:
        unnamed = (Path("unnamed.cpp"), "void bindingBridge(int, float);")
        named = (Path("named.cpp"), "void bindingBridge(int entityCount, float deltaTime);")

        for sources in (dict((unnamed, named)), dict((named, unnamed))):
            signature = SignatureIndex(sources).free("bindingBridge")
            self.assertIsNotNone(signature)
            self.assertEqual([parameter.name for parameter in signature[1]], ["entityCount", "deltaTime"])


if __name__ == "__main__":
    unittest.main()
