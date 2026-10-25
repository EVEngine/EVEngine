TILEMAP_TEST_BUILD ?= build/tilemap-editor-tests
TILEMAP_CMAKE_ARGS ?=
.PHONY: check/tilemap-editor
check/tilemap-editor:
	cmake -S test/tilemap_editor -B "$(TILEMAP_TEST_BUILD)" -DCMAKE_BUILD_TYPE=Debug $(TILEMAP_CMAKE_ARGS)
	cmake --build "$(TILEMAP_TEST_BUILD)" -j $(JOBS)
	ctest --test-dir "$(TILEMAP_TEST_BUILD)" --output-on-failure

