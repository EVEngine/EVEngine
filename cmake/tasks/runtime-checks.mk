# Reusable runtime checks. These consume an existing build; no implicit rebuild.
# CI supplies xvfb-run where a display is required, locals use their own display.
RUNTIME_BUILD_DIR ?= build/$(PLATFORM)-debug
RUNTIME_EVE ?= $(CURDIR)/$(RUNTIME_BUILD_DIR)/src/engine/eve$(if $(filter win32,$(PLATFORM)),.exe)
SMOKE_EXAMPLES ?=
RESOURCE_EXPORT_DIR ?= build/resource-format-exports
RESOURCE_PYTHON ?= python3
HEADLESS_GPU_JOBS ?= 2

.PHONY: smoke/examples test/devlab test/headless-gpu check/resource-fixtures \
    check/resource-results check/resource-exports

# EVE may be supplied by an artifact consumer; leave automatic binary detection
# to the harness when it is absent. MIN_RUN_SECONDS/RUN_SECONDS remain supported.
smoke/examples:
	bash scripts/smoke_examples.sh $(SMOKE_EXAMPLES)

test/devlab:
	bash scripts/test_devlab.sh "$(RUNTIME_EVE)"

test/headless-gpu:
	ctest --test-dir "$(RUNTIME_BUILD_DIR)" --output-on-failure -j $(HEADLESS_GPU_JOBS) \
		-R '^bundle/(graphics_font|TextureSampler|gpgpu|tensor|virtualgeometry_gpu|graphics_headless)\.cpp$$'

check/resource-fixtures:
	python3 scripts/resource_format_suite.py --report build/resource-format-inventory.json

check/resource-results:
	python3 scripts/resource_format_suite.py --build-dir "$(RUNTIME_BUILD_DIR)" --report build/resource-format-report.json

check/resource-exports:
	$(RESOURCE_PYTHON) scripts/check_resource_format_exports.py "$(RESOURCE_EXPORT_DIR)" --report build/resource-format-export-report.json

# Linux artifact handoff; tar preserves the executable bit across Actions jobs.
SMOKE_RUNTIME_ARCHIVE ?= build/example-smoke-runtime.tar.gz
.PHONY: package/smoke-runtime
package/smoke-runtime:
	bash scripts/package_smoke_runtime.sh "$(RUNTIME_EVE)" "$(SMOKE_RUNTIME_ARCHIVE)"
