AGENT_TEST_BUILD ?= build/agent
AGENT_CMAKE_ARGS ?=
.PHONY: check/agent
check/agent:
	cmake -S test/agent -B "$(AGENT_TEST_BUILD)" -DCMAKE_BUILD_TYPE=Debug $(AGENT_CMAKE_ARGS)
	cmake --build "$(AGENT_TEST_BUILD)" -j $(JOBS)
	ctest --test-dir "$(AGENT_TEST_BUILD)" --output-on-failure

