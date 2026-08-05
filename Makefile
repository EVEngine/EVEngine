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

.PHONY: all build/win32 build/linux build/macosx build/win32-debug build/linux-debug build/macosx-debug debug release example

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

# build/uwp:
# 	cmake.exe -G "Visual Studio 17 2022" -B $@ -S . -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10
# 	cmake.exe --build $@ --config $(BUILD_TYPE) -j 32

# build/android:

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
