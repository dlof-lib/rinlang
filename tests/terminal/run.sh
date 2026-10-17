#!/usr/bin/env bash
# يشغّل اختبارات طرفية Rin:  RIN_BIN=./cli/linux/build/rin tests/terminal/run.sh
set -u
cd "$(dirname "$0")"
RIN_BIN="${RIN_BIN:-rin}"
export RIN_BIN
status=0

echo "== batch session =="
actual="$(RIN_LANG=en "$RIN_BIN" terminal --batch --no-color --no-history --no-banner < batch_session.in 2>&1)"
if [ "$actual" == "$(cat batch_session.expected)" ]; then echo "PASS batch_session"; else
  echo "FAIL batch_session"; diff <(echo "$actual") batch_session.expected | head -20; status=1; fi

echo "== projects + indsin (batch) =="
tmp="$(mktemp -d)"
pactual="$(cd "$tmp" && RIN_LANG=en "$RIN_BIN" terminal --batch --no-color --no-history --no-banner < "$OLDPWD/project_session.in" 2>&1 | sed -e "s#$tmp#<TMP>#g" -e "s/  [0-9]* ms/  N ms/")"
rm -rf "$tmp"
if [ "$pactual" == "$(cat project_session.expected)" ]; then echo "PASS project_session"; else
  echo "FAIL project_session"; diff <(echo "$pactual") project_session.expected | head -30; status=1; fi

echo "== extra commands ($ cat / $ cp / $ seq ... ) =="
xtmp="$(mktemp -d)"
xactual="$(cd "$xtmp" && RIN_LANG=en "$RIN_BIN" terminal --batch --no-color --no-history --no-banner < "$OLDPWD/xcmds_session.in" 2>&1)"
rm -rf "$xtmp"
if [ "$xactual" == "$(cat xcmds_session.expected)" ]; then echo "PASS xcmds_session"; else
  echo "FAIL xcmds_session"; diff <(echo "$xactual") xcmds_session.expected | head -30; status=1; fi

echo "== img (:img و rin img) =="
if (command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1) && python3 -c "import PIL" 2>/dev/null; then
  itmp="$(mktemp -d)"
  python3 - "$itmp/two colors.png" <<'PY'
import sys
from PIL import Image
img = Image.new('RGB', (40, 40), (255, 255, 255))
for y in range(20, 40):
    for x in range(40): img.putpixel((x, y), (255, 0, 0))
img.save(sys.argv[1])
PY
  img_ok=1
  # 1) braille عبر أنبوب (بلا ألوان): نصف الصورة العلوي مضاء ⣿ والسفلي أحمر (سطوعه أقل من المتوسط) فارغ
  got="$(RIN_LANG=en "$RIN_BIN" img "$itmp/two colors.png" -w 10 2>/dev/null | head -1)"
  [ "$got" == "⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿" ] || { echo "FAIL rin img braille (got: $got)"; img_ok=0; }
  # 2) :img داخل الطرفية بنمط half بلا ألوان: الأعلى █ والأسفل فراغ بعد قص الفراغات
  got="$(printf ':img "%s" -m half -w 8\n' "$itmp/two colors.png" | RIN_LANG=en "$RIN_BIN" terminal --batch --no-color --no-history --no-banner 2>&1 | head -1)"
  [ "$got" == "████████" ] || { echo "FAIL :img half (got: $got)"; img_ok=0; }
  # 3) أخطاء: ملف مفقود → 1، ملف نصي بامتداد png → 1، وسيط خاطئ → 2
  RIN_LANG=en "$RIN_BIN" img "$itmp/missing.png" >/dev/null 2>&1; [ $? -eq 1 ] || { echo "FAIL img missing exit code"; img_ok=0; }
  printf '<svg xmlns="http://www.w3.org/2000/svg"/>' > "$itmp/fake.png"
  RIN_LANG=en "$RIN_BIN" img "$itmp/fake.png" >/dev/null 2>&1; [ $? -eq 1 ] || { echo "FAIL img fake png not rejected"; img_ok=0; }
  RIN_LANG=en "$RIN_BIN" img "$itmp/two colors.png" -m nope >/dev/null 2>&1; [ $? -eq 2 ] || { echo "FAIL img bad mode exit code"; img_ok=0; }
  rm -rf "$itmp"
  if [ $img_ok -eq 1 ]; then echo "PASS img"; else status=1; fi
else
  echo "SKIP img (يحتاج ImageMagick و python3-pil)"
fi

echo "== interactive (pty) =="
python3 pty_test.py || status=1
exit $status
