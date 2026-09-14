# إصلاح فشل البناء: حذف ملفات GETY/WGOM المتبقية (سكربت يحذف نفسه)

فكّوا ضغط هذا الأرشيف **في جذر مستودع rinlang-main مباشرة** (بحيث يندمج مجلد
`scripts/` مع مجلدات مشروعكم الموجودة أصلاً: `app/`, `firebase/`, ...).

## التنفيذ

```bash
chmod +x scripts/fix-ci/remove-leftover-files.sh
./scripts/fix-ci/remove-leftover-files.sh
git push
```

السكربت يقوم بكل شيء تلقائياً بترتيب واحد:
1. يحذف الملفات السبعة المتبقية من GETY/WGOM (`git rm` إن كان المجلد مستودع Git، وإلا `rm` عادي).
2. يعمل commit بحذفها.
3. **يحذف نفسه** (الملف `remove-leftover-files.sh`) من القرص ومن Git في commit منفصل، حتى لا يبقى أثر له في المشروع بعد أن أدّى غرضه.
4. يطلب منكم تنفيذ `git push` يدوياً في النهاية (لم يُنفَّذ تلقائياً تحسباً لأي مراجعة أخيرة منكم قبل الرفع).

بعدها سيختفي مجلد `scripts/fix-ci/` بالكامل من مشروعكم تلقائياً.

## الملفات المحذوفة
```
app/src/main/java/com/dlof/rinlang/auth/PairingRepository.kt
app/src/main/java/com/dlof/rinlang/auth/DevicePairingActivity.kt
app/src/main/res/layout/activity_device_pairing.xml
app/src/main/res/drawable/bg_pairing_qr_frame.xml
app/src/main/res/layout/activity_account.xml
app/src/main/res/values/strings_wgom.xml
app/src/main/res/drawable/bg_button_outline.xml
```

## بديل: خطوة CI مؤقتة فقط (بلا حذف فعلي من المستودع)
موجود في `.github/workflow-snippet/cleanup-step.yml` إن كنتم تفضّلون حل الـCI المؤقت
بدل حذف الملفات من المستودع نفسه — لكن لا داعي له إن استخدمتم السكربت أعلاه.

### Simple Container + Mask Style
Rin now provides a compact runtime style for common container/mask access:

```rin
app = container("App");
print container("App", "title");
container("App", "title", "Hello");
root = mask("app");
```

The full guide is in `docs/simple-style.md`.
