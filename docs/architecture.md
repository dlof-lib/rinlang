# المعمارية وبنية المستودع

## المعمارية

```text
            ┌──────────────────────────────┐
            │   Lexer → Parser → AST       │
            │        Interpreter           │   app/src/main/cpp  (C++17)
            └──────────────┬───────────────┘
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
   rin CLI           RinStudio (JNI)     WebAssembly
 linux·macos·win        Android          المتصفح / Pages
```

المحرّك نفسه يُستخدم في كل مكان — لا نسخ مكرَّرة ولا محاكاة — لذلك يعطي البرنامج النتيجة نفسها أينما شُغِّل.

| المضيف | الاستخدام |
|---|---|
| أداة `rin` | تشغيل الملفات، REPL، فحص نحوي، إنشاء المشاريع، إدارة الحزم |
| RinStudio | بيئة تطوير كاملة على أندرويد: محرّر، مشاريع، معاينة واجهات، نشر حزم |
| المتصفح | تشغيل Rin عبر WebAssembly في Playground |

## بنية المستودع

```text
rinlang/
├── README.md · indsin.md · LICENSE · NOTICE · COPYRIGHT · VERSION
├── CONTRIBUTING.md · CODE_OF_CONDUCT.md · SECURITY.md · SUPPORT.md
├── TRADEMARKS.md · THIRD_PARTY_NOTICES.md
├── .changelog/      سجل التغييرات
│
├── app/             RinStudio (أندرويد) + المحرّك في app/src/main/cpp
├── cli/             أداة rin لـ linux · macos · windows
├── compiler/        rinc — المترجم الأصلي
├── gorin/           محرّك Go مساعد
├── pitok/           لغة واجهات مستقلة (تجريبي)
│
├── lib/             المكتبات الرسمية (.og.rin) + الفهرس
├── examples/        أمثلة وقوالب
├── templates/ · content-packs/   قوالب المشاريع وحزم المحتوى
├── docs/            التوثيق
│
├── web/ · user-site/   الموقع + متجر الحزم + WebAssembly
├── bindings/        Python · Node.js · C
├── src/ · syntaxes/ · editors/ · linguist-submission/   دعم المحرّرات
│
├── tests/           الاختبارات
├── tools/ · scripts/   أدوات التشغيل والبناء والنشر
├── packaging/ · schemas/
└── .github/         سير عمل CI وقوالب Issues/PR
```
