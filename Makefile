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
ANDROID_NDK ?= $(ANDROID_SDK)/ndk/26.1.10909125
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
	wsl/linux wsl/linux-debug show-targets \
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
	check/test-manifest check/module-layers \
	ensure-built/win32 ensure-built/win32-debug ensure-built/linux ensure-built/linux-debug \
	ensure-built/macosx ensure-built/macosx-debug \
	init/submodules \
	docs

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

# Verify every test/*.cpp on disk is registered in test/CMakeLists.txt.
check/test-manifest:
	python3 scripts/check_test_manifest.py

# Verify module includes never climb above the declared manifest LAYER.
check/module-layers:
	python3 scripts/module_depgraph.py --check-layers

# Verify every script-facing addFunc is actually documented (see
# docs/usr/REVIEW.md). Pre-existing debt is registered in
# scripts/check_bindings_gaps.txt (regenerate with --write-gaps); new gaps
# fail, so docs cannot silently drift from the bindings.
check/bindings:
	python3 scripts/check_bindings.py --strict

# Worktree/agent setup: initialize the pinned git submodules (external/*).
# third-party/ itself is fetched by the first cmake configure at the pinned
# commit (EVENGINE_THIRD_PARTY_PIN in CMakeLists.txt).
init/submodules:
	git submodule update --init --recursive

# clangd: build/compile_commands.json -> host platform debug CDB
link-compile-commands:
	@mkdir -p build
	ln -sfn $(PLATFORM)-debug/compile_commands.json build/compile_commands.json

# Host platform only.
debug: build/$(PLATFORM)-debug
	@$(MAKE) link-compile-commands
release: build/$(PLATFORM)

# Windows host: build Linux targets inside WSL2 (same tree).
wsl/linux-debug:
	wsl --cd "$(CURDIR)" -- make build/linux-debug

wsl/linux:
	wsl --cd "$(CURDIR)" -- make build/linux

# win32: Ninja/MSVC helpers（debug/release 都用 Ninja+cl，经 vcvars 定位任意 VS）
WITH_MSVC = cmake\with-msvc.cmd
# Override in CI, e.g. VS_GENERATOR="Visual Studio 17 2022"
VS_GENERATOR ?= Visual Studio 18 2026
# Extra cmake -D... flags (CI: CMAKE_EXTRA_ARGS=-DBUILD_TESTING=OFF)
CMAKE_EXTRA_ARGS ?=
JOBS ?= 32
ANDROID_JOBS ?= 8
CTEST_JOBS ?= 4

# Reusable configure command lines: used both by the first-configure rules and
# by the on-change reconfigure inside the build recipes below.
WIN32_CMAKE_ARGS        = -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl $(CMAKE_EXTRA_ARGS) -B build/win32 -S .
WIN32_DEBUG_CMAKE_ARGS  = -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl $(CMAKE_EXTRA_ARGS) -B build/win32-debug -S .
LINUX_CMAKE_ARGS        = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=linux $(CMAKE_EXTRA_ARGS) -B build/linux -S .
LINUX_DEBUG_CMAKE_ARGS  = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=linux $(CMAKE_EXTRA_ARGS) -B build/linux-debug -S .
MACOSX_CMAKE_ARGS       = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=macosx $(CMAKE_EXTRA_ARGS) -B build/macosx -S .
MACOSX_DEBUG_CMAKE_ARGS = -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=macosx $(CMAKE_EXTRA_ARGS) -B build/macosx-debug -S .

# $(call reconfigure-if-args-changed,<build-dir>,<cmake-command>)
# Re-runs cmake when the recorded CMAKE_EXTRA_ARGS stamp differs from the
# current make-level value. build/<plat> targets are phony, so this check runs
# on every invocation but costs only one cat when nothing changed.
define reconfigure-if-args-changed
	@if [ ! -f $(1)/.eve-config-args ]; then \
	  printf '%s\n' "$(CMAKE_EXTRA_ARGS)" > $(1)/.eve-config-args; \
	elif [ "$$(cat $(1)/.eve-config-args)" != "$(CMAKE_EXTRA_ARGS)" ]; then \
	  echo "CMAKE_EXTRA_ARGS changed; reconfiguring $(1)"; \
	  $(2); \
	  printf '%s\n' "$(CMAKE_EXTRA_ARGS)" > $(1)/.eve-config-args; \
	fi
endef

build/win32: build/win32/build.ninja
	$(call reconfigure-if-args-changed,build/win32,$(WITH_MSVC) cmake.exe $(WIN32_CMAKE_ARGS))
	$(WITH_MSVC) cmake.exe --build $@ --target deps -j $(JOBS)
	$(WITH_MSVC) cmake.exe --build $@ -j $(JOBS)

build/win32/build.ninja:
	$(WITH_MSVC) cmake.exe $(WIN32_CMAKE_ARGS)

# Fast up-to-date check for repeated run/test loops: ninja's dry run costs
# ~100ms and lists every pending edge, so vcvars + cmake only run when
# something is actually stale. check_sources_* rescan targets run on every
# build by design, so their lines are filtered out of the dry-run output.
ensure-built/win32:
	@stale=0; \
	for f in build/win32/src/modules/*_src.txt build/win32/src/engine/*_src.txt; do \
	  [ -f "$$f" ] || continue; \
	  folder=$${f##*/}; folder=$${folder%_src.txt}; \
	  for src in src/modules/$$folder src/engine/$$folder; do \
	    if [ -d "$$src" ] && [ "$$src" -nt "$$f" ]; then stale=1; break 2; fi; \
	  done; \
	done; \
	if [ "$$stale" = 0 ] && [ -d test ] && [ -f build/win32/test/unit_test.exe ] \
	   && [ test -nt build/win32/test/unit_test.exe ]; then stale=1; fi; \
	if [ -f build/win32/build.ninja ] \
	   && [ "$$(cat build/win32/.eve-config-args 2>/dev/null)" = "$(CMAKE_EXTRA_ARGS)" ] \
	   && [ "$$stale" = 0 ] \
	   && ! ninja -C build/win32 -n 2>&1 \
	        | grep -v 'Entering directory' \
	        | grep -v 'no work to do' \
	        | grep -v 'rescan_source.cmake' \
	        | grep -q .; then \
	  echo "build/win32: up to date"; \
	else \
	  $(MAKE) build/win32; \
	fi

build/linux: build/linux/Makefile
	$(call reconfigure-if-args-changed,build/linux,cmake $(LINUX_CMAKE_ARGS))
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/linux/Makefile:
	cmake $(LINUX_CMAKE_ARGS)

# Unix Makefiles cannot be dry-run reliably (cmake_check_build_system and the
# ALL custom targets always report work), so linux/macosx just build; make's
# own incremental check keeps the no-op case cheap.
ensure-built/linux: build/linux

build/macosx: build/macosx/Makefile
	$(call reconfigure-if-args-changed,build/macosx,cmake $(MACOSX_CMAKE_ARGS))
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/macosx/Makefile:
	cmake $(MACOSX_CMAKE_ARGS)

ensure-built/macosx: build/macosx

build/android: build/android/build.ninja
	cmake --build $@ --target deps -j $(ANDROID_JOBS)
	cmake --build $@ -j $(ANDROID_JOBS)
	$(MAKE) sync/android-libs BUILD_DIR=build/android
	$(MAKE) sync/android-assets
	cd $(APK_DIR) && JAVA_HOME="$(JAVA_HOME)" ANDROID_HOME="$(ANDROID_SDK)" ./gradlew assembleRelease

build/android/build.ninja:
	cmake -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake \
		-DANDROID_ABI=$(ANDROID_ABI) \
		-DANDROID_PLATFORM=$(ANDROID_PLATFORM) \
		-DANDROID_STL=$(ANDROID_STL) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_PLATFORM=android \
		-DBUILD_TESTING=OFF \
		$(CMAKE_EXTRA_ARGS) \
		-B build/android -S .

build/win32-debug: build/win32-debug/build.ninja
	$(call reconfigure-if-args-changed,build/win32-debug,$(WITH_MSVC) cmake.exe $(WIN32_DEBUG_CMAKE_ARGS))
	$(WITH_MSVC) cmake.exe --build $@ --target deps -j $(JOBS)
	$(WITH_MSVC) cmake.exe --build $@ -j $(JOBS)

build/win32-debug/build.ninja:
	$(WITH_MSVC) cmake.exe $(WIN32_DEBUG_CMAKE_ARGS)

ensure-built/win32-debug:
	@stale=0; \
	for f in build/win32-debug/src/modules/*_src.txt build/win32-debug/src/engine/*_src.txt; do \
	  [ -f "$$f" ] || continue; \
	  folder=$${f##*/}; folder=$${folder%_src.txt}; \
	  for src in src/modules/$$folder src/engine/$$folder; do \
	    if [ -d "$$src" ] && [ "$$src" -nt "$$f" ]; then stale=1; break 2; fi; \
	  done; \
	done; \
	if [ "$$stale" = 0 ] && [ -d test ] && [ -f build/win32-debug/test/unit_test.exe ] \
	   && [ test -nt build/win32-debug/test/unit_test.exe ]; then stale=1; fi; \
	if [ -f build/win32-debug/build.ninja ] \
	   && [ "$$(cat build/win32-debug/.eve-config-args 2>/dev/null)" = "$(CMAKE_EXTRA_ARGS)" ] \
	   && [ "$$stale" = 0 ] \
	   && ! ninja -C build/win32-debug -n 2>&1 \
	        | grep -v 'Entering directory' \
	        | grep -v 'no work to do' \
	        | grep -v 'rescan_source.cmake' \
	        | grep -q .; then \
	  echo "build/win32-debug: up to date"; \
	else \
	  $(MAKE) build/win32-debug; \
	fi

build/linux-debug: build/linux-debug/Makefile
	$(call reconfigure-if-args-changed,build/linux-debug,cmake $(LINUX_DEBUG_CMAKE_ARGS))
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/linux-debug/Makefile:
	cmake $(LINUX_DEBUG_CMAKE_ARGS)

ensure-built/linux-debug: build/linux-debug

build/linux-asan: build/linux-asan/Makefile
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/linux-asan/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=linux \
		-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer" \
		-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr" \
		-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr" \
		$(CMAKE_EXTRA_ARGS) -B build/linux-asan -S .

build/linux-coverage: build/linux-coverage/Makefile
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/linux-coverage/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=linux \
		-DCMAKE_C_FLAGS="--coverage -O0" \
		-DCMAKE_CXX_FLAGS="--coverage -O0" \
		-DCMAKE_EXE_LINKER_FLAGS="--coverage" \
		-DCMAKE_SHARED_LINKER_FLAGS="--coverage" \
		$(CMAKE_EXTRA_ARGS) -B build/linux-coverage -S .

# Coverage-guided parser fuzzing is intentionally a small standalone Clang
# build. The engine's Linux build remains GCC, matching production CI.
build/linux-fuzz: build/linux-fuzz/build.ninja
	cmake --build build/linux-fuzz -j $(JOBS)

build/linux-fuzz/build.ninja:
	cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
		$(CMAKE_EXTRA_ARGS) \
		-B build/linux-fuzz -S test/fuzz

fuzz/linux: build/linux-fuzz
	cd build/linux-fuzz && ./evengine_json_fuzz --fuzz --testcase='^fuzz.json.document$$'

build/macosx-debug: build/macosx-debug/Makefile
	$(call reconfigure-if-args-changed,build/macosx-debug,cmake $(MACOSX_DEBUG_CMAKE_ARGS))
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/macosx-debug/Makefile:
	cmake $(MACOSX_DEBUG_CMAKE_ARGS)

ensure-built/macosx-debug: build/macosx-debug

build/android-debug: build/android-debug/build.ninja
	cmake --build $@ --target deps -j $(ANDROID_JOBS)
	cmake --build $@ -j $(ANDROID_JOBS)
	$(MAKE) sync/android-libs BUILD_DIR=build/android-debug
	$(MAKE) sync/android-assets
	cd $(APK_DIR) && JAVA_HOME="$(JAVA_HOME)" ANDROID_HOME="$(ANDROID_SDK)" ./gradlew assembleDebug

build/android-debug/build.ninja:
	cmake -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake \
		-DANDROID_ABI=$(ANDROID_ABI) \
		-DANDROID_PLATFORM=$(ANDROID_PLATFORM) \
		-DANDROID_STL=$(ANDROID_STL) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=android \
		-DBUILD_TESTING=OFF \
		$(CMAKE_EXTRA_ARGS) \
		-B build/android-debug -S .

build/ios: build/ios/EVEngine.xcodeproj
	cmake --build build/ios --target deps -j $(ANDROID_JOBS)
	@if [ -z "$(IOS_DEVELOPMENT_TEAM)" ]; then \
		echo "WARNING: IOS_DEVELOPMENT_TEAM unset; building unsigned (install will fail)"; \
		cd build/ios && xcodebuild -scheme eve -configuration Release \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
			build; \
	else \
		cd build/ios && xcodebuild -scheme eve -configuration Release \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
			CODE_SIGN_STYLE=Automatic \
			-allowProvisioningUpdates \
			build; \
	fi

build/ios/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=$(IOS_SDK) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=OFF \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_GAME=$(IOS_GAME) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios -S .

build/ios-debug: build/ios-debug/EVEngine.xcodeproj
	cmake --build build/ios-debug --target deps -j $(ANDROID_JOBS)
	@if [ -z "$(IOS_DEVELOPMENT_TEAM)" ]; then \
		echo "WARNING: IOS_DEVELOPMENT_TEAM unset; building unsigned (install will fail)"; \
		echo "  Xcode → Settings → Accounts → add Apple ID, then export IOS_DEVELOPMENT_TEAM=<TeamID>"; \
		cd build/ios-debug && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
			build; \
	else \
		cd build/ios-debug && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
			CODE_SIGN_STYLE=Automatic \
			-allowProvisioningUpdates \
			build; \
	fi

build/ios-debug/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=$(IOS_SDK) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=OFF \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_GAME=$(IOS_GAME) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-debug -S .

# iOS game app for the simulator (no signing required). Uses its own
# third-party tree (ios-simulator-debug) so the device deps are not clobbered.
build/ios-sim-debug: build/ios-sim-debug/EVEngine.xcodeproj
	cmake --build build/ios-sim-debug --target deps -j $(ANDROID_JOBS)
	cd build/ios-sim-debug && xcodebuild -scheme eve -configuration Debug \
		-sdk iphonesimulator -arch $(IOS_ARCH) \
		CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
		build

build/ios-sim-debug/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=iphonesimulator \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=OFF \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_GAME=$(IOS_GAME) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-sim-debug -S .

# ---- iOS test app (zeroerr suite as the eve.app executable) ----
#   make build/ios-debug-test [IOS_DEVELOPMENT_TEAM=<TeamID>]
#   make run/ios-test-debug [FILTER=math.*]   # install + launch on device
#   make log/ios-test                          # stream test results
build/ios-debug-test: build/ios-debug-test/EVEngine.xcodeproj
	cmake --build build/ios-debug-test --target deps -j $(ANDROID_JOBS)
	@if [ -z "$(IOS_DEVELOPMENT_TEAM)" ]; then \
		echo "WARNING: IOS_DEVELOPMENT_TEAM unset; building unsigned (install will fail)"; \
		cd build/ios-debug-test && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
			build; \
	else \
		cd build/ios-debug-test && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
			CODE_SIGN_STYLE=Automatic \
			-allowProvisioningUpdates \
			build; \
	fi

build/ios-debug-test/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=$(IOS_SDK) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=ON \
		-DEVENGINE_IOS_TEST_APP=ON \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_TEST_BUNDLE_ID=$(IOS_TEST_BUNDLE_ID) \
		-DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF \
		-DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF \
		-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-debug-test -S .

# iOS test app for the simulator (no signing required). Uses its own
# third-party tree (ios-simulator-debug) so the device deps are not clobbered.
build/ios-sim-debug-test: build/ios-sim-debug-test/EVEngine.xcodeproj
	cmake --build build/ios-sim-debug-test --target deps -j $(ANDROID_JOBS)
	cd build/ios-sim-debug-test && xcodebuild -scheme eve -configuration Debug \
		-sdk iphonesimulator -arch $(IOS_ARCH) \
		CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY=- CODE_SIGNING_ALLOWED=YES \
		build

build/ios-sim-debug-test/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=iphonesimulator \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=ON \
		-DEVENGINE_IOS_TEST_APP=ON \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_TEST_BUNDLE_ID=$(IOS_TEST_BUNDLE_ID) \
		-DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF \
		-DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF \
		-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-sim-debug-test -S .

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

# Copy native shared libraries into the Gradle jniLibs tree.
# In Android test-app mode the runner lives in test/libmain.so (zeroerr suite);
# otherwise it is the engine's src/engine/libmain.so.
sync/android-libs:
	mkdir -p $(JNI_LIBS)
	@if [ -f $(BUILD_DIR)/test/libmain.so ]; then \
	  echo "syncing test runner libmain.so"; \
	  cp -f $(BUILD_DIR)/test/libmain.so $(JNI_LIBS)/; \
	else \
	  cp -f $(BUILD_DIR)/src/engine/libmain.so $(JNI_LIBS)/; \
	fi
	@TP_DIR=build/third-party-binary/$$(basename $(BUILD_DIR)); \
	  if [ ! -d "$$TP_DIR/lib" ]; then TP_DIR=build/third-party-binary/android-debug; fi; \
	  if [ ! -d "$$TP_DIR/lib" ]; then TP_DIR=build/third-party-binary/android; fi; \
	  cp -f "$$TP_DIR/lib/libSDL2.so" $(JNI_LIBS)/; \
	  if [ -f "$$TP_DIR/lib/libhidapi.so" ]; then cp -f "$$TP_DIR/lib/libhidapi.so" $(JNI_LIBS)/; fi
	cp -f "$(ANDROID_NDK)/toolchains/llvm/prebuilt/"*"/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" $(JNI_LIBS)/
	@echo "Synced native libs -> $(JNI_LIBS)"
	ls -la $(JNI_LIBS)

# Populate APK assets/game from a source tree (do not hand-edit assets/game).
#   make sync/android-assets                 # default: demo shell
#   make sync/android-assets ANDROID_GAME=example
sync/android-assets:
	@rm -rf "$(ANDROID_ASSETS_GAME)"
	@mkdir -p "$(ANDROID_ASSETS_GAME)"
	@if [ "$(ANDROID_GAME)" = "example" ]; then \
	  src="example"; \
	elif [ "$(ANDROID_GAME)" = "demo" ]; then \
	  src="platform/android/game-shell"; \
	elif [ -d "$(ANDROID_GAME)" ]; then \
	  src="$(ANDROID_GAME)"; \
	else \
	  echo "ANDROID_GAME must be 'demo', 'example', or a directory (got: $(ANDROID_GAME))"; \
	  exit 1; \
	fi; \
	cp -R "$$src"/. "$(ANDROID_ASSETS_GAME)/"; \
	touch "$(ANDROID_ASSETS_GAME)/.gitkeep"; \
	echo "Synced Android game assets ($$src) -> $(ANDROID_ASSETS_GAME)"; \
	ls -la "$(ANDROID_ASSETS_GAME)"

# Populate APK assets/test + assets/examples for the Android test app
# (EVTestActivity unpacks them to <files>/evengine_test at first launch).
sync/android-test-assets:
	@rm -rf "$(APK_DIR)/app/src/main/assets/test" "$(APK_DIR)/app/src/main/assets/examples" "$(APK_DIR)/app/src/main/assets/game"
	@mkdir -p "$(APK_DIR)/app/src/main/assets/test" "$(APK_DIR)/app/src/main/assets/examples"
	cp -R test/. "$(APK_DIR)/app/src/main/assets/test/"
	cp -R examples/. "$(APK_DIR)/app/src/main/assets/examples/"
	@echo "Synced Android test assets -> $(APK_DIR)/app/src/main/assets/{test,examples}"
	ls -la "$(APK_DIR)/app/src/main/assets/test" | head -20

install/android-debug:
	$(ANDROID_SDK)/platform-tools/adb install -r \
		$(APK_DIR)/app/build/outputs/apk/debug/app-debug.apk

run/android-debug: install/android-debug
	$(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVEngineActivity

log/android:
	$(ANDROID_SDK)/platform-tools/adb logcat -s EVEngineActivity:I SDL:V SDL/APP:V vulkan:V libc:F DEBUG:F

# ---- Android test app (zeroerr suite as libmain.so) ----
#   make build/android-debug-test        # configure + deps + engine/tests + APK
#   make run/android-test-debug [FILTER=math.*]   # install + launch on device
#   make log/android-test                # stream test results from logcat
build/android-debug-test: build/android-debug-test/build.ninja
	cmake --build $@ --target deps -j $(ANDROID_JOBS)
	cmake --build $@ -j $(ANDROID_JOBS)
	$(MAKE) sync/android-libs BUILD_DIR=build/android-debug-test
	$(MAKE) sync/android-test-assets
	cd $(APK_DIR) && JAVA_HOME="$(JAVA_HOME)" ANDROID_HOME="$(ANDROID_SDK)" ./gradlew assembleDebug

build/android-debug-test/build.ninja:
	cmake -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake \
		-DANDROID_ABI=$(ANDROID_ABI) \
		-DANDROID_PLATFORM=$(ANDROID_PLATFORM) \
		-DANDROID_STL=$(ANDROID_STL) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=android \
		-DBUILD_TESTING=ON \
		-DEVENGINE_ANDROID_TEST_APP=ON \
		-DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF \
		-DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF \
		-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF \
		$(CMAKE_EXTRA_ARGS) \
		-B build/android-debug-test -S .

# Convenience: reuse the game APK's install/run with the test filter extra.
FILTER ?=
run/android-test-debug: install/android-debug
	@if [ -n "$(FILTER)" ]; then \
	  $(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVTestActivity --es evengine.test.filter "$(FILTER)"; \
	else \
	  $(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVTestActivity; \
	fi

log/android-test:
	$(ANDROID_SDK)/platform-tools/adb logcat -c
	$(ANDROID_SDK)/platform-tools/adb logcat -s EVEngineTest:V SDL:V vulkan:V libc:F DEBUG:F

# Install Debug .app to the first connected physical iOS/iPadOS device.
install/ios-debug:
	@test -n "$$(security find-identity -v -p codesigning 2>/dev/null | sed -n '/Apple Development/p')" \
	  || (echo "No Apple Development signing identity. Open Xcode → Settings → Accounts, add Apple ID, then rebuild with IOS_DEVELOPMENT_TEAM=<TeamID>."; exit 1)
	@APP="$(IOS_APP)"; \
	  if [ ! -d "$$APP" ]; then \
	    APP=$$(find build/ios-debug -name 'eve.app' -type d 2>/dev/null | head -1); \
	  fi; \
	  test -n "$$APP" -a -d "$$APP" || (echo "eve.app not found; run make build/ios-debug first"; exit 1); \
	  echo "Installing $$APP"; \
	  if xcrun --find devicectl >/dev/null 2>&1; then \
	    DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	    test -n "$$DEV" || (echo "No connected/available iOS device found (devicectl)."; exit 1); \
	    echo "Device $$DEV"; \
	    xcrun devicectl device install app --device $$DEV "$$APP"; \
	  elif command -v ios-deploy >/dev/null 2>&1; then \
	    ios-deploy --bundle "$$APP"; \
	  else \
	    echo "Need xcrun devicectl (Xcode 15+) or ios-deploy"; exit 1; \
	  fi

run/ios-debug: install/ios-debug
	@if xcrun --find devicectl >/dev/null 2>&1; then \
	  DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	  xcrun devicectl device process launch --device $$DEV $(IOS_BUNDLE_ID); \
	elif command -v ios-deploy >/dev/null 2>&1; then \
	  ios-deploy --justlaunch --bundle "$$(find build/ios-debug -name 'eve.app' -type d | head -1)"; \
	fi

log/ios:
	@echo "Streaming device logs (Ctrl-C to stop)..."
	@if xcrun --find devicectl >/dev/null 2>&1; then \
	  xcrun devicectl device info log --help >/dev/null 2>&1 || true; \
	  log stream --predicate 'processImagePath CONTAINS "eve" OR process CONTAINS "EVEngine"' --style compact; \
	else \
	  log stream --predicate 'processImagePath CONTAINS "eve"' --style compact; \
	fi

# ---- iOS test app: install / launch / logs (mirrors run/android-test-debug) ----
install/ios-test-debug:
	@test -n "$$(security find-identity -v -p codesigning 2>/dev/null | sed -n '/Apple Development/p')" \
	  || (echo "No Apple Development signing identity. Open Xcode → Settings → Accounts, add Apple ID, then rebuild with IOS_DEVELOPMENT_TEAM=<TeamID>."; exit 1)
	@APP="$(IOS_TEST_APP)"; \
	  if [ ! -d "$$APP" ]; then \
	    APP=$$(find build/ios-debug-test -name 'eve.app' -type d 2>/dev/null | head -1); \
	  fi; \
	  test -n "$$APP" -a -d "$$APP" || (echo "eve.app not found; run make build/ios-debug-test first"; exit 1); \
	  echo "Installing $$APP"; \
	  if xcrun --find devicectl >/dev/null 2>&1; then \
	    DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	    test -n "$$DEV" || (echo "No connected/available iOS device found (devicectl)."; exit 1); \
	    echo "Device $$DEV"; \
	    xcrun devicectl device install app --device $$DEV "$$APP"; \
	  elif command -v ios-deploy >/dev/null 2>&1; then \
	    ios-deploy --bundle "$$APP"; \
	  else \
	    echo "Need xcrun devicectl (Xcode 15+) or ios-deploy"; exit 1; \
	  fi

run/ios-test-debug: install/ios-test-debug
	@if xcrun --find devicectl >/dev/null 2>&1; then \
	  DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	  if [ -n "$(FILTER)" ]; then \
	    xcrun devicectl device process launch --terminate-existing --console --device $$DEV $(IOS_TEST_BUNDLE_ID) -- -evengine.test.filter "$(FILTER)"; \
	  else \
	    xcrun devicectl device process launch --terminate-existing --console --device $$DEV $(IOS_TEST_BUNDLE_ID); \
	  fi; \
	elif command -v ios-deploy >/dev/null 2>&1; then \
	  if [ -n "$(FILTER)" ]; then \
	    ios-deploy --justlaunch --bundle "$$(find build/ios-debug-test -name 'eve.app' -type d | head -1)" --args "-evengine.test.filter $(FILTER)"; \
	  else \
	    ios-deploy --justlaunch --bundle "$$(find build/ios-debug-test -name 'eve.app' -type d | head -1)"; \
	  fi; \
	else \
	  echo "Need xcrun devicectl (Xcode 15+) or ios-deploy"; exit 1; \
	fi

# Run the full suite on a device, one test file per app launch. Per-file
# process isolation mirrors CI's per-case CTest isolation: rapid window
# create/destroy across hundreds of tests trips the iOS watchdog (SIGKILL), so
# a crash in one file only loses that file's results.
IOS_TEST_RESULTS ?= build/ios-test-results
run/ios-test-all-debug: install/ios-test-debug
	@mkdir -p "$(IOS_TEST_RESULTS)"; rm -f "$(IOS_TEST_RESULTS)"/*.log; \
	DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	test -n "$$DEV" || (echo "No connected/available iOS device found (devicectl)."; exit 1); \
	ran=0; \
	for f in test/*.cpp; do \
	  b=$$(basename "$$f"); \
	  [ "$$b" = "main.cpp" ] && continue; \
	  ran=$$((ran+1)); \
	  echo "== $$b =="; \
	  xcrun devicectl --timeout 300 device process launch --terminate-existing --console --device "$$DEV" $(IOS_TEST_BUNDLE_ID) -- -evengine.test.file "$$b" > "$(IOS_TEST_RESULTS)/$${b%.cpp}.log" 2>&1 || true; \
	done; \
	echo "Ran $$ran test files; logs in $(IOS_TEST_RESULTS)/"; \
	echo "--- per-file results ---"; \
	passed=0; crashed=0; failed=0; \
	for l in "$(IOS_TEST_RESULTS)"/*.log; do \
	  b=$$(basename "$$l" .log); \
	  if grep -aq "signal" "$$l"; then \
	    echo "CRASH $$b ($$(grep -aoE 'signal [0-9]+' "$$l" | head -1))"; \
	    crashed=$$((crashed+1)); \
	  elif grep -aq "❌" "$$l"; then \
	    echo "FAIL  $$b"; \
	    failed=$$((failed+1)); \
	  else \
	    echo "PASS  $$b"; \
	    passed=$$((passed+1)); \
	  fi; \
	done; \
	echo "--- summary: PASS=$$passed FAIL=$$failed CRASH=$$crashed ---"

log/ios-test:
	@echo "Streaming iOS test logs (Ctrl-C to stop)..."
	log stream --predicate 'subsystem == "$(IOS_TEST_BUNDLE_ID)"' --style compact

# ---- iOS simulator test app: install / launch / logs (no signing needed) ----
install/ios-sim-test-debug:
	@APP="$(IOS_SIM_TEST_APP)"; \
	  if [ ! -d "$$APP" ]; then \
	    APP=$$(find build/ios-sim-debug-test -name 'eve.app' -type d 2>/dev/null | head -1); \
	  fi; \
	  test -n "$$APP" -a -d "$$APP" || (echo "eve.app not found; run make build/ios-sim-debug-test first"; exit 1); \
	  SIM=$$(xcrun simctl list devices available 2>/dev/null | sed -n 's/.*(\([0-9A-Fa-f-]\{36\}\)).*/\1/p' | head -1); \
	  test -n "$$SIM" || (echo "No available iOS simulator"; exit 1); \
	  xcrun simctl boot "$$SIM" 2>/dev/null || true; \
	  xcrun simctl bootstatus "$$SIM" -b >/dev/null 2>&1; \
	  echo "Installing $$APP on simulator $$SIM"; \
	  xcrun simctl install "$$SIM" "$$APP"

run/ios-sim-test-debug: install/ios-sim-test-debug
	@SIM=$$(xcrun simctl list devices available 2>/dev/null | sed -n 's/.*(\([0-9A-Fa-f-]\{36\}\)).*/\1/p' | head -1); \
	  if [ -n "$(FILTER)" ]; then \
	    xcrun simctl launch "$$SIM" $(IOS_TEST_BUNDLE_ID) -evengine.test.filter "$(FILTER)"; \
	  else \
	    xcrun simctl launch "$$SIM" $(IOS_TEST_BUNDLE_ID); \
	  fi

log/ios-sim-test:
	@echo "Streaming simulator test logs (Ctrl-C to stop)..."
	xcrun simctl spawn booted log stream --predicate 'subsystem == "$(IOS_TEST_BUNDLE_ID)"' --style compact

test: test/$(PLATFORM)-debug

# Fetch large classic render scenes for ClassicScenes.* tests
# (Sponza, DamagedHelmet, FlightHelmet, BoomBox, WaterBottle, Corset, …).
# Cornell Box is committed under test/assets/classic/cornell/.
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

# Default: run tests per case (process-isolated; this is the fast path on CI —
# main runs 1526 cases in ~2-11 min).  "bundle/<file>" entries stay registered
# by cmake/ZeroErrDiscoverTestsImpl.cmake as an opt-in: GPU/window tests were
# ~70x slower when several shared one process on CI, so bundles are excluded
# unless requested (FILTER=bundle/<file> or ctest -L bundle).
CTEST_RUN_SEL = $(if $(filter bundle/%,$(FILTER)),-L bundle,-E '^bundle/')

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
	$(CTEST_ENV) ctest --test-dir build/win32 -C Release --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

test/win32-debug: ensure-built/win32-debug
	$(CTEST_ENV) ctest --test-dir build/win32-debug --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

test/linux: ensure-built/linux
	$(CTEST_ENV) ctest --test-dir build/linux -C Release --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

test/linux-debug: ensure-built/linux-debug
	$(CTEST_ENV) ctest --test-dir build/linux-debug --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

# Sanitizer (ASan+UBSan) and coverage builds are opt-in Linux variants; CI
# uses them for the quality-gate jobs. Runtime env for tests:
#   ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
test/linux-asan:
	$(CTEST_ENV) ctest --test-dir build/linux-asan --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

test/linux-coverage:
	$(CTEST_ENV) ctest --test-dir build/linux-coverage --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

test/macosx: ensure-built/macosx
	$(CTEST_ENV) ctest --test-dir build/macosx -C Release --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

test/macosx-debug: ensure-built/macosx-debug
	$(CTEST_ENV) ctest --test-dir build/macosx-debug --output-on-failure -j $(CTEST_JOBS) $(CTEST_RUN_SEL) $(CTEST_FILTER) $(CTEST_REPEAT)

# Host-debug shortcut by test-name prefix, e.g. `make test/graphics.print`
# (explicit test/<platform> rules above take precedence over this pattern).
test/%: ensure-built/$(PLATFORM)-debug
	$(CTEST_ENV) ctest --test-dir build/$(PLATFORM)-debug --output-on-failure -j $(CTEST_JOBS) -R '^$(subst .,\.,$*)' $(CTEST_REPEAT)

# Host platform debug shortcut (same as run/$(PLATFORM)-debug).
run: run/$(PLATFORM)-debug

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
	@if [ -n "$(GAME)" ]; then cd $(GAME) && "$(CURDIR)/build/win32-debug/src/engine/eve.exe" run $(RUN_ARGS); \
	else build/win32-debug/src/engine/eve.exe $(RUN_ARGS); fi

run/linux-debug: ensure-built/linux-debug
	@if [ -n "$(GAME)" ]; then cd $(GAME) && "$(CURDIR)/build/linux-debug/src/engine/eve" run $(RUN_ARGS); \
	else build/linux-debug/src/engine/eve $(RUN_ARGS); fi

run/macosx-debug: ensure-built/macosx-debug
	@if [ -n "$(GAME)" ]; then cd $(GAME) && "$(CURDIR)/build/macosx-debug/src/engine/eve" run $(RUN_ARGS); \
	else build/macosx-debug/src/engine/eve $(RUN_ARGS); fi

run/win32: ensure-built/win32
	@if [ -n "$(GAME)" ]; then cd $(GAME) && "$(CURDIR)/build/win32/src/engine/eve.exe" run $(RUN_ARGS); \
	else build/win32/src/engine/eve.exe $(RUN_ARGS); fi

run/linux: ensure-built/linux
	@if [ -n "$(GAME)" ]; then cd $(GAME) && "$(CURDIR)/build/linux/src/engine/eve" run $(RUN_ARGS); \
	else build/linux/src/engine/eve $(RUN_ARGS); fi

run/macosx: ensure-built/macosx
	@if [ -n "$(GAME)" ]; then cd $(GAME) && "$(CURDIR)/build/macosx/src/engine/eve" run $(RUN_ARGS); \
	else build/macosx/src/engine/eve $(RUN_ARGS); fi

tools/debug:
	cd tools/vscode-eve-debug && npx @vscode/vsce package 
	cursor --install-extension ./tools/vscode-eve-debug/*.vsix

# Target-platform SDK install (independent prefix per plat; for publishing games TO that plat).
# Release: make sdk/macosx  → builds build/macosx then dist/eve-sdk/macosx
# Debug:   make sdk/macosx-debug
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
	@plat=$@; plat=$${plat#sdk/}; \
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

# make: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cmake -H./src -B./src/build && cmake --build src/build --parallel 8"

# deps: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cd src/third-party && bash build.sh"

# start: build/.build-docker
# 	$(DOCKER_START) /bin/bash

# build/.build-docker: Dockerfile
# 	docker build . --tag evengine
# 	mkdir -p build && touch $@
