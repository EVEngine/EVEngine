build/win32-debug: build/win32-debug/build.ninja
	$(call reconfigure-if-args-changed,build/win32-debug,$(WITH_MSVC) cmake.exe $(WIN32_DEBUG_CMAKE_ARGS),$(WIN32_DEBUG_CONFIG_STAMP))
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
	if [ ! -f build/win32-debug/test/test_src.txt ]; then stale=1; \
	else \
	  n_disk=0; \
	  for cpp in test/*.cpp $$(find test/unit test/integration -name '*.cpp' -type f); do \
	    [ -f "$$cpp" ] || continue; \
	    n_disk=$$((n_disk+1)); \
	    if [ "$$cpp" -nt build/win32-debug/test/test_src.txt ]; then stale=1; break; fi; \
	  done; \
	  n_list=$$(grep -c . build/win32-debug/test/test_src.txt 2>/dev/null || echo 0); \
	  if [ "$$stale" = 0 ] && [ "$$n_disk" != "$$n_list" ]; then stale=1; fi; \
	fi; \
	if [ "$$stale" = 1 ] && [ -f build/win32-debug/build.ninja ]; then \
	  echo "source list stale; reconfiguring build/win32-debug"; \
	  $(WITH_MSVC) cmake.exe $(WIN32_DEBUG_CMAKE_ARGS); \
	fi; \
	if [ -f build/win32-debug/build.ninja ] \
	   && [ "$$(cat build/win32-debug/.eve-config-args 2>/dev/null)" = "$(WIN32_DEBUG_CONFIG_STAMP)" ] \
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

