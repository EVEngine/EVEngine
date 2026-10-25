build/macosx-debug: build/macosx-debug/Makefile
	$(call reconfigure-if-args-changed,build/macosx-debug,cmake $(MACOSX_DEBUG_CMAKE_ARGS))
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/macosx-debug/Makefile:
	cmake $(MACOSX_DEBUG_CMAKE_ARGS)

ensure-built/macosx-debug: build/macosx-debug

