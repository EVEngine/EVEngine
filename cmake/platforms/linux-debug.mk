build/linux-debug: build/linux-debug/Makefile
	$(call reconfigure-if-args-changed,build/linux-debug,cmake $(LINUX_DEBUG_CMAKE_ARGS),$(LINUX_DEBUG_CONFIG_STAMP))
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/linux-debug/Makefile:
	cmake $(LINUX_DEBUG_CMAKE_ARGS)

ensure-built/linux-debug: build/linux-debug

build/linux-asan: build/linux-asan/Makefile
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

# Keep heap, stack and undefined-behavior instrumentation plus engine assertions,
# but omit per-global ASan redzones: the monolithic unit_test has enough globals
# for those redzones alone to exceed x86-64's PC-relative relocation range.
# Disabling linker relaxation also avoids GOTPCREL overflows near that limit.
build/linux-asan/Makefile:
	cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_PLATFORM=linux \
		-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer --param=asan-globals=0" \
		-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer --param=asan-globals=0" \
		-DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -g -DNDEBUG -fno-optimize-sibling-calls" \
		-DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O1 -g -DNDEBUG -fno-optimize-sibling-calls" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr -Wl,--no-relax" \
		-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr" \
		-DEVENGINE_ENABLE_ASSERTS=ON \
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

