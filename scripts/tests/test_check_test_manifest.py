import unittest

from scripts.check_test_manifest import discovery_contract_errors


VALID_CMAKE = """
file(GLOB all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/test_src.txt" "")
list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")
if(EVENGINE_BUILD_DEMO)
  list(APPEND all_test_cpp demo.cpp)
endif()
"""

VALID_MAKEFILE = """
	for cpp in test/*.cpp; do
	  if [ "$$cpp" -nt build/win32-debug/test/test_src.txt ]; then stale=1; fi
	done
"""


class CheckTestManifestTest(unittest.TestCase):
    def test_accepts_src_list_discovery(self):
        self.assertEqual(discovery_contract_errors(VALID_CMAKE, VALID_MAKEFILE), [])

    def test_rejects_configure_depends_glob(self):
        invalid = VALID_CMAKE.replace(
            'file(GLOB all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")',
            'file(GLOB all_test_cpp CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")',
        )
        errors = discovery_contract_errors(invalid, VALID_MAKEFILE)
        self.assertTrue(any("CONFIGURE_DEPENDS" in error for error in errors))

    def test_rejects_glob_without_stale_makefile_check(self):
        errors = discovery_contract_errors(VALID_CMAKE, "")
        self.assertTrue(any("test_src.txt" in error for error in errors))

    def test_requires_demo_to_remain_conditional(self):
        invalid = VALID_CMAKE.replace(
            'list(REMOVE_ITEM all_test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp")',
            "",
        )
        errors = discovery_contract_errors(invalid, VALID_MAKEFILE)
        self.assertTrue(any("removed" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
