# Explicit native targets: no cross-toolchain or synthetic processor override.
define eve-native-arm64
.PHONY: build/$(1)-arm64$(2) deps/$(1)-arm64$(2) configure/$(1)-arm64$(2) ensure-built/$(1)-arm64$(2) test/$(1)-arm64$(2)
configure/$(1)-arm64$(2):
	$(3) cmake -G Ninja -DCMAKE_BUILD_TYPE=$(4) -DEVENGINE_BUILD_PLATFORM=$(1) -DEVENGINE_NATIVE_ARM64=ON $(5) $(CMAKE_EXTRA_ARGS) -B build/$(1)-arm64$(2) -S .
deps/$(1)-arm64$(2): configure/$(1)-arm64$(2)
	$(3) cmake --build build/$(1)-arm64$(2) --target deps -j $(JOBS)
build/$(1)-arm64$(2): deps/$(1)-arm64$(2)
	$(3) cmake --build $$@ -j $(JOBS)
ensure-built/$(1)-arm64$(2): build/$(1)-arm64$(2)
test/$(1)-arm64$(2): ensure-built/$(1)-arm64$(2)
	$$(CTEST_ENV) ctest --test-dir build/$(1)-arm64$(2) --output-on-failure -j $$(CTEST_JOBS) $$(CTEST_RUN_SEL) $$(CTEST_DOMAIN_SEL) $$(CTEST_LABEL_FILTER) $$(CTEST_FILTER) $$(CTEST_REPEAT)
endef

WITH_MSVC_ARM64 = : && cmake/with-msvc.cmd --arm64
$(eval $(call eve-native-arm64,win32,,$(WITH_MSVC_ARM64),Release,-DEVENGINE_MODULE_LINKAGE=OBJECT))
$(eval $(call eve-native-arm64,win32,-debug,$(WITH_MSVC_ARM64),Debug,$(DEBUG_STRICT_WARNINGS_FLAG)))
$(eval $(call eve-native-arm64,linux,,,Release,-DEVENGINE_MODULE_LINKAGE=OBJECT))
$(eval $(call eve-native-arm64,linux,-debug,,Debug,$(DEBUG_STRICT_WARNINGS_FLAG)))
