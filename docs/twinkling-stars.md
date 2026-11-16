# Twinkling Stars — دليل المكتبة

**الإصدار:** 1.0.0 · **الملف:** `lib/twinkling-stars.og.rin` · **الحزمة:** `packages/twinkling-stars/` · **الترخيص:** MIT

Twinkling Stars طبقة بناء تطبيقات فوق Rin وIndsin. توفّر نماذج بيانات قابلة للترجمة إلى واجهات، ونظام تصميم، ومكوّنات شائعة، وتعريف الشاشات والمسارات، ومساعدات النماذج والبيانات وHTML.

## الاستيراد

```rin
@import "lib/twinkling-stars.og.rin";

let app = ts_app("Stellar Notes", "1.0.0", "com.example.stellarnotes", "portrait");
let home = ts_screen("home", "Welcome", "dashboard");
let hero = ts_text("hero", "Build something brilliant", "headline");
home = ts_add_child(home, hero);
let action = ts_button("start", "Get started", "navigate:home", "primary");
print ts_theme();
print ts_export_manifest(app);
```

## المجموعات

| المجموعة | الدوال |
|---|---|
| Design system | `ts_theme`, `ts_color`, `ts_space`, `ts_radius`, `ts_text_size` |
| App & screens | `ts_app`, `ts_screen`, `ts_add_child`, `ts_export_manifest` |
| Components | `ts_text`, `ts_button`, `ts_image`, `ts_input`, `ts_card`, `ts_row`, `ts_column`, `ts_grid`, `ts_modal`, `ts_progress`, `ts_badge`, `ts_list` |
| Navigation | `ts_route`, `ts_navigate`, `ts_back`, `ts_tabbar`, `ts_topbar`, `ts_drawer` |
| Forms | `ts_field`, `ts_form`, `ts_validate_required`, `ts_validate_email`, `ts_validate_min`, `ts_validate_range`, `ts_validate_fields` |
| Data / JSON | `ts_ok`, `ts_fail`, `ts_get`, `ts_pick`, `ts_without`, `ts_map`, `ts_filter`, `ts_find`, `ts_unique`, `ts_sorted`, `ts_json_shape` |
| HTML | `ts_html_escape`, `ts_html_text`, `ts_html_button`, `ts_html_card`, `ts_html_document` |
| Native bridge | `ts_native_capabilities`, `ts_native_request`, `ts_native_supported`, `ts_native_guard`, `ts_permission` |

## Indsin integration

تُرجع دوال `ts_component` ورفاقها خرائط توصيفية محايدة. يمكن لمحوّل Indsin تحويل هذه الخرائط إلى عقد `@view` أو عناصر Fabric. لا تُعدّ هذه الخرائط بحد ذاتها تنفيذًا لعقدة Indsin؛ لذلك يبقى مسار Indsin المباشر باستخدام `@view` متاحًا.

## HTML وJSON

`ts_html_*` تنشئ مقاطع HTML نصية، وتهرب القيم النصية قبل إدراجها. لا تمرّر HTML غير موثوق عبر `body` الخام إلى `ts_html_document`. بيانات `ts_export_manifest` متوافقة بنيويًا مع JSON ويمكن تسلسلها عبر `jsonkit` أو `json.encode` حسب بيئة المشروع.

## C++ / Android

`ts_native_request` تنتج عقدًا موحّدًا بالشكل `{bridge, capability, action, payload}`. على التطبيق المضيف تنفيذ الجسر والتحقق من الأذونات والقدرات قبل تنفيذ الكاميرا أو الملفات أو الإشعارات أو المشاركة. المكتبة لا تنفّذ تلك العمليات مباشرة.

## ZPP / RinPM

المجلد `packages/twinkling-stars/` يحتوي `disconom.toml` و`main.zpp` ووحدات `core/*.zpp`. الملف المفرد `.og.rin` هو نقطة الدخول المضمّنة في المحرك، بينما وحدات ZPP مصدر حزمة قابل للتوزيع والتطوير.

## RinStudio

تظهر المكتبة في **المكتبات → المدمجة → واجهات المستخدم** في RinStudio. اختيارها يعيد سطر `@import` إلى المحرر. صفحة المكتبة على الويب: [Twinkling Stars](https://dlof-lib.github.io/rinlang/?library=twinkling-stars).
