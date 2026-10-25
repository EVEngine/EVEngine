# Copy native shared libraries into the Gradle jniLibs tree.
# In Android test-app mode the runner lives in test/libmain.so (zeroerr suite);
# otherwise it is the engine's src/engine/libmain.so.
sync/android-libs:
	cmake -DBUILD_DIR="$(BUILD_DIR)" -DJNI_LIBS="$(JNI_LIBS)" -DANDROID_ABI="$(ANDROID_ABI)" -DANDROID_NDK="$(ANDROID_NDK)" -P cmake/sync_android_libs.cmake

# Populate APK assets/game from a source tree (do not hand-edit assets/game).
#   make sync/android-assets                 # default: demo shell
#   make sync/android-assets ANDROID_GAME=example
sync/android-assets:
	@rm -rf "$(ANDROID_ASSETS_GAME)"
	@mkdir -p "$(ANDROID_ASSETS_GAME)"
	@if [ "$(ANDROID_GAME)" = "example" ]; then \
	  src="example"; \
	elif [ "$(ANDROID_GAME)" = "demo" ]; then \
	  src="platform/android/game-shell"; \
	elif [ -d "$(ANDROID_GAME)" ]; then \
	  src="$(ANDROID_GAME)"; \
	else \
	  echo "ANDROID_GAME must be 'demo', 'example', or a directory (got: $(ANDROID_GAME))"; \
	  exit 1; \
	fi; \
	cp -R "$$src"/. "$(ANDROID_ASSETS_GAME)/"; \
	touch "$(ANDROID_ASSETS_GAME)/.gitkeep"; \
	echo "Synced Android game assets ($$src) -> $(ANDROID_ASSETS_GAME)"; \
	ls -la "$(ANDROID_ASSETS_GAME)"

# Populate APK assets/test + assets/examples for the Android test app
# (EVTestActivity unpacks them to <files>/evengine_test at first launch).
sync/android-test-assets:
	@rm -rf "$(APK_DIR)/app/src/main/assets/test" "$(APK_DIR)/app/src/main/assets/examples" "$(APK_DIR)/app/src/main/assets/game"
	@mkdir -p "$(APK_DIR)/app/src/main/assets/test" "$(APK_DIR)/app/src/main/assets/examples"
	cp -R test/. "$(APK_DIR)/app/src/main/assets/test/"
	cp -R examples/. "$(APK_DIR)/app/src/main/assets/examples/"
	@echo "Synced Android test assets -> $(APK_DIR)/app/src/main/assets/{test,examples}"
	ls -la "$(APK_DIR)/app/src/main/assets/test" | head -20

install/android-debug:
	$(ANDROID_SDK)/platform-tools/adb install -r \
		$(APK_DIR)/app/build/outputs/apk/debug/app-debug.apk

run/android-debug: install/android-debug
	$(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVEngineActivity

log/android:
	$(ANDROID_SDK)/platform-tools/adb logcat -s EVEngineActivity:I SDL:V SDL/APP:V vulkan:V libc:F DEBUG:F

# ---- Android test app (zeroerr suite as libmain.so) ----
#   make build/android-debug-test        # configure + deps + engine/tests + APK
#   make run/android-test-debug [FILTER=math.*]   # install + launch on device
#   make log/android-test                # stream test results from logcat
build/android-debug-test: build/android-debug-test/build.ninja
	cmake --build $@ --target deps -j $(ANDROID_JOBS)
	cmake --build $@ -j $(ANDROID_JOBS)
	$(MAKE) sync/android-libs BUILD_DIR=build/android-debug-test
	$(MAKE) sync/android-test-assets
	cd $(APK_DIR) && JAVA_HOME="$(JAVA_HOME)" ANDROID_HOME="$(ANDROID_SDK)" ./gradlew assembleDebug

build/android-debug-test/build.ninja:
	cmake -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake \
		-DANDROID_ABI=$(ANDROID_ABI) \
		-DANDROID_PLATFORM=$(ANDROID_PLATFORM) \
		-DANDROID_STL=$(ANDROID_STL) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=android \
		-DBUILD_TESTING=ON \
		-DEVENGINE_ANDROID_TEST_APP=ON \
		-DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF \
		-DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF \
		-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF \
		$(CMAKE_EXTRA_ARGS) \
		-B build/android-debug-test -S .

# Convenience: reuse the game APK's install/run with the test filter extra.
FILTER ?=
run/android-test-debug: install/android-debug
	@if [ -n "$(FILTER)" ]; then \
	  $(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVTestActivity --es evengine.test.filter "$(FILTER)"; \
	else \
	  $(ANDROID_SDK)/platform-tools/adb shell am start -n com.evengine.example/.EVTestActivity; \
	fi

log/android-test:
	$(ANDROID_SDK)/platform-tools/adb logcat -c
	$(ANDROID_SDK)/platform-tools/adb logcat -s EVEngineTest:V SDL:V vulkan:V libc:F DEBUG:F

