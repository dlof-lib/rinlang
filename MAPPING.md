# خريطة الاستبدال: الإيموجي والرموز ← أيقونات vector (RinStudio)

| الرمز/الإيموجي | الأيقونة |
|---|---|
| ✓ ✔ ✅ | `rs_ic_check` |
| ✗ ✕ ✖ ❌ | `rs_ic_close` |
| ⚠ | `rs_ic_warning` |
| ⛔ | `rs_ic_block` |
| ⭐ ★ | `rs_ic_star` / `rs_ic_star_outline` |
| ▶ ▸ | `rs_ic_play` / `rs_ic_chevron_right` |
| ▾ | `rs_ic_chevron_down` |
| ← → ↑ ↓ | `rs_ic_arrow_left/right/up/down` |
| ↔ ⇅ ↗ | `rs_ic_swap` / `rs_ic_sort` / `rs_ic_external` |
| ↻ ↺ ↶ ↷ 🔄 | `rs_ic_refresh` / `rs_ic_undo` / `rs_ic_redo` |
| ⬇ ⤓ | `rs_ic_download` |
| ⏹ ❚❚ | `rs_ic_stop` / `rs_ic_pause` |
| ⏱ | `rs_ic_timer` |
| ⛶ ⤢ | `rs_ic_fullscreen` |
| ⌫ ⇥ | `rs_ic_backspace` / `rs_ic_tab` |
| 🔍 | `rs_ic_search` |
| 📦 | `rs_ic_package` |
| 🌐 | `rs_ic_globe` |
| 📚 | `rs_ic_library` |
| 🖼 | `rs_ic_image` |
| 🗜 | `rs_ic_archive` |
| ⚙ | `rs_ic_settings` |
| 💾 | `rs_ic_save` |
| 🗂 | `rs_ic_container` |
| 🎨 | `rs_ic_palette` |
| 🏷 | `rs_ic_tag` |
| 📌 | `rs_ic_pin` |
| 🔗 | `rs_ic_link` |
| 🔏 | `rs_ic_shield_check` |
| 🧾 | `rs_ic_receipt` |
| 📊 | `rs_ic_chart` |
| 🧵 | `rs_ic_pipeline` |
| ▦ | `rs_ic_grid` |
| 🖨 | `rs_ic_print` |
| 📄 | `rs_ic_file` |
| 📁 | `rs_ic_folder` |
| ● ◆ 🔹 | `rs_dot_accent` / `rs_ic_diamond` |
| △ ▽ | `rs_ic_triangle_up/down` |
| ◽ | `rs_ic_checkbox_empty` |
| ✦ | `rs_ic_sparkle` |
| ‹ › ⋮ − | `rs_ic_chevron_left/right`, `rs_ic_more_vert`, `rs_ic_minus` |

الأحجام المتاحة للاستخدام داخل النصوص: `rs_ic_<name>_18` و`_22` و`_44`.

## أيقونات الملفات
`rs_file_file`, `rs_file_rin`, `rs_file_indsin`, `rs_file_rinpak`, `rs_file_json`, `rs_file_markdown`, `rs_file_text`, `rs_file_html`, `rs_file_css`, `rs_file_js`, `rs_file_xml`, `rs_file_yaml`, `rs_file_csv`, `rs_file_log`, `rs_file_image`, `rs_file_audio`, `rs_file_video`, `rs_file_archive`, `rs_file_pdf`, `rs_file_binary`, `rs_file_config`, `rs_file_shell`, `rs_file_code`

## أيقونات المجلدات
`rs_folder`, `rs_folder_open`, `rs_folder_empty`, `rs_folder_src`, `rs_folder_lib`, `rs_folder_assets`, `rs_folder_docs`, `rs_folder_tests`, `rs_folder_config`, `rs_folder_new`

الاختيار التلقائي لأيقونة المجلد حسب اسمه (src, lib, assets, docs, tests, config) في `RsFolderIcons.kt`.

## ما تم تحويله في الكود
- `RsIcons.kt`: مساعد لوضع أيقونة vector داخل أي TextView (`start` / `only` / `clear`).
- أزرار طرفية RinTerminalView، وصفوف RinJobAdapter (نسخ/مشاركة/إعادة/تثبيت/إغلاق/طيّ)، وصفوف الإعدادات، وعدّاد تنزيلات المتجر، وأزرار NewProjectActivity، وشريط لوحة المعاينة الحية.
- نصوص `strings*.xml`: أُزيلت الإيموجي الزخرفية (✓ ⚠ ⛔ ⭐ 📦 …) من 220 نصاً.
- سجلّ تصدير APK وقوالب المشاريع: أُزيلت البادئات الزخرفية.

## أُبقي عمداً
- ★ في نصوص التقييم، والأسهم والنقاط الطباعية داخل النصوص (→ · • ▸).
- رموز الطرفية/الكونسول (RinConsoleFormatter وRinContainerOutput وRinTerminalView ذات الرسم النصي): جزء من بروتوكول مخرجات المحرّك الأصلي.
- رموز الرسم داخل IndsinFabricView وIndsinIconData (بيانات لغة Indsin نفسها).
- أعلام الدول في قائمة اللغات.
