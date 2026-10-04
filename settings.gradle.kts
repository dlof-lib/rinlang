pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        // Tesseract4Android (OCR عربي) لا يُنشر على Maven Central/Google بل على JitPack فقط.
        // exclusiveContent: JitPack يُستعمل لهذه المجموعة وحدها ولا يُستعلم عنه لأي مكتبة أخرى.
        exclusiveContent {
            forRepository { maven { url = uri("https://jitpack.io") } }
            filter { includeGroup("cz.adaptech.tesseract4android") }
        }
    }
}

rootProject.name = "RinLang"
include(":app")
