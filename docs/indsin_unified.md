# indsin — ربط المحرّك الأصلي (C++) بمترجم الويب (HTML/JS) في أداة واحدة

الفكرة: **مصدر `.rin` واحد ⇒ ناتجان متكافئان**، وصفحة هجينة تعرضهما معاً.

```
              ┌── محرّك Indsin الأصلي (C++: Fabric → Indsin → Dye) ──► DrawList JSON ─► PNG / HTML ثابت
 app.rin ─────┤
              └── مترجم الويب (indsin2web) ──► HTML + CSS + JS تفاعلي
                                   └── --hybrid: يضمّ DrawList + راسم canvas (JS) + زرّ web / native / split
```

## الاستعمال
```
bash scripts/build_indsin.sh                       # يبني build/indsin (محرّك + مترجم ويب في ملف واحد)
indsin build  app.rin -o dist --hybrid             # dist/app.html (هجين) + app.native.{json,png,html}
indsin build  app.rin -o dist --pwa --lang ar      # أي خيار من indsin2web يُمرَّر كما هو
indsin native app.rin --png out.png --width 320    # المحرّك الأصلي وحده
indsin web    app.rin -o app.html                  # مترجم الويب وحده
indsin check  app.rin [--strict]                   # تحذيرات الويب + تدقيق المحرّك الأصلي
```
في الصفحة الهجينة زرّ ثابت أسفل اليسار يدور بين `web` ← `native` ← `split` (الاثنان جنباً إلى جنب)، و`window.indsinNative` يكشف `data` (الـ DrawList) و`canvas` لأي كود JS.

## ما رُبط
| الطبقة | المساهمة |
|---|---|
| C++ | `tools/indsin.cpp` يضمّ `indsin2web.cpp` (بإعادة تسمية `main`) ويستدعي C API للمحرّك: `session_create/render_json/export_html/export_png/audit/stats` |
| JS | `kHybridJs` في `indsin2web_runtime.inc`: راسم canvas لعمليات `fill_rect/stroke_rect/text/stroke_arc/scrim_rect` (زوايا القوس بنفس اصطلاح Android) |
| HTML | `indsin2web --embed-native draw.json`: يضمّن JSON داخل `<script type="application/json" id="indsin-native">` (مع تهريب `</`) |
| اصطلاحات موحَّدة | مكوّنات `Sparkline/BarChart/ProgressRing` تقبل في الويب نفس سمات المحرّك: `data="تسمية:قيمة,…"` · `diameter` · `values=true` |

## التكافؤ
`tests/indsin_unified/browser_hybrid.js` يتحقق أن كل نصّ يرسمه المحرّك الأصلي موجود في صفحة الويب (15/15 في العيّنة) وأن الـ canvas مرسوم فعلاً وأن التبديل يعمل.

## حدود صريحة
* الجزء التفاعلي (hover، مسارات، DataTable، reveal…) في الويب فقط؛ الأصلي رسم ساكن (انظر `indsin_pro_native.md`).
* مظهر المحرّك الأصلي (ثيمه الداكن وخطّه) يختلف عن ثيم الويب؛ التكافؤ في **البنية والنصوص والبيانات** لا في البكسل.
* راسم canvas يرسم DrawList كما هي؛ بلا تدرجات/صور لأن `DrawOp` الحالية لا تحملها.
* يحتاج بناء `indsin` نفس مصادر وقت التشغيل كهدف `indsinc` (أول بناء بطيء ثم تُعاد الكائنات عند التغيّر فقط).
* الإخراج الأصلي لا يستعمل خط النظام للعربية في PNG (الخط المدمج ASCII)، لكن راسم canvas في الصفحة يستعمله فيظهر العربي صحيحاً.
