# Rin build repair — 2026-09-14

Fixed:
- Gradle plugin resolution failure at app/build.gradle:2.
- Root Groovy build.gradle now declares Android/Kotlin/Google Services plugin versions.
- Removed duplicate build.gradle.kts files that competed with the Groovy build.
- Aligned Gradle wrapper and GitHub Actions with AGP 8.13.0 -> Gradle 8.13.
- Kept JDK 17, NDK 26.1.10909125 and CMake 3.22.1.
- Gave duplicate signed-release workflows distinct names.
- Added Gradle toolchain verification to Android workflows.

Primary failure from supplied log:
Plugin [id: 'com.android.application'] was not found ... plugin dependency must include a version number.
The cause was that the active root build.gradle had no plugin versions while app/build.gradle requested an unversioned Android plugin.

Official AGP compatibility: AGP 8.13 requires Gradle 8.13 and JDK 17.
