# Indsin — الخطوط · حركة النص · إمكانية الوصول

> الملفات: `rin_indsin_typography.h` · `rin_indsin_textfx.h` · `rin_indsin_a11y.h` · اختبار: `tests/tools/test_indsin_typo_motion_a11y.cpp`
> كل شيء إضافي: البرامج القديمة تُرسم وتُصدَّر JSON بلا أي مفتاح جديد.

## 1) الخطوط (Typography)

تُعلَن الخطوط على مستوى أعلى الملف (مثل `<Theme>`):

```rin
<Font#Brand src="fonts/brand.woff2, fonts/brand.ttf" weight="100 900" display=swap axes="wght 100 900, wdth 75 125" />
<Font#Lazy  src="fonts/lazy.woff2" load=lazy display=optional />
```
أو بصيغة `@font=Brand … .end/font`.

| المفهوم | الخاصية |
|---|---|
| CustomFont | `<Font#Name src=…>` ثم `font="Name, serif"` على أي عنصر (يرث من الأسلاف) |
| FontLoad | `load=eager\|lazy\|preload` + المضيف: `rin_indsin_font_set_status(name, "loaded"\|"error")` |
| FontDisplay | `display=auto\|block\|swap\|fallback\|optional` (أو `font_display=` على العنصر). block يخفي النص حتى 3ث أثناء التحميل؛ fallback/optional حتى 100ms؛ swap يعرض البديل فوراً |
| SystemFont | `font="system"`, `mono`, `serif`, `sans`, `kufi`, `ui-monospace`… أو اسم خط مثبَّت |
| FontVariant | `font_variant="small-caps tabular-nums"` |
| FontFeature | `font_feature="liga, kern=0, 'ss01', -dlig"` (الصريح يغلب ما تولّده الـ variant) |
| VariableFont | `font_variation="wght 650, wdth 85"` · `weight=650` · `font_stretch=condensed` · `slant=-8` · `optical_size=14`؛ تُقصّ القيم إلى محاور `axes=` |
| أخرى | `font_style=italic\|oblique 12deg` · `text_transform=uppercase\|lowercase\|capitalize\|full-width\|none` (مع قواعد التركية ولاتينية/يونانية/سيريلية) · `text_shadow="1 2 3 #f00, 0 0 8 primary"` · `text_rotate=30` |

## 2) التأثيرات والحركة (TextFx)

| المفهوم | الخاصية |
|---|---|
| TextAnimation | `text_animation="fade-in\|slide-up\|zoom\|bounce\|shake\|wave\|blink\|pulse\|rainbow\|glow\|neon\|flicker\|orbit\|drop\|…"` + `text_duration/text_delay/text_easing/text_distance` |
| TypeWriter | `typewriter=true type_speed=<حرف/ث> type_cursor="\|" type_cursor_blink=500 type_loop type_hold` |
| FadeText | `fade_text=in\|out\|chars\|words` |
| TextReveal | `text_reveal=left\|right\|center\|up\|down\|words\|chars` (قصّ clip أو وحدات) |
| TextRotate | `text_rotate=10 text_rotate_to=110` أو `text_rotate=spin` |
| TextTransition | `text_transition=fade\|slide-up\|…` عند تغيّر `text=` (يُرسم القديم والجديد معاً) |
| Rotator | `text_rotator="Fast\|Simple\|Fun" text_rotator_interval=2000 text_rotator_effect=…` |
| TextShadowAnimation | `text_shadow_animation=glow\|neon\|pulse\|custom` + `text_shadow_to=` (من→إلى) + `text_shadow_color` |
| TextTransform | انظر الخطوط |

- العربية والخطوط المتصلة تُجزَّأ إلى **كلمات** لا حروف حتى لا ينكسر الاتصال.
- `rin_indsin_session_set_reduce_motion(1)` يعيد النص النهائي الساكن فوراً.
- بلا `TextFxRuntime` (CLI/PNG) يُرسم الوضع النهائي.

## 3) إمكانية الوصول (A11y)

| المفهوم | الخاصية |
|---|---|
| AriaLabel | `aria-label` / `ariaLabel` / `a11y_label` · `aria-labelledby` · `aria-describedby` |
| AltText | `alt` / `alt_text` · `decorative=true` |
| AccessibleName | ترتيب الأولوية: labelledby > aria-label > alt/label/text > عناصر الدوّار > نص الأبناء |
| ScreenReader | `rin_indsin_session_screenreader` (نص ما يقوله القارئ) · `rin_indsin_session_a11y_json` · `sr_only=true` · `aria_hidden=true` · مناطق حيّة `aria_live=polite\|assertive` → `rin_indsin_session_announcements_json` |
| SemanticText | `semantic=h1..h6\|strong\|em\|p\|…` · `heading_level` |
| LanguageTag | `lang=ar` (BCP-47، يرث) → `dir=rtl` تلقائياً للغات RTL؛ `<html lang dir>` في التصدير |
| ReadingOrder | `reading_order=N` على الأبناء أو `reading_order=visual` على الأب |

تصدير HTML يحوي شجرة دلالية مخفية بصرياً (`.sr-tree`) والشجرة المرئية `aria-hidden`.

### قواعد التدقيق الجديدة
`A11Y-006` وسم لغة غير صالح · `A11Y-007` لا لغة معلنة · `A11Y-008` ترتيب القراءة ≠ البصري · `A11Y-009` قفزة عناوين · `A11Y-010` هدف labelledby مفقود · `A11Y-011` عنصر تفاعلي `aria_hidden` · `A11Y-012` `aria_live` غير صالح · `TYPO-001/002` · `TXTFX-001` حركة غير معروفة.

## حدود معروفة
- المرسِم المحلي (5×7) يتجاهل العائلة/الـ variant/المحاور/الدوران ويقرّب ضبابية الظل؛ HTML والـ JSON يحملان كل شيء.
- حركات الحرف الواحد وواجهة Kotlin (`typo` JSON) لم تُنفَّذ بعد على أندرويد.
