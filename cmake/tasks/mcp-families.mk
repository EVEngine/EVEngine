# Native provider composition contract. Use MCP_SHARED=ON/OFF and
# MCP_WITH_PROVIDER=ON/OFF through MCP_CMAKE_ARGS for both linkage/trim routes.
MCP_TEST_BUILD ?= build/mcp-tool-families
MCP_DEPENDENCIES ?= $(CURDIR)/build/third-party-binary/$(PLATFORM)-debug
MCP_CMAKE_ARGS ?=
MCP_WITH_COMPILER = $(if $(filter win32,$(PLATFORM)),$(WITH_MSVC))
.PHONY: check/mcp-families
check/mcp-families:
	$(MCP_WITH_COMPILER) cmake -S test/mcp_tool_families -B "$(MCP_TEST_BUILD)" -DCMAKE_BUILD_TYPE=Debug -DEVE_DEPENDENCIES="$(MCP_DEPENDENCIES)" $(MCP_CMAKE_ARGS)
	$(MCP_WITH_COMPILER) cmake --build "$(MCP_TEST_BUILD)" -j $(JOBS)
	ctest --test-dir "$(MCP_TEST_BUILD)" --output-on-failure
