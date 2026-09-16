# تعديلات: `args` / `@stop` / `finally` فوق `@Program`

هذه إضافات جديدة تُبنى مباشرة فوق `@Program` (الذي أُضيف في تعديل سابق: `rinlang-changed-files.zip`
+ `PATCH_NOTES.md` المرفقين هناك). لا تُغيِّر شيئاً في ذلك التعديل، بل تُكمِله بثلاثة مفاهيم جديدة
لجعل `@Program` نقطة دخول/خروج حقيقية للبرنامج (entry/exit point) لا مجرد علامتَي طباعة.

## طريقة التركيب
انسخ كل ملف إلى نفس المسار **بالضبط** داخل مستودعك (استبدال الملف الأصلي بالكامل، وليس دمجاً) —
هذه الملفات هي **أحدث نسخة كاملة**، وتتضمن تعديلات هذا الدور فوق تعديلات `@Program` السابقة معاً:

```
app/src/main/cpp/rin_ast.h
app/src/main/cpp/rin_interpreter.h
app/src/main/cpp/rin_interpreter.cpp
app/src/main/cpp/rin_parser.cpp
app/src/main/cpp/rin_parser.h
cli/linux/src/main.cpp
cli/windows/src/main.cpp
cli/macos/src/main.cpp
```

لم يتغيّر شيء في `app/src/main/cpp/rin_make.cpp` أو `app/src/main/cpp/loom/rin_loom_pipeline.h` في
هذا الدور (ما زالا كما سلَّمهما التعديل السابق لـ `@Program` بلا أي حاجة لإعادة نسخهما).

## المفاهيم الجديدة الثلاثة

### 1) `args` — وسائط سطر الأوامر داخل `@Program`
أقرب `@Program` إلى سطح الملف (غير المتعشِّشة داخل أي `@Program` أخرى) تحصل تلقائياً على متغيّر
`args` (مصفوفة نصوص) مُعرَّف في بيئتها، مرئي لكل ما بداخلها (بما فيها أي `@Program` متعشِّشة أعمق،
عبر سلسلة البيئات العادية). القيمة تُملأ من `Interpreter::setProgramArgs(std::vector<std::string>)`
(عامة جديدة)، وتبقى `[]` لأي مستدعٍ لا يستدعيها إطلاقاً — توافقية كاملة.

عبر واجهة سطر الأوامر (linux/windows/macos)، أي وسائط بعد اسم الملف تُمرَّر تلقائياً:
```
rin myapp.rin hello 42
```
داخل `myapp.rin`:
```rin
@Program=Main
    print(args); // ["hello", "42"]
.end/Program
```

### 2) `@stop;` / `@stop expr;` — إنهاء نظيف لأقرب `@Program`
عبارة جديدة (وليست تعديلاً على `return`/`break`): تنهي أقرب `@Program` مفتوحة حالياً فوراً ونظيفاً
(وليست فشلاً) — تنفّذ `finally` إن وُجدت، تطبع علامة النجاح المعتادة 🏁 مع إشارة "أُوقفت يدوياً"،
ثم يستمر البرنامج بعد `.end/Program` الخاصة بها طبيعياً، تماماً كأن جسمها انتهى عند تلك النقطة.

- تؤثر فقط على أقرب `@Program` محيطة (لا تتخطى إلى الأب عند التعشيش، تماماً كـ `break` مع الحلقات).
- القيمة الاختيارية (`@stop 42;`) تصبح "نتيجة" تلك الـ Program، تُقرأ عبر `programResult()` الجديدة.
- استخدامها خارج أي `@Program` خطأ تنفيذ صريح وواضح (`'@stop' used outside of any @Program`)، بنفس
  أسلوب `'return' used outside of a function`.
- `stop` بلا `@` تبقى اسم متغيّر/دالة عادياً بلا أي تعارض — التمييز حصراً عبر البادئة `@stop`.

### 3) `finally { ... }` — إنهاء مضمون
عبارة اختيارية جديدة، بأي ترتيب مع `recover` الموجودة مسبقاً، تُكتَب أيضاً قبل `.end/Program`:
```rin
@Program=WithCleanup
    print("العمل...");
recover (err) {
    print("تم الاسترداد: " + err.message);
}
finally {
    print("هذا يُطبَع دائماً: نجاح أو استرداد أو @stop أو فشل غير مُدار");
}
.end/Program
```
جسمها يُنفَّذ **مرة واحدة بالضبط** قبل أن تنتهي الـ Program فعلياً، في كل الحالات الأربع: نجاح
طبيعي، `@stop`، استرداد ناجح عبر `recover`، أو فشل بلا `recover` (وحتى هنا: `finally` تُنفَّذ *قبل*
إعادة رمي الخطأ للأعلى). لا يصلها متغيّر خطأ (خلافاً لـ `recover`) — دورها التنظيف الحتمي فقط.

### دالة مدمجة جديدة: `programResult()`
تُعيد نتيجة آخر `@Program` انتهت للتو (أي عمق تعشيش): قيمة `@stop expr;` إن استُخدِمت، أو `nil`
غير ذلك (انتهاء طبيعي أو عبر `recover`). مفيدة مباشرة بعد `.end/Program` لمعرفة كيف انتهت.

## مثال متكامل
```rin
@Program=Outer
    print("args=" + args);

    @Program=Phase1
        print("...");
        @stop "تم مبكراً";
    finally {
        print("نتيجة Phase1 = " + programResult()); // "تم مبكراً"
    }
    .end/Program

    @Program=Phase2
        text x = 5; // خطأ نوع
    recover (err) {
        print("استرداد Phase2: " + err.message);
    }
    finally {
        print("تنظيف Phase2 دائماً");
    }
    .end/Program
.end/Program
```

## التحقق
تم بناء `cli/linux` بالكامل فعلياً عبر `g++ -std=c++17` (نفس قائمة ملفات `CMakeLists.txt` الأصلية)،
وتشغيل اختبارات يدوية غطّت: `args` من سطر الأوامر، `@stop` بقيمة وبدونها، `@stop` متعشِّشة (تؤثر
على الأقرب فقط)، `finally` في كل الحالات الأربع (نجاح/`@stop`/استرداد/فشل بلا استرداد قبل إعادة
الرمي)، ترتيب `programResult()` الصحيح داخل `finally` نفسها، و`@stop` خارج أي `@Program` (رسالة
خطأ واضحة بدل انهيار). لم يُبنَ `cli/windows`/`cli/macos` فعلياً هنا (بلا بيئة Windows/macOS في
هذا الـ sandbox) لكن التعديل عليهما مطابق بنيوياً لتعديل `cli/linux` المُختبَر.

## ملاحظات
- كل شيء أعلاه اختياري تماماً؛ أي ملف `.rin` لا يستخدم `@stop`/`finally`/`args` يعمل بلا أي تغيير.
- `@stop`/`finally`/`programResult()` كلها خاصة بـ `@Program`؛ لا تعمل بلا `@Program` محيطة.
- لم تُعدَّل أي ملفات أخرى غير الثمانية المذكورة أعلاه في هذا الدور.
