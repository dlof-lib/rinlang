# banner.md — Banner: إشعار شريطي

Banner لها وجهان مختلفان: عنصر واجهة إعلاني (`@view.Banner`، يُبنى/يُقاس/يُرسَم عبر
indsin كأي عنصر آخر — انظر [`indsin.md`](../indsin.md)) ودوال أصلية مستقلة
(`bannerInfo`/`bannerSuccess`/...) لبث حدث Banner إلى المضيف (host)، بلا علاقة
مباشرة بالعنصر الأول.

## `@view.Banner` — العنصر
```rin
@view.Banner=b
  tone="info"; title="Hello"; message="World";
.end/view
```
`title=`/`message=` اختصار يُصنِّع تلقائيًا نصّين حقيقيّين بداخله (`b_title`
بحجم خط "title"، `b_message` بحجم خط "body") — نفس الأسلوب الذي تعتمده كل
المكوّنات المُركَّبة الأحدث (Tag/Breadcrumb/Pagination، انظر
[`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md#توسعة-مكتبة-uiux-الأحدث)).

### تأليف يدوي إضافي (اختياري)
يمكن تعشيش عناصر حقيقية أخرى بداخل Banner بجانب `title=`/`message=`:
```rin
@view.Banner=b
  title="Update"; message="Ready";
  @view.Button=go label="Go"; tone="primary"; .end/view
.end/view
```

### `closable=true` — زر إغلاق حقيقي
```rin
@view.Banner=notice
  title="Heads up"; closable=true;
.end/view
```
يُصنِّع زر إغلاق حقيقي، ويربط `visible=` تلقائيًا بخلية Warp باسم `"<name>_open"`
(هنا `notice_open`، تبدأ `"true"`) — النقر عليه فعليًا عبر Needle (`dispatchTap`)
يضبطها `"false"`، فينهار الـBanner إلى حجم صفري بعد إعادة القياس (نفس القاعدة
العامة "`visible=false` يعني حجم صفري" المُستخدَمة في كل مكان آخر، مثل
[`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md) لعنصر Tag القابل للحذف). بلا `closable=true`
لا يُصنَّع أي `visible=` تلقائي إطلاقًا — العنصر يبقى ببساطة بلا هذه الخاصية.

## الدوال الأصلية المستقلة (حدث Banner للمضيف)
منفصلة تمامًا عن `@view.Banner` أعلاه — تبث حدثًا (`BANNER_CREATED`/`BANNER_SHOWN`/
`BANNER_DISMISSED`) يمكن لتطبيق مضيف (Android مثلًا) الاستماع إليه لعرض Toast/Snackbar
أصلي، مع سطر مقروء في لوحة الإخراج:
```rin
bannerInfo("Saved successfully");    // ℹ️ [banner:info] Saved successfully
bannerSuccess("Great job");          // ✅ [banner:success] Great job
bannerWarning("Careful");            // ⚠️ [banner:warning] Careful
bannerError("Something failed");     // ❌ [banner:error] Something failed
bannerDismiss();                     // يبث BANNER_DISMISSED (بلا وسائط)
```
كل دالة من الأربع الأولى تأخذ وسيطًا نصيًا واحدًا إلزاميًا.

## انظر أيضًا
- [`indsin.md`](../indsin.md) — محرّك عرض `@view.Banner` نفسه.
- [`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md) — كتالوج عناصر `@element`/`@view` الكامل.
- [`android.md`](./android.md) — كيف يستمع مضيف أندرويد لأحداث مثل `BANNER_CREATED`.
