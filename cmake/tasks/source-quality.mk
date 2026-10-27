# Canonical source-quality command inventory. The Python runner queries this
# list and invokes these targets; it owns only bounded scheduling and logs.
SOURCE_QUALITY_JOBS ?= 4
export SOURCE_QUALITY_JOBS
SOURCE_QUALITY_TARGETS := check/depgraph check/bindings check/test-manifest \
    check/test-case-lines check/test-case-names check/test-assert-level \
    check/sq-call-sites check/examples check/nodiscard check/adhoc-result-tables \
    check/versions check/quality-metadata check/profile-matrix \
    check/architecture-lint check/scripts

.PHONY: $(SOURCE_QUALITY_TARGETS) check/source-targets check/architecture-lint
check/source-targets:
	@$(SH) printf '%s\n' $(SOURCE_QUALITY_TARGETS)

# Lint only: the architecture unit tests also belong to check/scripts-test.
check/architecture-lint:
	python3 scripts/check_architecture_contracts.py --base "$(or $(ARCHITECTURE_BASE),$(CI_BASE))"
