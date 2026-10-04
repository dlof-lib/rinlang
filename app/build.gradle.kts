import java.io.File

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    // يجب أن يبقى هذا آخر سطر بلوجنات لكي يقرأ google-services.json بشكل صحيح
    id("com.google.gms.google-services")
}

// نظام الإصدار الرسمي لمحرّك Rin: المصدر الوحيد هو ملف VERSION في جذر
// المستودع (يجب أن يطابق RIN_VERSION_STRING في app/src/main/cpp/rin_version.h
// حرفياً — انظر docs/VERSIONING.md). versionName يُقرأ من هنا مباشرة بدل رقم
// ثابت منفصل يمكن أن ينسى أحد تحديثه عند رفع الإصدار.
val rinVersionName = File(rootDir, "VERSION").readText().trim()
val rinVersionParts = rinVersionName.split("\\.")
// versionCode = major*10000 + minor*100 + patch — يضمن تصاعداً رقمياً صحيحاً
// عبر أي تسلسل SemVer صالح (patch/minor حتى 99)، كما يطلبه Play Store.
val rinVersionCode = (rinVersionParts[0].toInt() * 10000) +
                     (rinVersionParts[1].toInt() * 100) +
                      rinVersionParts[2].toInt()

// ---- تقليص حجم التطبيق ---------------------------------------------------------------------
// خصائص gradle (gradle.properties أو -P): rin.minify=false يعطّل R8/shrinkResources في release،
// و rin.abis="" يبني كل المعماريات (الافتراضي arm64-v8a و armeabi-v7a لـ release فقط؛ debug يبني الكل
// ليعمل على المحاكيات x86). اللغات الإضافية (en/es) ومحتوى مثل لغة Illust لا تُشحن في الـ APK أصلاً:
// تُنزَّل كحزم عند الحاجة (انظر content-packs/ و app/src/main/java/com/dlof/rinlang/packs/).
val rinMinify = (findProperty("rin.minify") ?: "true").toString().toBoolean()
val rinReleaseAbis = (findProperty("rin.abis") ?: "arm64-v8a,armeabi-v7a").toString()
    .split(",").map { it.trim() }.filter { it.isNotEmpty() }

android {
    namespace = "com.dlof.rinlang"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.dlof.rinlang"
        minSdk = 24
        targetSdk = 34
        versionCode = rinVersionCode
        versionName = rinVersionName

        // موارد المكتبات (Material/AppCompat/Firebase...) بعشرات اللغات لا حاجة لها: نُبقي لغات التطبيق فقط.
        // (en/es تأتي من حزم اللغة المنزَّلة وقت التشغيل، لكن نصوص المكتبات الافتراضية بالإنجليزية تكفيها.)
        @Suppress("DEPRECATION")
        resourceConfigurations += listOf("ar", "en", "es")

        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = File("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // توقيع الإصدار الاحترافي (release) عبر متغيّرات بيئة، بدل قيم ثابتة في الملف أو Secrets يجب
    // ضبطها يدوياً في GitHub. سير عمل build_apk.yml يُولِّد keystore وكلمتَي مرور عشوائيتين بنفسه
    // في كل تشغيل ويمرّرهما هنا كمتغيّرات بيئة — فلا حاجة لأي إعداد يدوي على GitHub إطلاقاً.
    // عند البناء المحلي بدون هذه المتغيّرات، يقع release تلقائياً على توقيع debug القياسي حتى
    // لا يفشل `gradle assembleRelease` محلياً بسبب غياب keystore.
    val rinKeystorePath = System.getenv("RIN_KEYSTORE_PATH")
    val hasReleaseSigning = rinKeystorePath != null && File(rinKeystorePath).exists()

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                storeFile = File(rinKeystorePath!!)
                storePassword = System.getenv("RIN_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("RIN_KEY_ALIAS") ?: "rin"
                keyPassword = System.getenv("RIN_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = rinMinify
            isShrinkResources = rinMinify
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (rinReleaseAbis.isNotEmpty()) {
                ndk { abiFilters += rinReleaseAbis }
            }
            if (hasReleaseSigning) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        viewBinding = false
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.activity:activity-ktx:1.9.2")
    implementation("com.google.android.material:material:1.12.0")
    // Real QR generation for Rin Artifact / Container runtime.
    implementation("com.google.zxing:core:3.5.3")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    // سحب-للتحديث (Pull-to-refresh) في شاشة متجر Rin — مكتبة AndroidX قياسية مستقرة.
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    // درج "مستكشف المشروع" المدمج في المحرر (شجرة الملفات/المجلدات + شجرة الحاويات) — انظر
    // activity_main.xml (DrawerLayout) و MainActivity.kt (btnExplorer).
    implementation("androidx.drawerlayout:drawerlayout:1.2.0")

    // Firebase — Realtime Database + Authentication (Email/Password) فقط، وكلاهما ضمن
    // خطة Spark المجانية بدون بطاقة دفع. لا نستخدم Cloud Functions ولا Storage المدفوع.
    implementation(platform("com.google.firebase:firebase-bom:33.1.2"))
    implementation("com.google.firebase:firebase-auth-ktx")
    implementation("com.google.firebase:firebase-database-ktx")

    // مكتبة Google الرسمية لتوقيع حزم APK (نفس المكتبة التي تستخدمها أداة `apksigner` في
    // Android SDK) — تُستخدم من ميزة "تصدير APK" (انظر RinApkExporter) لتوقيع مشاريع Rin
    // المُصدَّرة بتوقيع Android رسمي فعلي (Signature Scheme v1 + v2 + v3)، بلا اعتماد على
    // أدوات بناء خارجية أو خادم بعيد. لا تعتمد على أي API داخلي في OpenJDK فتعمل على أندرويد.
    implementation("com.android.tools.build:apksig:8.5.2")

    // OCR على أندرويد لـ make.ocr / make.image.text (RinMediaBridge: image.ocr). النموذج اللاتيني مضمَّن
    // في الحزمة (يعمل بلا إنترنت). للاتينية فقط (لا يدعم العربية) — العربية عبر Tesseract أدناه.
    implementation("com.google.mlkit:text-recognition:16.0.1")
    // العربية: ML Kit لا يدعمها، فتمر عبر Tesseract (مع ara.traineddata/eng.traineddata — انظر scripts/fetch_tessdata.sh).
    implementation("cz.adaptech.tesseract4android:tesseract4android:4.9.0")
}
