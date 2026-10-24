# Windows GNU Make (Chocolatey Win32 port) defaults to cmd.exe. Point SHELL at
# Git Bash so POSIX recipes work from PowerShell/cmd. Use bash.exe (not WSL's
# System32\bash.exe). Prepend only Git\bin — Git\usr\bin has GNU link.exe and
# would shadow MSVC's linker if it stays at the front of PATH.
ifeq ($(OS),Windows_NT)
	ifneq ($(wildcard C:/Program\ Files/Git/bin/bash.exe),)
		export PATH := C:\Program Files\Git\bin;$(PATH)
		SHELL := C:/Program Files/Git/bin/bash.exe
	else ifneq ($(wildcard C:/Program\ Files\ \(x86\)/Git/bin/bash.exe),)
		export PATH := C:\Program Files (x86)\Git\bin;$(PATH)
		SHELL := C:/Program Files (x86)/Git/bin/bash.exe
	else
		$(error Git Bash not found (bin/bash.exe). Install Git for Windows from https://git-scm.com/download/win — this Makefile cannot run under cmd.exe.)
	endif
	.SHELLFLAGS := -c
	# Win32 GNU Make CreateProcess()es recipes with no |&; even when SHELL is
	# bash (mkdir.exe is not on PATH). Prefix simple POSIX commands with this.
	SH = : &&
endif
SH ?=

INSIDE_DOCKER=$(shell [ -f /.dockerenv ] && echo 1 || echo 0 )
PWD = $(shell pwd)

ifeq ($(INSIDE_DOCKER), 0)
	DOCKER = docker run -it --rm --volume="$(PWD):/home/evengine/src" evengine 
	DOCKER_START = docker run -it --volume="$(PWD):/home/evengine/src" evengine 
else
	DOCKER = 
endif


ifeq ($(OS),Windows_NT)
	PLATFORM = win32
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		PLATFORM = macosx
	else
		PLATFORM = linux
	endif
endif

# Android SDK/NDK defaults differ by host; fall back to any installed NDK.
ifeq ($(PLATFORM),macosx)
	ANDROID_SDK ?= $(HOME)/Library/Android/sdk
else ifeq ($(PLATFORM),win32)
	ANDROID_SDK ?= $(LOCALAPPDATA)/Android/Sdk
else
	ANDROID_SDK ?= $(HOME)/Android/Sdk
endif
ANDROID_NDK ?= $(ANDROID_SDK)/ndk/27.3.13750724
ifeq ($(wildcard $(ANDROID_NDK)/build/cmake/android.toolchain.cmake),)
	ANDROID_NDK := $(shell ls -d "$(ANDROID_SDK)/ndk"/* 2>/dev/null | sort -V | tail -1)
endif
ANDROID_ABI ?= arm64-v8a
ANDROID_PLATFORM ?= android-24
ANDROID_STL ?= c++_shared
APK_DIR = platform/android/apk
JNI_LIBS = $(APK_DIR)/app/src/main/jniLibs/$(ANDROID_ABI)
# Packaged squirrel game directory inside the APK (filled by sync/android-assets).
ANDROID_ASSETS_GAME = $(APK_DIR)/app/src/main/assets/game
# demo  = platform/android/game-shell (no main.nut → embedded eve.demoScript)
# example = copy from example/
ANDROID_GAME ?= demo
ifeq ($(PLATFORM),macosx)
	JAVA_HOME ?= $(shell brew --prefix openjdk@17 2>/dev/null)/libexec/openjdk.jdk/Contents/Home
endif
BUILD_DIR ?= build/android-debug

# iOS / iPadOS (iphoneos arm64, min 13.0). Requires full Xcode (not just CLT).
IOS_DEPLOYMENT_TARGET ?= 13.0
IOS_ARCH ?= arm64
IOS_SDK ?= iphoneos
# Team ID is the certificate subject OU; the value in the identity name's
# parentheses is the certificate ID and Xcode rejects it as a team.
IOS_DEVELOPMENT_TEAM ?= $(shell security find-certificate -a -c "Apple Development" -p 2>/dev/null | openssl x509 -noout -subject 2>/dev/null | sed -n 's/.*OU *= *\([A-Z0-9]*\).*/\1/p' | head -1)
IOS_BUNDLE_ID ?= com.evengine.example
IOS_TEST_BUNDLE_ID ?= com.evengine.example.test
# demo  = platform/ios/game-shell (no main.nut → embedded eve.demoScript + particles)
# example = copy from example/ (box + fire emitter)
IOS_GAME ?= demo
VULKAN_SDK ?= $(shell ls -d $(HOME)/VulkanSDK/*/ 2>/dev/null | sort -V | tail -1 | sed 's:/*$$::')
IOS_APP ?= build/ios-debug/src/engine/Debug-iphoneos/eve.app
ifeq ($(wildcard $(IOS_APP)),)
IOS_APP = build/ios-debug/src/engine/eve.app
endif
IOS_TEST_APP ?= build/ios-debug-test/src/engine/Debug-iphoneos/eve.app
IOS_SIM_TEST_APP ?= build/ios-sim-debug-test/src/engine/Debug-iphonesimulator/eve.app

# ---- Capability detection for `make all` (host + optional cross targets) ----
# debug/release remain host-only; all builds every available debug target.
HAS_IOS := 0
HAS_ANDROID := 0
HAS_WSL := 0

ifeq ($(PLATFORM),macosx)
	HAS_IOS := $(shell xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 && echo 1 || echo 0)
endif

ifneq ($(wildcard $(ANDROID_NDK)/build/cmake/android.toolchain.cmake),)
	HAS_ANDROID := $(shell command -v ninja >/dev/null 2>&1 && test -f "$(APK_DIR)/gradlew" && echo 1 || echo 0)
endif

ifeq ($(PLATFORM),win32)
	# Deferred (recursive) and VM-free: only expanded when ALL_DEBUG_TARGETS is
	# actually used (make all / show-targets), and `wsl -l -q` lists distros
	# without booting the WSL VM (`wsl -e true` cold-starts it, costing seconds
	# on every make invocation -- including `make run`).
	HAS_WSL = $(shell wsl -l -q >/dev/null 2>&1 && echo 1 || echo 0)
endif

ALL_DEBUG_TARGETS = build/$(PLATFORM)-debug \
	$(if $(filter 1,$(HAS_IOS)),build/ios-debug) \
	$(if $(filter 1,$(HAS_ANDROID)),build/android-debug) \
	$(if $(filter 1,$(HAS_WSL)),wsl/linux-debug)

# Desktop game directory for run/xxx. Empty (default) = run eve with no args,
# no cd, so the current directory has no main.nut and eve falls back to the
# embedded release demo. Set GAME=example (or any path) to run that game instead.
GAME ?=

.PHONY: all build/win32 build/linux build/macosx build/android build/ios \
	build/win32-debug build/linux-debug build/macosx-debug build/android-debug build/ios-debug \
	build/linux-asan build/linux-coverage build/linux-fuzz fuzz/linux \
	build/android-debug-test \
	build/ios-debug-test \
	build/ios-sim-debug-test \
	wsl/linux wsl/linux-debug show-targets stats/build \
	debug release example devlab \
	run run/win32 run/linux run/macosx \
	run/win32-debug run/linux-debug run/macosx-debug \
	sync/android-libs sync/android-assets sync/android-test-assets install/android-debug run/android-debug log/android \
	run/android-test-debug log/android-test \
	install/ios-debug run/ios-debug log/ios \
	install/ios-test-debug run/ios-test-debug log/ios-test \
	install/ios-sim-test-debug run/ios-sim-test-debug log/ios-sim-test \
	sdk/win32 sdk/linux sdk/macosx sdk/android sdk/ios \
	sdk/win32-debug sdk/linux-debug sdk/macosx-debug sdk/android-debug sdk/ios-debug \
	reinstall/third-party reinstall/third-party/win32 reinstall/third-party/win32-debug \
	reinstall/third-party/linux reinstall/third-party/linux-debug \
	reinstall/third-party/macosx reinstall/third-party/macosx-debug \
	reinstall/third-party/android reinstall/third-party/android-debug \
	reinstall/third-party/ios reinstall/third-party/ios-debug \
	link-compile-commands download-classic-scenes download-skinned-character \
	check/test-manifest check/module-layers check/bindings check/nodiscard check/adhoc-result-tables \
	check/quality-metadata check/profile-matrix check/architecture-contracts check/quality \
	check/examples check \
	check/format check/depgraph check/versions check/scripts check/scripts-test \
	check/test-case-lines check/sq-call-sites check/test-assert-level check/test-case-names \
	profile profile/configure profile/build profile/smoke profile/dry-run \
	ensure-built/win32 ensure-built/win32-debug ensure-built/linux ensure-built/linux-debug \
	ensure-built/macosx ensure-built/macosx-debug \
	init/submodules \
	docs

# Local source-quality checks compare the worktree against this git base.
# CI passes the pull-request base SHA; locally this is origin/dev so a branch
# is linted the same way the PR will be.
CI_BASE ?= origin/dev
ARCHITECTURE_BASE ?= $(CI_BASE)

# Default: every debug target this machine can build (host + optional ios/android/wsl).
all: $(ALL_DEBUG_TARGETS)
	@$(MAKE) link-compile-commands
	@echo "all done: $(ALL_DEBUG_TARGETS)"

show-targets:
	@echo "PLATFORM=$(PLATFORM)"
	@echo "HAS_IOS=$(HAS_IOS) HAS_ANDROID=$(HAS_ANDROID) HAS_WSL=$(HAS_WSL)"
	@echo "ANDROID_SDK=$(ANDROID_SDK)"
	@echo "ANDROID_NDK=$(ANDROID_NDK)"
	@echo "all -> $(ALL_DEBUG_TARGETS)"
	@echo "debug -> build/$(PLATFORM)-debug"
	@echo "release -> build/$(PLATFORM)"
	@echo "sdk -> sdk/$(PLATFORM) (Release) or sdk/$(PLATFORM)-debug"
	@echo "run -> run/$(PLATFORM)-debug (GAME=$(GAME), empty = embedded demo)"
	@echo "check -> source-quality gate (same commands as CI; CI_BASE=$(CI_BASE))"
	@echo "profile -> PROFILE=$(PROFILE) in $(PROFILE_BUILD_ROOT)"
	@echo "profile stages -> profile/configure profile/build profile/smoke"

# Verify every test/*.cpp on disk is registered in test/CMakeLists.txt.
check/test-manifest:
	python3 scripts/check_test_manifest.py

# Verify the examples/ layout contract: runnable vs reviewed non-runnable
# directories, config.nut assigning the engine config table, README.md presence
# and optional curated overview links. Source-only; the runtime half of the
# contract is scripts/smoke_examples.sh (MIN_RUN_SECONDS).
check/examples:
	python3 scripts/check_examples.py

# Verify module includes never climb above the declared manifest LAYER.
check/module-layers:
	python3 scripts/module_depgraph.py --check-layers

# Verify script-facing bindings have a guide explanation or generated SDK
# signature. Unresolved signatures and new unreferenced bindings fail CI.
check/bindings:
	python3 scripts/check_bindings.py --strict

# Validate the bounded exception inventory and its reviewed baseline. This
# target never rewrites the baseline.
check/quality-metadata:
	python3 scripts/check_quality_metadata.py

# Verify no source line declares two test cases. zeroerr registers cases into a
# std::set ordered only by (file, line), so the second case on a line is dropped
# silently and never runs (see scripts/check_test_case_lines.py).
check/test-case-lines:
	python3 scripts/check_test_case_lines.py

# Keep hand-written sq_call sites at their reviewed baseline. sq_call's parameter
# count includes the receiver slot; counting it by hand caused five silent defects.
# New call sites go through eve::script::callFunction, which derives the count.
check/sq-call-sites:
	python3 scripts/check_sq_call_sites.py

# A test case whose assertions are all CHECK reports "PASSED / WARNING" and exits 0, so it
# can never fail (zeroerr maps CHECK* to ZEROERR_WARN). Two cases asserting opposite
# behaviour of the same code both reported green this way. Existing offenders are frozen in
# scripts/test_assert_level_baseline.json; new ones must state the decisive assertion with
# REQUIRE (or use an in-file helper that does).
check/test-assert-level:
	python3 scripts/check_test_assert_level.py

# CTest is keyed by test name, so two cases sharing a name turn every name-based filter
# ("ctest -R ...", unit_test --testcase=X) into one ambiguous entry and make a "N/N passed"
# count stop meaning what it looks like. zeroerr registers by (file, line) and does not
# object, so only a check catches it. No baseline: a duplicate is always an error.
check/test-case-names:
	python3 scripts/check_test_case_names.py

# Compile-fail diagnostics for critical Result/ID/Subscription returns. This
# is a source-only check and does not configure or build the engine.
check/nodiscard:
	python3 scripts/check_nodiscard.py

# Forbid new hand-built `{ok,...}` Squirrel Result tables; allowlist is shrink-only.
check/adhoc-result-tables:
	python3 scripts/check_adhoc_result_tables.py

# Validate the top-level architecture contracts and lint changed C/C++
# lines. The source-only gate never configures or builds the engine.
# The unit test runs with -X utf8: its own stdout/stderr would otherwise follow
# the host ANSI code page (cp936 on a Chinese Windows) and mangle test output.
check/architecture-contracts:
	python3 scripts/check_architecture_contracts.py $(if $(ARCHITECTURE_BASE),--base "$(ARCHITECTURE_BASE)")
	python3 -X utf8 -m unittest scripts.tests.test_architecture_contracts -v

# Feature-specific task recipes are owned by independent fragments.
include $(sort $(wildcard cmake/tasks/*.mk))

# Resolve all supported module profiles without a compiler or build tree.
check/profile-matrix:
	python3 scripts/profile_matrix.py --check

# Fast local quality gate for the profile and debt contracts.
check/quality: check/quality-metadata check/profile-matrix check/nodiscard \
	check/adhoc-result-tables check/architecture-contracts check/test-case-lines \
	check/sq-call-sites check/test-assert-level check/test-case-names

# Individual source-quality pieces that CI also runs (see scripts/check_source_quality.sh).
check/format:
	bash .github/scripts/check-format.sh "$(CI_BASE)"

check/depgraph:
	python3 scripts/module_depgraph.py --check

check/versions:
	python3 scripts/release.py check-versions

check/scripts:
	python3 -m ruff check scripts

check/scripts-test:
	python3 -X utf8 -m unittest discover -s scripts/tests -p "test_*.py" -v

# Source-only gate matching CI job `source-quality`. Does not build the engine.
# Host compile/test is a separate lane: make build/$(PLATFORM)-debug && make test
check:
	bash scripts/check_source_quality.sh "$(CI_BASE)"

# Profile stages are separate so CI can report configure, build, and
# independent capability smoke failures independently. The default build root
# is /tmp; no normal build/<platform> directory is touched.
profile/configure:
	python3 scripts/profile_matrix.py $(PROFILE_MATRIX_ARGS) --configure

profile/build:
	python3 scripts/profile_matrix.py $(PROFILE_MATRIX_ARGS) --build

profile/smoke:
	python3 scripts/profile_matrix.py $(PROFILE_MATRIX_ARGS) --smoke

profile:
	@$(MAKE) profile/configure PROFILE="$(PROFILE)" PROFILE_BUILD_ROOT="$(PROFILE_BUILD_ROOT)" PROFILE_GENERATOR="$(PROFILE_GENERATOR)" PROFILE_PLATFORM="$(PROFILE_PLATFORM)" PROFILE_JOBS="$(PROFILE_JOBS)" PROFILE_CMAKE_COMMAND="$(PROFILE_CMAKE_COMMAND)"
	@$(MAKE) profile/build PROFILE="$(PROFILE)" PROFILE_BUILD_ROOT="$(PROFILE_BUILD_ROOT)" PROFILE_GENERATOR="$(PROFILE_GENERATOR)" PROFILE_PLATFORM="$(PROFILE_PLATFORM)" PROFILE_JOBS="$(PROFILE_JOBS)" PROFILE_CMAKE_COMMAND="$(PROFILE_CMAKE_COMMAND)"
	@$(MAKE) profile/smoke PROFILE="$(PROFILE)" PROFILE_BUILD_ROOT="$(PROFILE_BUILD_ROOT)" PROFILE_GENERATOR="$(PROFILE_GENERATOR)" PROFILE_PLATFORM="$(PROFILE_PLATFORM)" PROFILE_JOBS="$(PROFILE_JOBS)" PROFILE_CMAKE_COMMAND="$(PROFILE_CMAKE_COMMAND)"

profile/dry-run:
	python3 scripts/profile_matrix.py --all --dry-run

# Worktree/agent setup: initialize the pinned git submodules (external/*).
# third-party/ itself is fetched by the first cmake configure at the pinned
# commit (EVENGINE_THIRD_PARTY_PIN in CMakeLists.txt).
init/submodules:
	git submodule update --init --recursive

# clangd: build/compile_commands.json -> host platform debug CDB
link-compile-commands:
	@$(SH) mkdir -p build
	@$(SH) ln -sfn $(PLATFORM)-debug/compile_commands.json build/compile_commands.json \
		|| cp -f build/$(PLATFORM)-debug/compile_commands.json build/compile_commands.json

# Host platform only.
debug: build/$(PLATFORM)-debug
	@$(MAKE) link-compile-commands

# Local build-size and header-fanout report. This intentionally remains an
# explicit developer target so normal incremental builds do not pay for a full
# source/header scan. Override BUILD_DIR to inspect another configuration.
BUILD_STATS_DIR ?= build/$(PLATFORM)-debug
stats/build:
	python3 scripts/analyze_build.py --source-dir . --build-dir "$(BUILD_STATS_DIR)"
release: build/$(PLATFORM)

# Windows host: build Linux targets inside WSL2 (same tree).
wsl/linux-debug:
	wsl --cd "$(CURDIR)" -- make build/linux-debug

wsl/linux:
	wsl --cd "$(CURDIR)" -- make build/linux

# win32: Ninja/MSVC helpers（debug/release 都用 Ninja+cl，经 vcvars 定位任意 VS）
# `: &&` forces the recipe through Git Bash. Windows GNU Make otherwise
# CreateProcess()es a leading .cmd and mishandles flags like -j 32.
WITH_MSVC = : && cmake/with-msvc.cmd
# Override in CI, e.g. VS_GENERATOR="Visual Studio 17 2022"
VS_GENERATOR ?= Visual Studio 18 2026
# Extra cmake -D... flags (CI: CMAKE_EXTRA_ARGS=-DBUILD_TESTING=OFF)
CMAKE_EXTRA_ARGS ?=
# Match CI debug jobs in .github/workflows/ci.yml (windows + linux):
# -DEVENGINE_ENABLE_STRICT_WARNINGS=ON → MSVC /W4 /WX, GCC/Clang -Wall -Wextra -Werror.
# CMAKE_EXTRA_ARGS is appended after this, so an explicit
#   CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_STRICT_WARNINGS=OFF
# still wins. Release, macOS debug, Android, and iOS stay off, matching CI.
# `sdk/win32-debug` / `sdk/linux-debug` package the same debug trees, so they
# inherit the gate; `sdk/win32` / `sdk/linux` (Release) do not.
DEBUG_STRICT_WARNINGS_FLAG = -DEVENGINE_ENABLE_STRICT_WARNINGS=ON
JOBS ?= 32
ANDROID_JOBS ?= 8
CTEST_JOBS ?= 4

# Profile contract checks deliberately use a separate root so they cannot
# mutate the normal build/<platform> tree. Set PROFILE_BUILD_ROOT explicitly
# when several profile jobs share a workspace (CI uses runner.temp).
PROFILE ?= minimal
PROFILE_BUILD_ROOT ?= /tmp/evengine-profile-matrix
PROFILE_GENERATOR ?= Ninja
PROFILE_PLATFORM ?=
PROFILE_JOBS ?= 4
PROFILE_CMAKE_COMMAND ?= cmake
PROFILE_MATRIX_ARGS = --profile "$(PROFILE)" \
	--build-root "$(PROFILE_BUILD_ROOT)" \
	--generator "$(PROFILE_GENERATOR)" \
	--jobs "$(PROFILE_JOBS)" \
	$(if $(PROFILE_PLATFORM),--platform "$(PROFILE_PLATFORM)") \
	--cmake-command "$(PROFILE_CMAKE_COMMAND)"

# Reusable configure command lines: used both by the first-configure rules and
# by the on-change reconfigure inside the build recipes below.
MSVC_COMPILER_WRAPPER   := $(abspath cmake/msvc-cl.cmd)
# Module linkage policy: development wants the dynamic route so iterating on a
# domain test does not relink the whole engine; the release build and the SDK it
# installs (`make sdk/win32`) must ship one self-contained exe with no engine
# DLLs beside it, so they pin OBJECT here rather than inheriting a default that
# will flip to SHARED for development. Opt into the dynamic route locally with
# `CMAKE_EXTRA_ARGS="-DEVENGINE_MODULE_LINKAGE=SHARED"` (tracked by
# reconfigure-if-args-changed, so switching it reconfigures).
WIN32_CMAKE_ARGS        = -G Ninja -DCMAKE_BUILD_TYPE=Release -DEVENGINE_MODULE_LINKAGE=OBJECT -DCMAKE_C_COMPILER=$(MSVC_COMPILER_WRAPPER) -DCMAKE_CXX_COMPILER=$(MSVC_COMPILER_WRAPPER) $(CMAKE_EXTRA_ARGS) -B build/win32 -S .
WIN32_DEBUG_CMAKE_ARGS  = -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=$(MSVC_COMPILER_WRAPPER) -DCMAKE_CXX_COMPILER=$(MSVC_COMPILER_WRAPPER) $(DEBUG_STRICT_WARNINGS_FLAG) $(CMAKE_EXTRA_ARGS) -B build/win32-debug -S .
LINUX_CMAKE_ARGS        = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=linux -DEVENGINE_MODULE_LINKAGE=OBJECT $(CMAKE_EXTRA_ARGS) -B build/linux -S .
LINUX_DEBUG_CMAKE_ARGS  = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=linux $(DEBUG_STRICT_WARNINGS_FLAG) $(CMAKE_EXTRA_ARGS) -B build/linux-debug -S .
MACOSX_CMAKE_ARGS       = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=macosx $(CMAKE_EXTRA_ARGS) -B build/macosx -S .
MACOSX_DEBUG_CMAKE_ARGS = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=macosx $(CMAKE_EXTRA_ARGS) -B build/macosx-debug -S .
WIN32_DEBUG_CONFIG_STAMP = $(strip $(DEBUG_STRICT_WARNINGS_FLAG) $(CMAKE_EXTRA_ARGS))
LINUX_DEBUG_CONFIG_STAMP = $(strip $(DEBUG_STRICT_WARNINGS_FLAG) $(CMAKE_EXTRA_ARGS))

# $(call reconfigure-if-args-changed,<build-dir>,<cmake-command>[,<stamp>])
# Re-runs cmake when the recorded stamp differs from the current make-level
# value. A missing stamp is recorded without a second configure: the generated-
# file prerequisite has just configured. The optional third argument defaults
# to CMAKE_EXTRA_ARGS. build/<plat> targets are phony, so this check runs on
# every invocation but costs only one cat when nothing changed.
define reconfigure-if-args-changed
	@stamp="$(if $(3),$(3),$(CMAKE_EXTRA_ARGS))"; \
	if [ ! -f $(1)/.eve-config-args ]; then \
	  printf '%s\n' "$$stamp" > $(1)/.eve-config-args; \
	elif [ "$$(cat $(1)/.eve-config-args)" != "$$stamp" ]; then \
	  echo "cmake config args changed; reconfiguring $(1)"; \
	  $(2); \
	  printf '%s\n' "$$stamp" > $(1)/.eve-config-args; \
	fi
endef

include cmake/platforms/win32-release.mk

include cmake/platforms/linux-release.mk

include cmake/platforms/macosx-release.mk

include cmake/platforms/android-release.mk

include cmake/platforms/win32-debug.mk

include cmake/platforms/linux-debug.mk

include cmake/platforms/macosx-debug.mk

include cmake/platforms/android-debug.mk

include cmake/platforms/ios-build.mk

# Rebuild changed third-party sources and run install again without deleting
# build/third-party or build/third-party-binary.
reinstall/third-party: reinstall/third-party/$(PLATFORM)-debug

reinstall/third-party/win32:
	cmake.exe --build build/third-party/win32 --target install --config Release -j 32
	cmake.exe -DPREFIX=build/third-party-binary/win32 -DTP_DIR=third-party -DPLATFORM=win32 -DBUILD_TYPE=Release -P cmake/write_third_party_version.cmake

reinstall/third-party/win32-debug:
	$(WITH_MSVC) cmake.exe --build build/third-party/win32-debug --target install -j 32
	$(WITH_MSVC) cmake.exe -DPREFIX=build/third-party-binary/win32-debug -DTP_DIR=third-party -DPLATFORM=win32 -DBUILD_TYPE=Debug -P cmake/write_third_party_version.cmake

reinstall/third-party/linux:
	cmake --build build/third-party/linux --target install -j 32
	cmake -DPREFIX=build/third-party-binary/linux -DTP_DIR=third-party -DPLATFORM=linux -DBUILD_TYPE=Release -P cmake/write_third_party_version.cmake

reinstall/third-party/linux-debug:
	cmake --build build/third-party/linux-debug --target install -j 32
	cmake -DPREFIX=build/third-party-binary/linux-debug -DTP_DIR=third-party -DPLATFORM=linux -DBUILD_TYPE=Debug -P cmake/write_third_party_version.cmake

reinstall/third-party/macosx:
	cmake --build build/third-party/macosx --target install -j 32
	cmake -DPREFIX=build/third-party-binary/macosx -DTP_DIR=third-party -DPLATFORM=macosx -DBUILD_TYPE=Release -P cmake/write_third_party_version.cmake

reinstall/third-party/macosx-debug:
	cmake --build build/third-party/macosx-debug --target install -j 32
	cmake -DPREFIX=build/third-party-binary/macosx-debug -DTP_DIR=third-party -DPLATFORM=macosx -DBUILD_TYPE=Debug -P cmake/write_third_party_version.cmake

reinstall/third-party/android:
	cmake --build build/third-party/android --target install -j 8
	cmake -DPREFIX=build/third-party-binary/android -DTP_DIR=third-party -DPLATFORM=android -DBUILD_TYPE=Release -P cmake/write_third_party_version.cmake

reinstall/third-party/android-debug:
	cmake --build build/third-party/android-debug --target install -j 8
	cmake -DPREFIX=build/third-party-binary/android-debug -DTP_DIR=third-party -DPLATFORM=android -DBUILD_TYPE=Debug -P cmake/write_third_party_version.cmake

reinstall/third-party/ios:
	cmake --build build/third-party/ios --target install -j 8
	cmake -DPREFIX=build/third-party-binary/ios -DTP_DIR=third-party -DPLATFORM=ios -DBUILD_TYPE=Release -P cmake/write_third_party_version.cmake

reinstall/third-party/ios-debug:
	cmake --build build/third-party/ios-debug --target install -j 8
	cmake -DPREFIX=build/third-party-binary/ios-debug -DTP_DIR=third-party -DPLATFORM=ios -DBUILD_TYPE=Debug -P cmake/write_third_party_version.cmake

include cmake/platforms/android-run.mk

include cmake/platforms/ios-run.mk

download-classic-scenes:
	bash scripts/download_classic_scenes.sh

# Fetch CesiumMan (~0.5 MB) for animation.skinned.* skeletal skinning tests.
download-skinned-character:
	bash scripts/download_skinned_character.sh

# ---- API documentation (Doxygen) ----
#   make docs                 # generate docs/api/html from docs/Doxyfile.in
#   make docs CLEAN_DOCS=1    # remove the previous output first
# Output is git-ignored (docs/api/); open docs/api/html/index.html afterwards.
# Configures a docs-only CMake tree (build/docs) so this does not need a full engine build.
CLEAN_DOCS ?= 0
DOCS_BUILD_DIR ?= build/docs

docs:
	@if [ "$(CLEAN_DOCS)" = "1" ]; then rm -rf docs/api; fi
	@if [ ! -f $(DOCS_BUILD_DIR)/CMakeCache.txt ]; then \
		cmake -S docs -B $(DOCS_BUILD_DIR); \
	fi
	cmake --build $(DOCS_BUILD_DIR) --target docs
	@echo "API docs generated -> docs/api/html/index.html"

# Optional name-prefix filter for platform targets: make test FILTER=graphics.print
# (per-case run; use FILTER=bundle/<file> for a single-file bundle, e.g.
#  FILTER=bundle/ClassicScenes.cpp).
CTEST_FILTER = $(if $(FILTER),-R '^$(subst .,\.,$(FILTER))')
LABEL_FILTER ?=
# Level is independent of DOMAIN and name filters. Empty selections are errors.
TEST_LEVEL ?= all
TEST_BASE ?= origin/dev

# Build-derived source -> CTest index. The normal host build refreshes the index
# after case discovery; explicit selection refreshes dependency facts again.
test-impact/%: ensure-built/%
	python3 -X utf8 scripts/test_impact.py select --build-dir build/$* --base "$(TEST_BASE)" --level "$(TEST_LEVEL)" $(if $(filter win32,$*),--config Release,) $(if $(DOMAIN),--domain "$(DOMAIN)",)

test-affected/%: ensure-built/%
	$(CTEST_ENV) python3 -X utf8 scripts/test_impact.py run --build-dir build/$* --base "$(TEST_BASE)" --level "$(TEST_LEVEL)" --jobs $(CTEST_JOBS) $(if $(filter win32,$*),--config Release,) $(if $(DOMAIN),--domain "$(DOMAIN)",)
ifneq ($(words $(TEST_LEVEL)),1)
$(error TEST_LEVEL must be all, unit, or integration)
endif
ifeq ($(filter $(TEST_LEVEL),all unit integration),)
$(error TEST_LEVEL must be all, unit, or integration)
endif
CTEST_LEVEL_SEL = $(if $(filter all,$(TEST_LEVEL)),,-L '^$(TEST_LEVEL)$$' --no-tests=error)
CTEST_LABEL_FILTER = $(if $(LABEL_FILTER),-L '$(LABEL_FILTER)') $(CTEST_LEVEL_SEL)

# Restrict a run to one test link unit (see test/test_domains.cmake):
#   make test/win32-debug DOMAIN=physics
# Each domain executable labels its own cases with its target name, so this is a
# plain ctest -L; the alternative (test/<domain> targets) would shadow the
# test/<name-prefix> pattern rule below.
# Only meaningful when the suite is split (EVENGINE_TEST_DOMAIN_SPLIT, on by
# default for the SHARED route and off for the archive route, where the suite is
# one monolithic unit_test to keep its debug info small). A monolithic build
# carries only the `unit_test` label, so DOMAIN= there would silently select zero
# cases and exit 0; --no-tests=error turns that into a failure instead.
CTEST_DOMAIN_SEL = $(if $(DOMAIN),-L '^unit_test_$(DOMAIN)$$' --no-tests=error,)

# Default: run tests per case (process-isolated; this is the fast path on CI —
# main runs 1526 cases in ~2-11 min).  "bundle/<file>" entries stay registered
# by cmake/ZeroErrDiscoverTestsImpl.cmake as an opt-in: GPU/window tests were
# ~70x slower when several shared one process on CI, so bundles are excluded
# unless requested (FILTER=bundle/<file> or ctest -L bundle).
# Full FPS sweeps are opt-in; correctness still covers every ClassicScenes asset.
# Run FILTER=ClassicScenes.perf.maxFps or INCLUDE_BENCHMARKS=1 to include them.
INCLUDE_BENCHMARKS ?= 0
CTEST_BENCHMARK_SEL = $(if $(FILTER),,$(if $(filter 1,$(INCLUDE_BENCHMARKS)),,-LE benchmark))
CTEST_RUN_SEL = $(if $(filter bundle/%,$(FILTER)),-L bundle,-E '^bundle/') $(CTEST_BENCHMARK_SEL)

# Retry each failed test once before reporting it (see CI-DEBUG-PLAYBOOK:
# xvfb display allocation, first-run network fetches and TCP echo timing can
# fail once on a loaded runner while the code is healthy). A clean run is
# unaffected; a genuinely broken test still fails after the repeat.
CTEST_REPEAT ?= --repeat until-pass:2

# ClassicScenes live-view pacing and perf benchmarks are tuned for humans
# (4 s/phase, 120 timed frames).  `make test` uses fast headless defaults;
# override for interactive runs: make test VIEW_SECONDS=4 PERF_FRAMES=120.
VIEW_SECONDS ?= 0.3
PERF_FRAMES ?= 30
CTEST_ENV = EVENGINE_VIEW_SECONDS=$(VIEW_SECONDS) EVENGINE_PERF_FRAMES=$(PERF_FRAMES)

# Run discovered zeroerr cases via CTest (see cmake/ZeroErrDiscoverTests.cmake).
test/win32: ensure-built/win32
	$(CTEST_ENV) ctest --test-dir build/win32 -C Release --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

test/win32-debug: ensure-built/win32-debug
	$(CTEST_ENV) ctest --test-dir build/win32-debug --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

test/linux: ensure-built/linux
	$(CTEST_ENV) ctest --test-dir build/linux -C Release --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

test/linux-debug: ensure-built/linux-debug
	$(CTEST_ENV) ctest --test-dir build/linux-debug --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

# Sanitizer (ASan+UBSan) and coverage builds are opt-in Linux variants; CI
# uses them for the quality-gate jobs. Runtime env for tests:
#   ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
test/linux-asan:
	$(CTEST_ENV) ctest --test-dir build/linux-asan --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

test/linux-coverage:
	$(CTEST_ENV) ctest --test-dir build/linux-coverage --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

test/macosx: ensure-built/macosx
	$(CTEST_ENV) ctest --test-dir build/macosx -C Release --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

test/macosx-debug: ensure-built/macosx-debug
	$(CTEST_ENV) ctest --test-dir build/macosx-debug --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_DOMAIN_SEL) $(CTEST_LABEL_FILTER) $(CTEST_FILTER) $(CTEST_REPEAT)

# Host-debug shortcut by test-name prefix, e.g. `make test/graphics.print`
# (explicit test/<platform> rules above take precedence over this pattern).
test/%: ensure-built/$(PLATFORM)-debug
	$(CTEST_ENV) ctest --test-dir build/$(PLATFORM)-debug --output-on-failure -j $(CTEST_JOBS) $(CTEST_LABEL_FILTER) -R '^$(subst .,\.,$*)' $(CTEST_REPEAT)

# Build only one domain link unit. With EVENGINE_TEST_DOMAIN_SPLIT (on by
# default for the SHARED route; the archive route builds one monolithic unit_test
# instead, see test/CMakeLists.txt), an agent working on a single module compiles
# and links only that unit instead of every test translation unit at once.
# The build directory must already be configured (any earlier `make build/<platform>`
# or `make test/<platform>`):
#   make unit-test/win32-debug DOMAIN=physics
#   make test/win32-debug DOMAIN=physics
unit-test/%:
	@test -n "$(DOMAIN)" || { \
	  echo "usage: make unit-test/<platform> DOMAIN=<domain>"; \
	  echo "domains are declared in test/test_domains.cmake"; exit 2; }
	@if [ ! -f build/$*/build.ninja ] && [ ! -f build/$*/Makefile ]; then \
	  echo "build/$* is not configured; run: make build/$*"; exit 2; fi
	@if ! ctest --test-dir build/$* -N -L '^unit_test_$(DOMAIN)$$' 2>/dev/null | grep -q 'Total Tests: [1-9]'; then \
	  echo "build/$* has no unit_test_$(DOMAIN): the suite is monolithic there"; \
	  echo "(EVENGINE_TEST_DOMAIN_SPLIT=OFF). Reconfigure with"; \
	  echo "-DEVENGINE_TEST_DOMAIN_SPLIT=ON for per-domain units, or build the whole"; \
	  echo "suite with: cmake --build build/$* --target unit_test"; exit 2; fi
	$(if $(findstring win32,$*),$(WITH_MSVC) cmake.exe,cmake) --build build/$* --target unit_test_$(DOMAIN) -j $(JOBS)

# Host platform debug shortcut (same as run/$(PLATFORM)-debug).
run: run/$(PLATFORM)-debug

# PATH prefix for running a built binary from its build tree.
# SHARED module-linkage builds (cmake/link_groups.cmake) write the link-group
# DLLs to the CMake binary dir -> build/<plat>, while eve.exe lives in
# build/<plat>/src/engine. Windows resolves DLLs through PATH (there is no
# rpath), so a local run needs build/<plat> on PATH -- the same directory the
# generated CTest entries prepend (cmake/ZeroErrDiscoverTestsImpl.cmake).
# The build tree is probed instead of adding a flag: the static/one-exe route
# (release passes -DEVENGINE_MODULE_LINKAGE=OBJECT) produces no group DLL, so the
# prefix is empty and the recipe is byte-for-byte what it was before.
# `cd ... && pwd` rather than $(CURDIR)/... because this recipe runs under Git
# bash, which does not accept a "C:/..." element inside PATH (MSYS then rewrites
# it relative to its own root and the DLL directory is silently lost); pwd
# yields the "/c/evt/..." form the loader conversion understands.
# PATH is the Win32 loader's search path. The ELF/Mach-O equivalent is
# LD_LIBRARY_PATH / DYLD_LIBRARY_PATH, which eve-dll-path-so prefixes for the
# linux/macosx recipes below; both macros probe the build tree, so a
# static/one-exe build (release passes -DEVENGINE_MODULE_LINKAGE=OBJECT) yields
# an empty prefix and the recipe is byte-for-byte what it was before.
eve-dll-path = $(if $(wildcard $(CURDIR)/$(1)/EVFoundation.dll),$$(cd '$(CURDIR)/$(1)' && pwd):,)
eve-dll-path-so = $(if $(wildcard $(CURDIR)/$(1)/libEVFoundation.so $(CURDIR)/$(1)/libEVFoundation.dylib),$(CURDIR)/$(1):,)

# Desktop: run built eve. With GAME unset, run with no args from the repo
# root so eve finds no main.nut and falls back to the embedded release demo.
# With GAME set, cd into it and run the "run" subcommand against it instead.
# Uses $(CURDIR) (make's invocation dir, absolute) rather than a relative
# "../build" so this works no matter how deeply GAME is nested
# (e.g. GAME=examples/rpg, not just single-level GAME=example).
#   make run/macosx-debug
#   make run/linux-debug GAME=example
#   make run/macosx-debug GAME=examples/rpg
#   make run              # current host platform, debug, embedded demo
run/win32-debug: ensure-built/win32-debug
	@if [ -n "$(GAME)" ]; then cd $(GAME) && PATH="$(call eve-dll-path,build/win32-debug)$$PATH" "$(CURDIR)/build/win32-debug/src/engine/eve.exe" run $(RUN_ARGS); \
	else PATH="$(call eve-dll-path,build/win32-debug)$$PATH" build/win32-debug/src/engine/eve.exe $(RUN_ARGS); fi

run/linux-debug: ensure-built/linux-debug
	@if [ -n "$(GAME)" ]; then cd $(GAME) && LD_LIBRARY_PATH="$(call eve-dll-path-so,build/linux-debug)$$LD_LIBRARY_PATH" "$(CURDIR)/build/linux-debug/src/engine/eve" run $(RUN_ARGS); \
	else LD_LIBRARY_PATH="$(call eve-dll-path-so,build/linux-debug)$$LD_LIBRARY_PATH" build/linux-debug/src/engine/eve $(RUN_ARGS); fi

run/macosx-debug: ensure-built/macosx-debug
	@if [ -n "$(GAME)" ]; then cd $(GAME) && DYLD_LIBRARY_PATH="$(call eve-dll-path-so,build/macosx-debug)$$DYLD_LIBRARY_PATH" "$(CURDIR)/build/macosx-debug/src/engine/eve" run $(RUN_ARGS); \
	else DYLD_LIBRARY_PATH="$(call eve-dll-path-so,build/macosx-debug)$$DYLD_LIBRARY_PATH" build/macosx-debug/src/engine/eve $(RUN_ARGS); fi

run/win32: ensure-built/win32
	@if [ -n "$(GAME)" ]; then cd $(GAME) && PATH="$(call eve-dll-path,build/win32)$$PATH" "$(CURDIR)/build/win32/src/engine/eve.exe" run $(RUN_ARGS); \
	else PATH="$(call eve-dll-path,build/win32)$$PATH" build/win32/src/engine/eve.exe $(RUN_ARGS); fi

run/linux: ensure-built/linux
	@if [ -n "$(GAME)" ]; then cd $(GAME) && LD_LIBRARY_PATH="$(call eve-dll-path-so,build/linux)$$LD_LIBRARY_PATH" "$(CURDIR)/build/linux/src/engine/eve" run $(RUN_ARGS); \
	else LD_LIBRARY_PATH="$(call eve-dll-path-so,build/linux)$$LD_LIBRARY_PATH" build/linux/src/engine/eve $(RUN_ARGS); fi

run/macosx: ensure-built/macosx
	@if [ -n "$(GAME)" ]; then cd $(GAME) && DYLD_LIBRARY_PATH="$(call eve-dll-path-so,build/macosx)$$DYLD_LIBRARY_PATH" "$(CURDIR)/build/macosx/src/engine/eve" run $(RUN_ARGS); \
	else DYLD_LIBRARY_PATH="$(call eve-dll-path-so,build/macosx)$$DYLD_LIBRARY_PATH" build/macosx/src/engine/eve $(RUN_ARGS); fi

debug/win32:
	@if [ -n "$(GAME)" ]; then cd $(GAME) && PATH="$(call eve-dll-path,build/win32-debug)$$PATH" "$(CURDIR)/build/win32-debug/src/engine/eve.exe" run $(RUN_ARGS); \
	else PATH="$(call eve-dll-path,build/win32-debug)$$PATH" build/win32-debug/src/engine/eve.exe $(RUN_ARGS); fi

debug/linux:
	@if [ -n "$(GAME)" ]; then cd $(GAME) && LD_LIBRARY_PATH="$(call eve-dll-path-so,build/linux-debug)$$LD_LIBRARY_PATH" "$(CURDIR)/build/linux-debug/src/engine/eve" run $(RUN_ARGS); \
	else LD_LIBRARY_PATH="$(call eve-dll-path-so,build/linux-debug)$$LD_LIBRARY_PATH" build/linux-debug/src/engine/eve $(RUN_ARGS); fi

debug/macosx:
	@if [ -n "$(GAME)" ]; then cd $(GAME) && DYLD_LIBRARY_PATH="$(call eve-dll-path-so,build/macosx-debug)$$DYLD_LIBRARY_PATH" "$(CURDIR)/build/macosx-debug/src/engine/eve" run $(RUN_ARGS); \
	else DYLD_LIBRARY_PATH="$(call eve-dll-path-so,build/macosx-debug)$$DYLD_LIBRARY_PATH" build/macosx-debug/src/engine/eve $(RUN_ARGS); fi

tools/debug:
	cd tools/vscode-eve-debug && npx @vscode/vsce package 
	cursor --install-extension ./tools/vscode-eve-debug/*.vsix

# Target-platform SDK install (independent prefix per plat; for publishing games TO that plat).
# Release: make sdk/macosx  → builds build/macosx then dist/eve-sdk/macosx
# Debug:   make sdk/macosx-debug
# win32/linux debug SDK trees reuse build/*-debug, including the CI warning gate.
sdk/win32: build/win32
sdk/linux: build/linux
sdk/macosx: build/macosx
sdk/android: build/android
sdk/ios: build/ios
sdk/win32-debug: build/win32-debug
sdk/linux-debug: build/linux-debug
sdk/macosx-debug: build/macosx-debug
sdk/android-debug: build/android-debug
sdk/ios-debug: build/ios-debug

sdk/win32 sdk/linux sdk/macosx sdk/android sdk/ios \
sdk/win32-debug sdk/linux-debug sdk/macosx-debug sdk/android-debug sdk/ios-debug:
	@set -e; plat=$@; plat=$${plat#sdk/}; \
	  cmake --install "build/$$plat" --prefix "dist/eve-sdk/$$plat"; \
	  echo "Installed target SDK -> dist/eve-sdk/$$plat"

basic:
	@$(MAKE) run/$(PLATFORM)-debug GAME=examples/basic

devlab:
	@$(MAKE) run/$(PLATFORM)-debug GAME=examples/devlab RUN_ARGS="--debug"

rpg:
	@$(MAKE) run/$(PLATFORM)-debug GAME=examples/rpg

inventory:
	@$(MAKE) run/$(PLATFORM)-debug GAME=examples/inventory

building:
	@$(MAKE) run/$(PLATFORM)-debug GAME=examples/building

ecs:
	@$(MAKE) run/$(PLATFORM)-debug GAME=examples/ecs

rpg-classic:
	@echo rpg-classic is a standalone game: https://github.com/EVEngine/rpg-classic
	@echo Clone it next to EVEngine, then: eve run .   or   make run/$(PLATFORM)-debug GAME=../rpg-classic

rpg-classic/tilemap:
	@echo Use the rpg-classic repo: eve run apps/tilemap

rpg-classic/event:
	@echo Use the rpg-classic repo: eve run apps/event

rpg-classic/skill:
	@echo Use the rpg-classic repo: eve run apps/skill

# make: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cmake -H./src -B./src/build && cmake --build src/build --parallel 8"

# deps: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cd src/third-party && bash build.sh"

# start: build/.build-docker
# 	$(DOCKER_START) /bin/bash

# build/.build-docker: Dockerfile
# 	docker build . --tag evengine
# 	mkdir -p build && touch $@

include cmake/native_arm64.mk
