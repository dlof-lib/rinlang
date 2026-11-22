## EXTRA13 — خمس إضافات بنمط Rin
- `bytes.encode/decode/size`: بايتات بمواصفة مسمّاة (`magic:str4 ver:u16le size:u32be`) مع endianness صريح وإدخال/إخراج بقاموس.
- `dur.*`: مدد زمنية (`parse/of/parts/text/fmt/add/sub/scale/abs/cmp/valid`) — رقم بالميلي ثانية، تقبل `"1h30m"` وISO.
- `firstOf/lastOf/firstKey/lastKey/findKeyIf` للمصفوفة والقاموس والمجموعة.
- `utf8Trim/TrimStart/TrimEnd/CapFirst/LowFirst/Pad` — يفهم NBSP وعلامات الاتجاه العربية ويعدّ الأحرف لا البايتات.
- `state fixed x = v;` داخل الحاوية: حقل يُهيَّأ مرة واحدة وأي إسناد لاحق يرمي `E0003`.
- المصدر: `rin_extra_natives13.cpp` (+ `StateDeclStmt::fixed` في المحلل والمفسّر). التوثيق: `docs/EXTRA13.md` · الاختبار: `tests/print/extra13.rin`.
## إصلاح الربط
- تعريف دوال `printlayout::mergePromptStyle/renderPrompt/promptText/styleWithText/pushFormStyle/popFormStyle` الناقصة في `rin_print_layout.cpp` (كانت تُسقط Engine tests وبناء APK بـ undefined reference).
