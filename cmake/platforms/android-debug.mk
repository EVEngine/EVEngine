build/android-debug: build/android-debug/build.ninja
	cmake --build $@ --target deps -j $(ANDROID_JOBS)
	cmake --build $@ -j $(ANDROID_JOBS)
	$(MAKE) sync/android-libs BUILD_DIR=build/android-debug
	$(MAKE) sync/android-assets
	cd $(APK_DIR) && JAVA_HOME="$(JAVA_HOME)" ANDROID_HOME="$(ANDROID_SDK)" ./gradlew assembleDebug

build/android-debug/build.ninja:
	cmake -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK)/build/cmake/android.toolchain.cmake \
		-DANDROID_ABI=$(ANDROID_ABI) \
		-DANDROID_PLATFORM=$(ANDROID_PLATFORM) \
		-DANDROID_STL=$(ANDROID_STL) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=android \
		-DBUILD_TESTING=OFF \
		$(CMAKE_EXTRA_ARGS) \
		-B build/android-debug -S .

