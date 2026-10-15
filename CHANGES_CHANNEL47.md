# CHANGES_CHANNEL47 — لغة channel47 (`.ch47`) لوصف بيانات وإعدادات المكتبات والحزم والأدوات

صيغة نصّية صارمة تمثّل النصوص والأرقام والمنطقي والتواريخ والمصفوفات والجداول. المواصفة: [`docs/channel47.md`](docs/channel47.md).

## الملفات (كلها جديدة، لا تعديل على ملفات موجودة)
| الملف | الدور |
|---|---|
| `docs/channel47.md` | المواصفة الكاملة: الأنواع، الجداول، رموز الأخطاء `C47xxx`، EBNF |
| `tools/channel47.py` | المرجع (بايثون بلا اعتماديات): `check fmt json from-json get merge` |
| `lib/channel47.og.rin` | قارئ داخل Rin: `c47_parse c47_try c47_get c47_has c47_merge c47_check` |
| `tests/channel47_test.py` · `tests/channel47_rin_test.rin` | 20 اختباراً بايثون · 57 فحصاً Rin |
| `examples/channel47/*.ch47` | بيان مكتبة، حزمة، أداة، وإعدادات تطبيق |
| `schemas/channel47-manifest.schema.json` | مخطط JSON لتصدير البيان |
| `assets/branding/channel47/` · `docs/icons/channel47{,-file}.svg` | أيقونتا التطبيق والملف: SVG + PNG 16…512 + ICO |
| `tools/gen_channel47_icon.py` | مولّد الأيقونة |

## التحقق
- `python3 tests/channel47_test.py` ← 20/20.
- `rin_run tests/channel47_rin_test.rin` (من الجذر) ← 57/57، بُني `rin_run` على سطح المكتب (g++) من مصادر المشروع.
- مرجع بايثون وقارئ Rin يعطيان الرمز نفسه لكل خطأ في الاختبار (17 رمزاً).

## ما لم يُنجز
- لم تُربط الصيغة بـ RinPM/RinPak بعد (يلزم قرار: هل يصير `.ch47` بياناً بديلاً لـ `project.og.urin`؟).
- ربط الامتداد `.ch47` بالنظام (أيقونة ملف/تطبيق) وإضافة مكتبة `channel47` إلى `tools/embed_libs.py` لم يُجريا.
- كتابة (dump) channel47 من Rin غير موجودة؛ تتم عبر أداة بايثون.
- التعليقات لا تُحفظ عند `fmt`.
