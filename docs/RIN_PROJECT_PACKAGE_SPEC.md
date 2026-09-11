# Rin Project Package Specification — `.rinproj`

**الحالة:** رسمية — الإصدار 1.0 من تنسيق حزم مشاريع Rin.

## 1. الامتداد الرسمي

`*.rinproj`

الامتداد هو حزمة ZIP معيارية من الداخل، لكن اسم `.rinproj` هو المعرّف الرسمي لمشروع Rin المحمول بين RinStudio والأدوات الأخرى.

MIME المقترح:

`application/vnd.rin.project+zip`

## 2. بنية الحزمة

يجب أن يحتوي جذر الحزمة على:

```text
rin.project.json
main.rin
```

ثم يمكن أن توجد أي ملفات ومجلدات مشروع أخرى، مثل:

```text
lib/
assets/
ui/
data/
*.rin
*.og.rin
صور وملفات ثنائية
```

## 3. الـManifest

اسم الملف ثابت:

`rin.project.json`

مثال رسمي:

```json
{
  "format": "rin-project",
  "formatVersion": 1,
  "name": "MyApp",
  "type": "ui",
  "entry": "main.rin",
  "fileCount": 8,
  "rinStudio": "RinStudio",
  "createdFor": "Rin"
}
```

### الحقول الأساسية

| الحقل | النوع | المطلوب | المعنى |
|---|---|---:|---|
| `format` | string | نعم | يجب أن يكون `rin-project` |
| `formatVersion` | integer | نعم | إصدار مخطط الحزمة |
| `name` | string | نعم | الاسم المقترح للمشروع |
| `type` | string | نعم | `container` / `table` / `ui` / `free` / `illust` |
| `entry` | string | نعم | ملف بدء المشروع؛ حالياً `main.rin` |
| `fileCount` | integer | لا | عدد الملفات المضمنة |
| `rinStudio` | string | لا | الأداة التي صدّرت الحزمة |
| `createdFor` | string | لا | المنظومة المستهدفة؛ `Rin` |

## 4. قواعد الاستيراد

RinStudio لا ينسخ الحزمة مباشرة إلى مشروع قائم. الاستيراد يتم كعملية ذرية:

1. قراءة `rin.project.json`.
2. التحقق من `format` و`formatVersion`.
3. التأكد من وجود `main.rin`.
4. رفض المسارات المطلقة و`..` وZip Slip.
5. رفض العناصر المكررة.
6. وضع حد على عدد العناصر والحجم الكلي للمحتوى.
7. فك الحزمة في مجلد مؤقت.
8. اختيار اسم مشروع فريد عند تعارض الاسم.
9. نقل المجلد إلى `filesDir/projects/` فقط بعد نجاح جميع الفحوص.

بهذا لا يترك استيراد فاشل مشروعاً نصف مستورد.

## 5. التوافق

إصدارات RinStudio القديمة التي تستعمل ZIP العادي يمكن إبقاء دعمها كمسار **Legacy ZIP Import** داخل المشروع.

لكن التبادل الرسمي الجديد بين المشاريع يجب أن يستخدم `.rinproj`.

## 6. التصدير

عند تصدير مشروع، ينشئ RinStudio:

```text
<ProjectName>.rinproj
```

ويضع الـManifest أولاً ثم جميع ملفات المشروع، بما فيها `lib/` والأصول والملفات الثنائية.

## 7. التطوير المستقبلي

الإصدار 1 يحجز إمكانية إضافة حقول مستقبلية مثل:

```json
{
  "minRinVersion": "1.0.0",
  "sdkVersion": "1",
  "dependencies": [],
  "permissions": [],
  "platforms": ["android", "desktop"]
}
```

المستورد الحالي يتجاهل الحقول الاختيارية غير المعروفة، لكنه يرفض `formatVersion` غير المدعوم حتى لا يفسر بنية مستقبلية بشكل خاطئ.
