.PHONY: all build/win32 build/linux build/win32-debug build/linux-debug

debug: build/win32-debug build/linux-debug 
release: build/win32 build/linux

build/win32: build/win32/EVEngine.sln
	cmake.exe --build $@ --config Release -j 32

build/win32/EVEngine.sln:
	cmake.exe -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release -B build/win32 -S .

build/linux: build/linux/Makefile
	cmake --build $@ -j 32

build/linux/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release  -B build/linux -S .

build/win32-debug: build/win32-debug/EVEngine.sln
	cmake.exe --build $@ --config Debug -j 32

build/win32-debug/EVEngine.sln:
	cmake.exe -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug -B build/win32-debug -S .

build/linux-debug: build/linux-debug/Makefile
	cmake --build $@ -j 32

build/linux-debug/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Debug  -B build/linux-debug -S .

# build/uwp:
# 	cmake.exe -G "Visual Studio 17 2022" -B $@ -S . -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10
# 	cmake.exe --build $@ --config $(BUILD_TYPE) -j 32

# build/android:

test/win32: build/win32
	build/win32/test/Release/unit_test.exe

test/win32-debug: build/win32-debug
	build/win32-debug/test/Debug/unit_test.exe

test/linux: build/linux
	build/linux/test/unit_test

test/linux-debug: build/linux-debug
	build/linux-debug/test/unit_test

	