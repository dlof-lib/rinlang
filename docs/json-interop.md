# Rin ↔ JSON — التواصل الكامل

المكتبة `json.*` (parse / stringify / query / patch / diff / readFile ...) موجودة أصلاً؛ هذا الملف
يوثّق ما أُضيف لجعل التواصل **من الطرفين** مباشراً وآمناً.

## 1) ملفات JSON كوحدات بيانات
الجذر يجب أن يكون **كائناً**؛ كل مفتاح (اسم صالح كمتغير) يصبح متغيراً.
```kotlin
@import "./config.json" as cfg;           // cfg.port · cfg.server.host
import { port as p, name } from "./config.json";   // مفاتيح محدَّدة (+ إعادة تسمية)
@import "./config.json";                  // دمج مباشر في النطاق الحالي
link file="./config.json" as=cfg readonly;// ربط حيّ بالحاوية (انظر link-and-import.md)
```
- المسارات `./` و`../` نسبةً إلى الملف المستورِد (كالوحدات).
- مفتاح غير صالح كاسم (`"not-an-identifier"`) يُتجاوَز في الدمج؛ استعمل `json.readFile` للوصول إليه.
- أخطاء: ملف مفقود `E0029` · JSON غير صالح (مع سطر/عمود) أو جذر غير كائن أو مفتاح مفقود `E0028`
  (رسالة المفتاح المفقود تسرد المفاتيح المتاحة).

## 2) JSON → كائنات صنف: `json.into`
```kotlin
class Address { let city = ""; let zip = 0; }
class User { let name = ""; let age = 0; let addr = Address(); let nick = nil; }

let u = json.into("User", "{\"name\":\"Rana\",\"age\":31,\"addr\":{\"city\":\"Beirut\"}}");
let list = json.into("User", "[{\"name\":\"A\"},{\"name\":\"B\"}]");     // مصفوفة → مصفوفة كائنات
json.into("User", data, {"strict": true});                              // يرفض المفاتيح غير المعرَّفة
```
- المدخل: نص JSON أو قاموس/مصفوفة جاهزة.
- **فحص الأنواع** من القيم الافتراضية في الصنف (`nil` = أي نوع)؛ الحقول الكائنية المتداخلة تُبنى تكرارياً.
- الحقول الناقصة تحتفظ بقيمها الافتراضية؛ لا يُستدعى `init` (كـ `oop.fromMap`).
- عدم التطابق يعطي `E0004` مع مسار الحقل: `json.into: field $.addr.zip expected number but got string`.
- الاتجاه المعاكس: `json.stringify(obj)` (الحقول العامة).

## 3) تصدير صارم: `json.stringifyStrict(v [, indent])`
`json.stringify` يحوّل الدوال وNaN/Infinity إلى `null` صامتاً. النسخة الصارمة **ترفضها** بخطأ `E0004`
يذكر المسار (`$[0]`, `$.a[1].b`) ويكشف البنى الدائرية.

## 4) JSON Lines (NDJSON)
```kotlin
json.lines(text)        // سطر = قيمة JSON → مصفوفة (يتجاوز الأسطر الفارغة؛ الخطأ يذكر رقم السطر)
json.toLines(array)     // العكس: عنصر لكل سطر + سطر جديد أخير
```

## 5) أخطاء `parse` بموضع
كل أخطاء `json.parse/tryParse/readFile` تنتهي بـ `(line L, column C)`.
