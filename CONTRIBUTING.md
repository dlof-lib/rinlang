# المساهمة في Rin

شكراً لاهتمامك! أي مساهمة تنفع: إصلاح خطأ، مكتبة، مثال، ترجمة، أو تحسين وثيقة.

## قبل أن تبدأ

- ابحث في [Issues](https://github.com/dlof-lib/rinlang/issues) أولاً حتى لا يتكرر العمل.
- التغييرات الكبيرة (تغيير في اللغة أو المحرّك) ناقشها في Issue قبل كتابة الكود.
- بالمشاركة تلتزم بـ [قواعد السلوك](./CODE_OF_CONDUCT.md).

## بناء المشروع واختباره

```bash
cd cli/linux && ./build.sh        # يحتاج cmake + g++ + zlib1g-dev
./build/rin test ../../tests      # تشغيل اختبارات .rin
```

macOS: `cli/macos/build.sh` · Windows: `cli\windows\build.bat` · أندرويد: `make apk`.
التفاصيل في [`docs/getting-started.md`](./docs/getting-started.md).

## أنواع المساهمات

| النوع | ما المطلوب |
|---|---|
| إصلاح في المحرّك (`app/src/main/cpp`) | اختبار `.rin` في `tests/` يفشل قبل الإصلاح وينجح بعده |
| مكتبة جديدة | ملف `lib/name.og.rin` يبدأ بسطر `// lib/name.og.rin — الوصف`، مع `@version` و`@category` و`@prefix`، وملف اختبار |
| وثائق | عدِّل ملف `docs/*.md` المعني، وحافظ على أمثلة **جُرِّبت فعلاً** |
| ترجمة الواجهة | `app/src/main/res/values-*/` |

بعد إضافة مكتبة شغّل workflow **Cleanup & Organize Project** ليُحدَّث [`lib/README.md`](./lib/README.md).

## طلب الدمج (Pull Request)

1. أنشئ فرعاً من `main` باسم وصفي: `fix/lexer-eof` · `feat/lib-geometry`.
2. اجعل الطلب صغيراً ومركّزاً على شيء واحد.
3. شغّل الاختبارات محلياً وأرفق نتيجتها.
4. أضف سطراً في [`.changelog/CHANGELOG.md`](./.changelog/CHANGELOG.md) لأي تغيير يراه المستخدم.
5. اتبع قالب الطلب في GitHub.

## أسلوب الكود

- C++17، لا اعتماديات جديدة دون نقاش (انظر [`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md)).
- ملفات Rin: مسافة بادئة ثابتة؛ استخدم `rin fmt <file> --write`.
- رسائل الـ commit: جملة قصيرة بصيغة الأمر، مثل `Fix lexer crash on empty file`.

## الترخيص

بإرسال مساهمتك فإنك توافق على ترخيصها بموجب [MIT](./LICENSE) مثل بقية المشروع،
وتؤكد أن لك الحق في تقديمها. لا تُضمِّن كوداً منسوخاً من مشروع بترخيص غير متوافق؛ وإن كان لا بدّ،
فنبّه عليه في الطلب ليُضاف إلى [`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md).
