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

ANDROID_SDK ?= $(HOME)/Library/Android/sdk
ANDROID_NDK ?= $(ANDROID_SDK)/ndk/26.1.10909125
ANDROID_ABI ?= arm64-v8a
ANDROID_PLATFORM ?= android-24
ANDROID_STL ?= c++_shared
APK_DIR = platform/android/apk
JNI_LIBS = $(APK_DIR)/app/src/main/jniLibs/$(ANDROID_ABI)
JAVA_HOME ?= $(shell brew --prefix openjdk@17 2>/dev/null)/libexec/openjdk.jdk/Contents/Home
BUILD_DIR ?= build/android-debug

.PHONY: all build/win32 build/linux build/macosx build/android \
	build/win32-debug build/linux-debug build/macosx-debug build/android-debug \
	debug release example sync/android-libs install/android-debug run/android-debug log/android

debug: build/$(PLATFORM)-debug
release: build/$(PLATFORM)

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

install/android-debug:
	$(ANDROID_SDK)/platform-tools/adb install -r \
		$(APK_DIR)/app/build/outputs/apk/debug/app-debug.apk

run/android-debug: install/android-debug
	$(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVEngineActivity

log/android:
	$(ANDROID_SDK)/platform-tools/adb logcat -s EVEngineActivity:I SDL:V SDL/APP:V vulkan:V libc:F DEBUG:F

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

example:
ifeq ($(PLATFORM),win32)
	cd example && ../build/win32-debug/src/engine/eve.exe run
else ifeq ($(PLATFORM),macosx)
	cd example && ../build/macosx-debug/src/engine/eve run
else
	cd example && ../build/linux-debug/src/engine/eve run
endif
# make: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cmake -H./src -B./src/build && cmake --build src/build --parallel 8"

# deps: build/.build-docker
# 	$(DOCKER) /bin/bash -c "cd src/third-party && bash build.sh"

# start: build/.build-docker
# 	$(DOCKER_START) /bin/bash

# build/.build-docker: Dockerfile
# 	docker build . --tag evengine
# 	mkdir -p build && touch $@
