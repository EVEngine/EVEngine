build/macosx: build/macosx/Makefile
	$(call reconfigure-if-args-changed,build/macosx,cmake $(MACOSX_CMAKE_ARGS))
	cmake --build $@ --target deps -j $(JOBS)
	cmake --build $@ -j $(JOBS)

build/macosx/Makefile:
	cmake $(MACOSX_CMAKE_ARGS)

ensure-built/macosx: build/macosx

