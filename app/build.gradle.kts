plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "dev.tuip.xclipserdna"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.tuip.xclipserdna"
        minSdk = 26
        targetSdk = 35
        versionCode = 4
        versionName = "0.4.0"

        // Exynos 1580 is arm64 only.
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                val ad = (project.findProperty("xclipse.adrenotools") as String?)
                if (ad != null) arguments += listOf("-DXCLIPSE_WITH_ADRENOTOOLS=ON", "-DXCLIPSE_ADRENOTOOLS_DIR=$ad")
            }
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    packaging {
        // FCL loads plugin libs from nativeLibraryDir, so they must be extracted to disk.
        jniLibs { useLegacyPackaging = true }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
