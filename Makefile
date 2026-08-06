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
# demo  = platform/ios/game-shell (no main.nut → embedded eve.demoScript + particles)
# example = copy from example/ (box + fire emitter)
IOS_GAME ?= demo
VULKAN_SDK ?= $(shell ls -d $(HOME)/VulkanSDK/*/ 2>/dev/null | sort -V | tail -1 | sed 's:/*$$::')
IOS_APP ?= build/ios-debug/src/engine/Debug-iphoneos/eve.app
ifeq ($(wildcard $(IOS_APP)),)
IOS_APP = build/ios-debug/src/engine/eve.app
endif

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
	HAS_WSL := $(shell wsl -e true >/dev/null 2>&1 && echo 1 || echo 0)
endif

ALL_DEBUG_TARGETS := build/$(PLATFORM)-debug
ifeq ($(HAS_IOS),1)
	ALL_DEBUG_TARGETS += build/ios-debug
endif
ifeq ($(HAS_ANDROID),1)
	ALL_DEBUG_TARGETS += build/android-debug
endif
ifeq ($(HAS_WSL),1)
	ALL_DEBUG_TARGETS += wsl/linux-debug
endif

# Desktop game directory for run/xxx (default: example/).
GAME ?= example

.PHONY: all build/win32 build/linux build/macosx build/android build/ios \
	build/win32-debug build/linux-debug build/macosx-debug build/android-debug build/ios-debug \
	wsl/linux wsl/linux-debug show-targets \
	debug release example \
	run run/win32 run/linux run/macosx \
	run/win32-debug run/linux-debug run/macosx-debug \
	sync/android-libs sync/android-assets install/android-debug run/android-debug log/android \
	install/ios-debug run/ios-debug log/ios \
	reinstall/third-party reinstall/third-party/win32 reinstall/third-party/win32-debug \
	reinstall/third-party/linux reinstall/third-party/linux-debug \
	reinstall/third-party/macosx reinstall/third-party/macosx-debug \
	reinstall/third-party/android reinstall/third-party/android-debug \
	reinstall/third-party/ios reinstall/third-party/ios-debug \
	link-compile-commands

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
	@echo "run -> run/$(PLATFORM)-debug (GAME=$(GAME))"

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

build/win32: build/win32/EVEngine.sln
	cmake.exe --build $@ --config Release -j 32

build/win32/EVEngine.sln:
	cmake.exe -G "Visual Studio 18 2026" -DCMAKE_BUILD_TYPE=Release -A x64 -B build/win32 -S .

build/linux: build/linux/Makefile
	cmake --build $@ -j 32

build/linux/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=linux -B build/linux -S .

build/macosx: build/macosx/Makefile
	cmake --build $@ -j 32

build/macosx/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_PLATFORM=macosx -B build/macosx -S .

build/android: build/android/build.ninja
	cmake --build $@ --target deps -j 8
	cmake --build $@ -j 8
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
		-B build/android -S .

# win32-debug: Ninja + MSVC cl；via cmake/with-msvc.cmd (vcvars) so STL headers resolve
# Emits build/win32-debug/compile_commands.json with MSVC INCLUDE paths for clangd
WITH_MSVC = cmake\with-msvc.cmd

build/win32-debug: build/win32-debug/build.ninja
	$(WITH_MSVC) cmake.exe --build $@ -j 32

build/win32-debug/build.ninja:
	$(WITH_MSVC) cmake.exe -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -B build/win32-debug -S .

build/linux-debug: build/linux-debug/Makefile
	cmake --build $@ -j 32

build/linux-debug/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=linux -B build/linux-debug -S .

build/macosx-debug: build/macosx-debug/Makefile
	cmake --build $@ -j 32

build/macosx-debug/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLATFORM=macosx -B build/macosx-debug -S .

build/android-debug: build/android-debug/build.ninja
	cmake --build $@ --target deps -j 8
	cmake --build $@ -j 8
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
		-B build/android-debug -S .

build/ios: build/ios/EVEngine.xcodeproj
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
		-B build/ios -S .

build/ios-debug: build/ios-debug/EVEngine.xcodeproj
	cmake --build build/ios-debug --target deps -j 8
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
		-B build/ios-debug -S .

# Rebuild changed third-party sources and run install again without deleting
# build/third-party or build/third-party-binary.
reinstall/third-party: reinstall/third-party/$(PLATFORM)-debug

reinstall/third-party/win32:
	cmake.exe --build build/third-party/win32 --target install --config Release -j 32

reinstall/third-party/win32-debug:
	$(WITH_MSVC) cmake.exe --build build/third-party/win32-debug --target install -j 32

reinstall/third-party/linux:
	cmake --build build/third-party/linux --target install -j 32

reinstall/third-party/linux-debug:
	cmake --build build/third-party/linux-debug --target install -j 32

reinstall/third-party/macosx:
	cmake --build build/third-party/macosx --target install -j 32

reinstall/third-party/macosx-debug:
	cmake --build build/third-party/macosx-debug --target install -j 32

reinstall/third-party/android:
	cmake --build build/third-party/android --target install -j 8

reinstall/third-party/android-debug:
	cmake --build build/third-party/android-debug --target install -j 8

reinstall/third-party/ios:
	cmake --build build/third-party/ios --target install -j 8

reinstall/third-party/ios-debug:
	cmake --build build/third-party/ios-debug --target install -j 8

# Copy native shared libraries into the Gradle jniLibs tree.
sync/android-libs:
	mkdir -p $(JNI_LIBS)
	cp -f $(BUILD_DIR)/src/engine/libmain.so $(JNI_LIBS)/
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

install/android-debug:
	$(ANDROID_SDK)/platform-tools/adb install -r \
		$(APK_DIR)/app/build/outputs/apk/debug/app-debug.apk

run/android-debug: install/android-debug
	$(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVEngineActivity

log/android:
	$(ANDROID_SDK)/platform-tools/adb logcat -s EVEngineActivity:I SDL:V SDL/APP:V vulkan:V libc:F DEBUG:F

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
	    DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -n 's/.*\([0-9A-Fa-f]\{8\}-[0-9A-Fa-f]\{4\}-[0-9A-Fa-f]\{4\}-[0-9A-Fa-f]\{4\}-[0-9A-Fa-f]\{12\}\).*connected.*/\1/p' | head -1); \
	    test -n "$$DEV" || (echo "No connected iOS device found (devicectl)."; exit 1); \
	    echo "Device $$DEV"; \
	    xcrun devicectl device install app --device $$DEV "$$APP"; \
	  elif command -v ios-deploy >/dev/null 2>&1; then \
	    ios-deploy --bundle "$$APP"; \
	  else \
	    echo "Need xcrun devicectl (Xcode 15+) or ios-deploy"; exit 1; \
	  fi

run/ios-debug: install/ios-debug
	@if xcrun --find devicectl >/dev/null 2>&1; then \
	  DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -n 's/.*\([0-9A-Fa-f]\{8\}-[0-9A-Fa-f]\{4\}-[0-9A-Fa-f]\{4\}-[0-9A-Fa-f]\{4\}-[0-9A-Fa-f]\{12\}\).*connected.*/\1/p' | head -1); \
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

test: test/$(PLATFORM)-debug

test/win32: 
	build/win32/test/Release/unit_test.exe

test/win32-debug:
	build/win32-debug/test/unit_test.exe

test/linux: 
	build/linux/test/unit_test

test/linux-debug: 
	build/linux-debug/test/unit_test

test/macosx:
	build/macosx/test/unit_test

test/macosx-debug:
	build/macosx-debug/test/unit_test

# Host platform debug shortcut (same as run/$(PLATFORM)-debug).
run: run/$(PLATFORM)-debug

# Desktop: run built eve against GAME/ (default example/).
#   make run/macosx-debug
#   make run/linux-debug GAME=.
#   make run              # current host platform, debug
run/win32-debug:
	cd $(GAME) && ../build/win32-debug/src/engine/eve.exe run

run/linux-debug:
	cd $(GAME) && ../build/linux-debug/src/engine/eve run

run/macosx-debug:
	cd $(GAME) && ../build/macosx-debug/src/engine/eve run

run/win32:
	cd $(GAME) && ../build/win32/src/engine/Release/eve.exe run

run/linux:
	cd $(GAME) && ../build/linux/src/engine/eve run

run/macosx:
	cd $(GAME) && ../build/macosx/src/engine/eve run

example: run/$(PLATFORM)-debug
# make: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cmake -H./src -B./src/build && cmake --build src/build --parallel 8"

# deps: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cd src/third-party && bash build.sh"

# start: build/.build-docker
# 	$(DOCKER_START) /bin/bash

# build/.build-docker: Dockerfile
# 	docker build . --tag evengine
# 	mkdir -p build && touch $@
