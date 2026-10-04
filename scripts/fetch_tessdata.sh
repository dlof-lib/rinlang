#!/usr/bin/env bash
# يضمّن ملفات لغات Tesseract داخل الـ APK لـ make.ocr على أندرويد (العربية + الإنجليزية افتراضياً).
#   scripts/fetch_tessdata.sh            # ara eng
#   scripts/fetch_tessdata.sh ara eng fas  # لغات إضافية (الفارسية...)
# المصدر: tessdata_fast (نماذج أصغر وأسرع، ~1–4MB للغة). يحتاج إنترنت مرة واحدة وقت البناء فقط؛
# بعدها يعمل OCR على الجهاز بلا اتصال. الملفات تُنسخ أول استعمال إلى filesDir/tessdata.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=app/src/main/assets/tessdata
BASE=https://github.com/tesseract-ocr/tessdata_fast/raw/main
LANGS=("$@"); [ ${#LANGS[@]} -eq 0 ] && LANGS=(ara eng)
mkdir -p "$OUT"
for l in "${LANGS[@]}"; do
  [[ "$l" =~ ^[A-Za-z0-9_]+$ ]] || { echo "اسم لغة غير صالح: $l" >&2; exit 1; }
  echo "→ $l.traineddata"
  curl -fL --retry 3 -o "$OUT/$l.traineddata.part" "$BASE/$l.traineddata"
  size=$(wc -c < "$OUT/$l.traineddata.part")
  if [ "$size" -lt 100000 ]; then echo "الملف المنزَّل لـ $l صغير جداً ($size بايت) — تالف؟" >&2; rm -f "$OUT/$l.traineddata.part"; exit 1; fi
  mv "$OUT/$l.traineddata.part" "$OUT/$l.traineddata"
done
echo "تم: $(ls "$OUT" | tr '\n' ' ')"
