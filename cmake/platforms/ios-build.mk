build/ios: build/ios/EVEngine.xcodeproj
	cmake --build build/ios --target deps --config Release -j $(ANDROID_JOBS)
	@if [ -z "$(IOS_DEVELOPMENT_TEAM)" ]; then \
		echo "WARNING: IOS_DEVELOPMENT_TEAM unset; building unsigned (install will fail)"; \
		cd build/ios && xcodebuild -scheme eve -configuration Release \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
			build; \
	else \
		cd build/ios && xcodebuild -scheme eve -configuration Release \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
			CODE_SIGN_STYLE=Automatic \
			-allowProvisioningUpdates \
			build; \
	fi

build/ios/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=$(IOS_SDK) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=OFF \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_GAME=$(IOS_GAME) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios -S .

build/ios-debug: build/ios-debug/EVEngine.xcodeproj
	cmake --build build/ios-debug --target deps --config Debug -j $(ANDROID_JOBS)
	@if [ -z "$(IOS_DEVELOPMENT_TEAM)" ]; then \
		echo "WARNING: IOS_DEVELOPMENT_TEAM unset; building unsigned (install will fail)"; \
		echo "  Xcode → Settings → Accounts → add Apple ID, then export IOS_DEVELOPMENT_TEAM=<TeamID>"; \
		cd build/ios-debug && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
			build; \
	else \
		cd build/ios-debug && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
			CODE_SIGN_STYLE=Automatic \
			-allowProvisioningUpdates \
			build; \
	fi

build/ios-debug/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=$(IOS_SDK) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=OFF \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_GAME=$(IOS_GAME) \
		$(DEBUG_LEVEL_FLAG) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-debug -S .

# iOS game app for the simulator (no signing required). Uses its own
# third-party tree (ios-simulator-debug) so the device deps are not clobbered.
build/ios-sim-debug: build/ios-sim-debug/EVEngine.xcodeproj
	cmake --build build/ios-sim-debug --target deps --config Debug -j $(ANDROID_JOBS)
	cd build/ios-sim-debug && xcodebuild -scheme eve -configuration Debug \
		-sdk iphonesimulator -arch $(IOS_ARCH) \
		CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
		build

build/ios-sim-debug/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=iphonesimulator \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=OFF \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_GAME=$(IOS_GAME) \
		$(DEBUG_LEVEL_FLAG) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-sim-debug -S .

# ---- iOS test app (zeroerr suite as the eve.app executable) ----
#   make build/ios-debug-test [IOS_DEVELOPMENT_TEAM=<TeamID>]
#   make run/ios-test-debug [FILTER=math.*]   # install + launch on device
#   make log/ios-test                          # stream test results
build/ios-debug-test: build/ios-debug-test/EVEngine.xcodeproj
	cmake --build build/ios-debug-test --target deps --config Debug -j $(ANDROID_JOBS)
	@if [ -z "$(IOS_DEVELOPMENT_TEAM)" ]; then \
		echo "WARNING: IOS_DEVELOPMENT_TEAM unset; building unsigned (install will fail)"; \
		cd build/ios-debug-test && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" \
			build; \
	else \
		cd build/ios-debug-test && xcodebuild -scheme eve -configuration Debug \
			-sdk $(IOS_SDK) -arch $(IOS_ARCH) \
			DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
			CODE_SIGN_STYLE=Automatic \
			-allowProvisioningUpdates \
			build; \
	fi

build/ios-debug-test/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=$(IOS_SDK) \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=ON \
		-DEVENGINE_IOS_TEST_APP=ON \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DIOS_DEVELOPMENT_TEAM=$(IOS_DEVELOPMENT_TEAM) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_TEST_BUNDLE_ID=$(IOS_TEST_BUNDLE_ID) \
		-DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF \
		-DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF \
		-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF \
		$(DEBUG_LEVEL_FLAG) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-debug-test -S .

# iOS test app for the simulator (no signing required). Uses its own
# third-party tree (ios-simulator-debug) so the device deps are not clobbered.
build/ios-sim-debug-test: build/ios-sim-debug-test/EVEngine.xcodeproj
	cmake --build build/ios-sim-debug-test --target deps --config Debug -j $(ANDROID_JOBS)
	cd build/ios-sim-debug-test && xcodebuild -scheme eve -configuration Debug \
		-sdk iphonesimulator -arch $(IOS_ARCH) \
		CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY=- CODE_SIGNING_ALLOWED=YES \
		build

build/ios-sim-debug-test/EVEngine.xcodeproj:
	cmake -G Xcode \
		-DCMAKE_SYSTEM_NAME=iOS \
		-DCMAKE_OSX_ARCHITECTURES=$(IOS_ARCH) \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DCMAKE_OSX_SYSROOT=iphonesimulator \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_PLATFORM=ios \
		-DBUILD_TESTING=ON \
		-DEVENGINE_IOS_TEST_APP=ON \
		-DEVENGINE_IOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) \
		-DEVENGINE_IOS_BUNDLE_ID=$(IOS_BUNDLE_ID) \
		-DEVENGINE_IOS_TEST_BUNDLE_ID=$(IOS_TEST_BUNDLE_ID) \
		-DEVENGINE_DOWNLOAD_CLASSIC_SCENES=OFF \
		-DEVENGINE_DOWNLOAD_SPINE_MODELS=OFF \
		-DEVENGINE_DOWNLOAD_SKINNED_CHARACTER=OFF \
		$(DEBUG_LEVEL_FLAG) \
		$(CMAKE_EXTRA_ARGS) \
		-B build/ios-sim-debug-test -S .

