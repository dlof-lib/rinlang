# CI وأدوات المستودع

| Workflow | الغرض |
|---|---|
| [`rin__apk.yml`](../.github/workflows/rin__apk.yml) | الخط الرئيسي: فحص مبكر ← اختبارات المحرّك ‖ بناء APK موقَّع ← Release |
| [`build-android-versions.yml`](../.github/workflows/build-android-versions.yml) | حزمة APK لكل إصدار أندرويد (على الوسوم `v*`) |
| `linux-cli` · `macos-cli` · `windows-cli` | بناء واختبار أداة `rin` لكل منصة |
| [`pages.yml`](../.github/workflows/pages.yml) | بناء الموقع + WASM ونشره على GitHub Pages |
| [`build.yml`](../.github/workflows/build.yml) | بناء إضافة Visual Studio (`RinLangVSSDK`) |
| [`cleanup-project.yml`](../.github/workflows/cleanup-project.yml) | تنظيف المستودع، دمج سجلات التغييرات، توليد فهرس المكتبات (يدوي) |
| [`highlight-rin.yml`](../.github/workflows/highlight-rin.yml) | تلوين كود Rin على GitHub (يدوي) |

> ⚠️ `delete-md.yml` يحذف **كل** ملفات `.md` عند تشغيله يدوياً (عدا README عند تفعيل الخيار).
> لا تشغّله بعد إضافة `CONTRIBUTING.md` و`SECURITY.md` وغيرها.

## تلوين Rin على GitHub

لغة Rin غير معروفة لـ [linguist](https://github.com/github-linguist/linguist) بعد (تتطلب انتشار ≥2000 ملف `.rin` علناً —
انظر [`linguist-submission/`](../linguist-submission/README.md)). لذلك يضبط `highlight-rin.yml` قاعدة
`linguist-language` في `.gitattributes` فتظهر ملفات `.rin` و`.og.rin` بتلوين Kotlin.
للتلوين الكامل في المحرّرات استخدم [`syntaxes/rin.tmLanguage.json`](../syntaxes/rin.tmLanguage.json).
