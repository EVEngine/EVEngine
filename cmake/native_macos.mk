# macOS architectures use independent engine, dependency and SDK trees.
# Floating-point std::format uses libc++ to_chars, available starting in macOS 13.3.
define eve-native-macos
.PHONY: configure/macosx-$(1)$(2) deps/macosx-$(1)$(2) build/macosx-$(1)$(2) ensure-built/macosx-$(1)$(2) test/macosx-$(1)$(2) sdk/macosx-$(1)$(2)
configure/macosx-$(1)$(2):
	cmake -G Ninja -DCMAKE_BUILD_TYPE=$(3) -DEVENGINE_BUILD_PLATFORM=macosx $(4) -DCMAKE_OSX_ARCHITECTURES=$(1) -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 $(CMAKE_EXTRA_ARGS) -B build/macosx-$(1)$(2) -S .
deps/macosx-$(1)$(2): configure/macosx-$(1)$(2)
	cmake --build build/macosx-$(1)$(2) --target deps -j $(JOBS)
build/macosx-$(1)$(2): deps/macosx-$(1)$(2)
	cmake --build $$@ -j $(JOBS)
ensure-built/macosx-$(1)$(2): build/macosx-$(1)$(2)
test/macosx-$(1)$(2): ensure-built/macosx-$(1)$(2)
	$$(CTEST_ENV) ctest --test-dir build/macosx-$(1)$(2) --output-on-failure -j $$(CTEST_JOBS) $$(CTEST_RUN_SEL) $$(CTEST_DOMAIN_SEL) $$(CTEST_LABEL_FILTER) $$(CTEST_FILTER) $$(CTEST_REPEAT)
sdk/macosx-$(1)$(2): build/macosx-$(1)$(2)
	cmake --install build/macosx-$(1)$(2) --prefix dist/eve-sdk/macosx-$(1)$(2)
endef

$(eval $(call eve-native-macos,x86_64,,Release,-DEVENGINE_NATIVE_MACOS_X86_64=ON))
$(eval $(call eve-native-macos,x86_64,-debug,Debug,-DEVENGINE_NATIVE_MACOS_X86_64=ON))
$(eval $(call eve-native-macos,arm64,,Release))
$(eval $(call eve-native-macos,arm64,-debug,Debug))
