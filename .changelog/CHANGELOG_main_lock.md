## main.lock — خزنة الأسرار المشفّرة (AES-256-GCM + PBKDF2)
- صيغة: `main.lock(5);` · `let <<main.lock["K"]>> = v;` · `<<main.lock["K"]>>` · `<<main.lock[]>>` · `let <<main.lock[]>>;`
- natives: set/get/has/names/remove/env/mask/seal/open/store/restore/status/report/reset (rin_lock.cpp، مُضمَّن في rin_interpreter.cpp)
- `print` يخفي الأسرار المكشوفة تلقائياً (`••••`)
- التوثيق: `docs/main-lock.md` · الاختبار: `tests/print/main_lock.rin`
