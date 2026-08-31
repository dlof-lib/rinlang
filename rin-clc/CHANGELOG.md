# Changelog

## 1.0.0 — الإصدار الأولي

- تصميم وتنفيذ صيغة CLC الثنائية من الصفر (رأس 96 بايت، metadata، dependency
  table، Rin symbol table، file index، block table، integrity، footer).
- Pipeline ضغط كامل: Rin/Text Optimization (قاموس رموز مشترك + استبدال عكوس)
  → Chunking → DEFLATE (entropy) مع اختيار STORE تلقائياً عند عدم الفائدة.
- Deduplication حقيقي بمحتوى الملف (sha256) — ملفات متطابقة تُخزَّن مرة واحدة.
- مستويات ضغط 0/1/2/3/4/ultra.
- أمان: منع Path Traversal، مسارات مطلقة، تحقق حدود صارم قبل أي تخصيص ذاكرة،
  فك ضغط محدود بالحجم الأصلي المُعلَن فقط.
- Streaming حقيقي للملفات الثنائية >8MB (قراءة/ضغط/كتابة على دفعات).
- CLI كامل: pack, unpack, list, info, check, verify, extract, convert, test.
- 14 اختبار ذاتي حقيقي (`clc test`) يغطي: مشروع فارغ، ملف واحد، ملفات كثيرة،
  ملف كبير، Unicode/عربي، ثنائي، تكرار، مجلدات متداخلة، مشروع Rin واقعي،
  ultra، رأس تالف، path traversal، تلف بيانات.
- `clc convert`: استيراد zip → rcl عبر قارئ ZIP داخلي مبسَّط.
- توثيق كامل: FORMAT.md, CLI.md, API.md, SECURITY.md, RIN_INTEGRATION.md.
- Benchmark حقيقي (غير مُصطنَع) مقابل zip وtar.gz — انظر benchmarks/results.md.

### معروف/موثَّق كقيد صريح (وليس عيباً مخفياً)
- ضغط "غير صلب" (كل ملف/كتلة منفصلة) — انظر README §القيود الحالية.
- الفهرس/الجداول تبقى في الذاكرة أثناء pack (بحجم O(عدد الملفات)).
- قاموس Rin عام بسقف 250 رمزاً.
- لا مُصدِّر rcl → zip بعد (فقط zip → rcl).
