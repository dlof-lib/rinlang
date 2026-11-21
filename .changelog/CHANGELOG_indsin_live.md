# indsinlive — Rin يُحرّك واجهات indsin ويستجيب للمستخدم

- مكتبة جديدة مضمَّنة `lib/indsinlive.og.rin` (`tools/embed_indsinlive.py`): أحداث بأنماط `click:*` مع حرّاس/throttle/debounce،
  خانات قيم مرتبطة بخلايا warp، مناطق قوائم بفرق مفتاحي (`ilDiff/ilApply`: add/remove/move/update) مع `ilLoad/ilOptimistic/ilPoll`،
  نماذج (قواعد، رسائل عربي/إنجليزي، دورة idle→invalid|submitting→success|failed، حماية النقر المزدوج، أخطاء الخادم لكل حقل)،
  ومحرّك حركة (tween/sequence/stagger/countUp/مسبقات، مزج ألوان، تأثيرات indsin الأصلية عند توفّر مضيف).
- الزمن وسيط صريح (`ilSetTime/ilTick`) ⇒ اختبارات حتمية؛ التحويل من ساعة النظام لساعة المضيف يُعيد ضبط الحركات المعلّقة.
- ملفات: `docs/indsin_live.md` · `examples/samples/indsin_live.rin` · `tests/print/indsin_live.rin` (75 فحصاً) · `lib/README.md` · `indsin.md`.
