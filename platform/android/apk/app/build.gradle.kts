plugins {
    id("com.android.application")
}

android {
    namespace = "com.evengine.example"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.evengine.example"
        minSdk = 24
        targetSdk = 34
        // Derived from EVENGINE_*_VERSION by scripts/release.py:
        // major*10000 + minor*100 + patch. Do not edit by hand.
        versionCode = 300
        versionName = "0.3.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        getByName("debug") {
            isDebuggable = true
        }
        getByName("release") {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            jniLibs.srcDirs("src/main/jniLibs")
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    lint {
        abortOnError = false
    }
}

dependencies {
    // SDL Java sources are compiled from src/main/java/org/libsdl/app
}
