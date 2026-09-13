# ملخص التغييرات (لم تُطبّق على GitHub فعلياً — راجع الرسالة)

## ملفات تم إنشاؤها
- .gitignore (جديد/محدّث)
- .github/workflows/cleanup-organize.yml (جديد)
- scripts/upload-secrets-local.sh (جديد)

## مجلدات/ملفات تم نقلها (المسار القديم -> الجديد)
- tools/test_*.cpp -> tests/tools/
- samples/* -> examples/samples/
- verification/* -> tests/verification/

## ملفات تم حذفها من المستودع (لن تجدها في هذا الـ zip لأنها محذوفة)
- signing/keystore-credentials.txt  (انقلها لـ GitHub Secret: ANDROID_KEYSTORE_CREDENTIALS)
- signing/release.keystore          (انقلها لـ GitHub Secret: ANDROID_KEYSTORE_BASE64)
- build-artifact/rin_run            (تُرفع كمرفق GitHub Release بدلاً من ذلك)
- build-artifact/rincheck           (تُرفع كمرفق GitHub Release بدلاً من ذلك)
