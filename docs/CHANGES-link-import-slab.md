# سجل التغييرات — link / import / slab

## link
- **ربط حيّ حقيقي** بلا نسخ (كان فحصاً وطباعة فقط): `link to=X;` / `link id="..";`
- `readonly` · `only=a,b` · `except=a,b` · `unlink to=X;` · `links("Name")`
- **ربط الملفات:** `link file="./f.rin" as=ns [..];`
- الوصول المؤهَّل (`Cur.x`) يرى الأسماء المرتبطة؛ حماية من الربط الذاتي والدائري (`E0014`).

## import
- `import { a as b }` · `import * as ns from ".."` · `export { a as b } from ".."` · `export * from ".."`
- `@import ".." as ns` صار نطاقاً مسمّى حقيقياً (`ns.fn()` / `ns.x = v`).
- المسارات `./` و`../` تُحلّ نسبةً لمجلد **الملف المستورِد** (متداخلة)، وتُوحَّد قبل منع التكرار.
- رسائل خطأ أوضح للمسار النسبي المفقود.

## slab (جديد)
- `slab.main()` حارس «الملف الرئيسي» يحكم بحسب **أصل الكود** (الدوال والـ methods تلتقط أصلها).
- `slab.main(fn, ...)` · `slab.isImported()` · `slab.name/path/dir/entry/depth/stack/args/imports`

## json
- استيراد ملفات `.json` كوحدات (`@import` / `import {..}` / `link file`)
- `json.into(Class, data[, {strict}])` · `json.stringifyStrict` · `json.lines` / `json.toLines`
- أخطاء parse مع سطر/عمود

## داخلي
- `Environment`: بيانات الربط في بنية تُنشأ عند الحاجة (لا كلفة على الكود العادي).
- `Callable`/`ClassDef`: حقل `originMain` لأجل `slab.main()`.

## الاختبار
- `tests/modules/run.sh`: 34 اختبار سلوك/أخطاء. مقارنة انحدار: 222 ملف مطابق لبناء المصدر الأصلي.
