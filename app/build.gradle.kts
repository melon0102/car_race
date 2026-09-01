plugins {
    id("com.android.application")
}

android {
    namespace = "com.revolt.game"
    compileSdk = 34
    ndkVersion = "25.2.9519653"

    defaultConfig {
        applicationId = "com.revolt.game"
        minSdk = 28        // API 28 so emulators (BlueStacks P64) can run it; primary targets are Android 11-13
        targetSdk = 33     // Android 13
        versionCode = 1
        versionName = "0.1.0"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_ARM_NEON=ON"
                )
                cppFlags += listOf("-std=c++17")
            }
        }
        ndk {
            // -ParmOnly builds without x86_64: BlueStacks then runs the ARM
            // libs through its (better-tested) translation layer
            if (project.hasProperty("armOnly")) {
                abiFilters += listOf("arm64-v8a", "armeabi-v7a")
            } else {
                abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
        debug {
            isJniDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    // NativeActivity shell needs no androidx runtime deps
}
