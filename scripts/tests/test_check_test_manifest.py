import unittest

from scripts.check_test_manifest import discovery_contract_errors


VALID = """
file(GLOB all_test_cpp CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")
if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()
"""


class CheckTestManifestTest(unittest.TestCase):
    def test_accepts_configure_depends_discovery(self):
        self.assertEqual(discovery_contract_errors(VALID), [])

    def test_rejects_glob_without_configure_depends(self):
        invalid = VALID.replace(" CONFIGURE_DEPENDS", "")
        errors = discovery_contract_errors(invalid)
        self.assertTrue(any("CONFIGURE_DEPENDS" in error for error in errors))

    def test_requires_demo_to_remain_conditional(self):
        invalid = VALID.replace(
            'list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")',
            "",
        )
        errors = discovery_contract_errors(invalid)
        self.assertTrue(any("removed" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
