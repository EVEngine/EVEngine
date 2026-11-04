build/win32: build/win32/build.ninja
	$(call reconfigure-if-args-changed,build/win32,$(WITH_MSVC) cmake.exe $(WIN32_CMAKE_ARGS))
	$(WITH_MSVC) cmake.exe --build $@ -j $(JOBS)

build/win32/build.ninja:
	$(WITH_MSVC) cmake.exe $(WIN32_CMAKE_ARGS)

# Fast up-to-date check for repeated run/test loops: ninja's dry run costs
# ~100ms and lists every pending edge, so vcvars + cmake only run when
# something is actually stale. check_sources_* rescan targets run on every
# build by design, so their lines are filtered out of the dry-run output.
# Do not use `test/ -nt unit_test.exe`: PRE_BUILD asset scripts and any write
# under test/ bump the directory mtime on Windows, which reconfigured CMake
# and rebuilt every test TU.
ensure-built/win32:
	@stale=0; \
	for f in build/win32/src/modules/*_src.txt build/win32/src/engine/*_src.txt; do \
	  [ -f "$$f" ] || continue; \
	  folder=$${f##*/}; folder=$${folder%_src.txt}; \
	  for src in src/modules/$$folder src/engine/$$folder; do \
	    if [ -d "$$src" ] && [ "$$src" -nt "$$f" ]; then stale=1; break 2; fi; \
	  done; \
	done; \
	if [ ! -f build/win32/test/test_src.txt ]; then stale=1; \
	else \
	  n_disk=0; \
	  for cpp in test/*.cpp $$(find test/unit test/integration -name '*.cpp' -type f); do \
	    [ -f "$$cpp" ] || continue; \
	    n_disk=$$((n_disk+1)); \
	    if [ "$$cpp" -nt build/win32/test/test_src.txt ]; then stale=1; break; fi; \
	  done; \
	  n_list=$$(grep -c . build/win32/test/test_src.txt 2>/dev/null || echo 0); \
	  if [ "$$stale" = 0 ] && [ "$$n_disk" != "$$n_list" ]; then stale=1; fi; \
	fi; \
	if [ "$$stale" = 1 ] && [ -f build/win32/build.ninja ]; then \
	  echo "source list stale; reconfiguring build/win32"; \
	  $(WITH_MSVC) cmake.exe $(WIN32_CMAKE_ARGS); \
	fi; \
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

