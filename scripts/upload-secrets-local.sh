#!/usr/bin/env bash
# شغّل هذا الملف مرة واحدة فقط من جهازك (ليس من GitHub Actions)
# يتطلب: gh CLI مثبت ومسجّل دخول (gh auth login)
set -euo pipefail

REPO="OWNER/REPO"   # عدّل هذا إلى اسم مستودعك، مثال: rinlang/rinlang

if ! command -v gh &>/dev/null; then
  echo "GitHub CLI (gh) غير مثبت. ثبّته من https://cli.github.com"
  exit 1
fi

gh auth status || { echo "سجّل الدخول أولاً: gh auth login"; exit 1; }

echo "==> رفع signing/release.keystore كسر باسم ANDROID_KEYSTORE_BASE64"
base64 -w0 signing/release.keystore | gh secret set ANDROID_KEYSTORE_BASE64 --repo "$REPO"

echo "==> رفع signing/keystore-credentials.txt كسر باسم ANDROID_KEYSTORE_CREDENTIALS"
gh secret set ANDROID_KEYSTORE_CREDENTIALS --repo "$REPO" < signing/keystore-credentials.txt

echo "==> تم رفع الأسرار بنجاح إلى $REPO"
echo "الآن يمكنك تشغيل workflow التنظيف (cleanup-organize.yml) بأمان لحذف الملفات من المستودع."
