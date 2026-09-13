#!/usr/bin/env bash
# نفّذوا هذا السكربت من جذر مشروع rinlang-main (نفس المجلد الذي يحوي app/ وfirebase/).
# يحذف الملفات المتبقية من GETY/WGOM التي تسبب فشل Aapt2 (resource linking) في البناء،
# يعمل commit واحد بكل شيء، ثم يحذف نفسه (هذا السكربت) من القرص ومن Git أيضاً.
set -e

FILES=(
  "app/src/main/java/com/dlof/rinlang/auth/PairingRepository.kt"
  "app/src/main/java/com/dlof/rinlang/auth/DevicePairingActivity.kt"
  "app/src/main/res/layout/activity_device_pairing.xml"
  "app/src/main/res/drawable/bg_pairing_qr_frame.xml"
  "app/src/main/res/layout/activity_account.xml"
  "app/src/main/res/values/strings_wgom.xml"
  "app/src/main/res/drawable/bg_button_outline.xml"
)

IS_GIT_REPO=false
if git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
  IS_GIT_REPO=true
fi

for f in "${FILES[@]}"; do
  if [ -f "$f" ]; then
    if $IS_GIT_REPO; then
      git rm -f -- "$f"
    else
      rm -f -- "$f"
    fi
    echo "تم حذف: $f"
  else
    echo "غير موجود أصلاً (تخطّي): $f"
  fi
done

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"

if $IS_GIT_REPO; then
  git add -A
  git commit -m "Remove leftover GETY/WGOM files causing resource linking failure" || true
  # حذف السكربت نفسه من Git في التزام منفصل حتى تبقى رسالة الالتزام الأولى واضحة عن سبب الحذف الفعلي
  if git ls-files --error-unmatch "$SELF" > /dev/null 2>&1; then
    git rm -f -- "$SELF"
    git commit -m "Remove self-deleting cleanup script (job done)" || true
  fi
  echo "تم عمل commit. الآن نفّذوا: git push"
else
  echo "تحذير: هذا ليس مستودع Git — تم حذف الملفات من القرص فقط بلا commit."
fi

rm -f -- "$SELF"
echo "تم حذف السكربت نفسه. انتهى."
