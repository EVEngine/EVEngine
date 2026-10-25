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

