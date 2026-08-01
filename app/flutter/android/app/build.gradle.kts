import java.util.Properties
import java.io.FileInputStream

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

// Release signing is read from android/key.properties (kept OUT of git; see
// key.properties.example). Absent -> the release build falls back to debug
// signing, so `flutter run --release` still works without the keystore.
val keystorePropertiesFile = rootProject.file("key.properties")
val keystoreProperties = Properties().apply {
    if (keystorePropertiesFile.exists()) load(FileInputStream(keystorePropertiesFile))
}

android {
    namespace = "com.aisindex.ais"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "com.aisindex.ais"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    signingConfigs {
        if (keystorePropertiesFile.exists()) {
            create("release") {
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
                // v2 is enough for Play, but v3 is what makes it possible to
                // ROTATE this key later without every installed copy refusing the
                // update. AGP leaves v3 off by default, so the shipped APKs have
                // been v2-only; turning it on costs nothing and cannot be added
                // retroactively to signatures already in the wild.
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    buildTypes {
        release {
            // Real release key when key.properties is present, else debug (dev builds).
            signingConfig = if (keystorePropertiesFile.exists())
                signingConfigs.getByName("release")
            else
                signingConfigs.getByName("debug")

            // R8 / resource shrinking is deliberately OFF, not overlooked. Dart is
            // compiled AOT and the engine ships as native .so, so almost the whole
            // download is code R8 never touches; what it could shrink is the thin
            // Kotlin layer, worth a rounding error. Against that it can strip
            // reflectively-reached classes -- speech_to_text reaches Android's
            // SpeechRecognizer service -- and the failure would appear only at
            // runtime, on a user's phone, in the release build alone. Bad trade
            // until there is a device test that would catch it.
            isMinifyEnabled = false
            isShrinkResources = false
        }
    }

    // Compile the AIS C engine (../../src -> ../../../c) into libais.so per ABI,
    // packaged into the APK and loaded by the Dart FFI (lib/ais_ffi.dart).
    externalNativeBuild {
        cmake {
            path = file("../../src/CMakeLists.txt")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
