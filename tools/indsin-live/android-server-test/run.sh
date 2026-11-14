#!/usr/bin/env bash
# يترجم IndsinLiveServer.kt وIndsinLiveService.kt الحقيقيين مقابل واجهات وهمية لأندرويد/المحرّك ويشغّله ويفحص HTTP (بدون جهاز).
# المتطلبات: java 17+، kotlinc 2.0.x في PATH (أو KOTLINC=...)، وorg.json (JSON_JAR=... أو مصدر JSON-java في JSON_SRC).
# الاستعمال من جذر المستودع:  tools/indsin-live/android-server-test/run.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd); T=$(mktemp -d); KOTLINC=${KOTLINC:-kotlinc}
CP=${JSON_JAR:-}
if [ -z "$CP" ]; then mkdir -p "$T/json"; javac -nowarn -d "$T/json" ${JSON_SRC:?set JSON_JAR or JSON_SRC}/*.java; CP="$T/json"; fi
export INDSIN_CLIENT="$ROOT/tools/indsin-live/client/index.html" INDSIN_PROJ="$T/proj"; mkdir -p "$T/proj"
printf 'ABCDEFGHIJ' > "$T/proj/a.png"; echo 'Text("hi")' > "$T/proj/main.indsin"; echo secret > "$T/secret.txt"; ln -s "$T/secret.txt" "$T/proj/leak.png"
HERE=$(dirname "$0")
$KOTLINC -nowarn -cp "$CP" "$HERE"/stubs/*.kt "$ROOT/app/src/main/java/com/dlof/rinlang/IndsinLiveServer.kt" "$ROOT/app/src/main/java/com/dlof/rinlang/IndsinLiveService.kt" -d "$T/out"
STD=$(dirname "$(readlink -f "$(which $KOTLINC)")")/../lib/kotlin-stdlib.jar
java -cp "$T/out:$CP:$STD" MainKt > "$T/run.log" 2>&1 & PID=$!; trap 'kill $PID 2>/dev/null' EXIT
for i in $(seq 1 30); do grep -q '^URL' "$T/run.log" && break; sleep 0.3; done
U=$(grep -o 'http://[^ ]*' "$T/run.log" | head -1); K=$(echo "$U" | grep -o 'k=[0-9a-f]*' | cut -d= -f2); B=${U%%\?*}; B=${B%/}
fail=0; chk(){ if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 (got $2, want $3)"; fail=1; fi; }
c(){ curl -s -o /dev/null -w '%{http_code}' "$@"; }
chk "navigate cross-site (Chrome من التطبيق)" "$(c -H 'Sec-Fetch-Site: cross-site' -H 'Sec-Fetch-Mode: navigate' "$U")" 200
chk "navigate none"            "$(c -H 'Sec-Fetch-Site: none' -H 'Sec-Fetch-Mode: navigate' "$U")" 200
chk "fetch cross-site مرفوض"   "$(c -H 'Sec-Fetch-Site: cross-site' -H 'Sec-Fetch-Mode: cors' "$U")" 403
chk "Host خاطئ مرفوض"          "$(c -H 'Host: evil.com' "$U")" 403
chk "بلا مفتاح"                "$(c "$B/")" 401
chk "مفتاح خاطئ"               "$(c "$B/?k=000")" 401
chk "ملف وسائط"                "$(c -H "X-Indsin-Key: $K" "$B/f/a.png")" 200
chk "Range"                    "$(c -H "X-Indsin-Key: $K" -H 'Range: bytes=2-4' "$B/f/a.png")" 206
chk "هروب .."                  "$(c -H "X-Indsin-Key: $K" --path-as-is "$B/f/../../secret.txt")" 403
chk "هروب مشفّر"               "$(c -H "X-Indsin-Key: $K" --path-as-is "$B/f/%2e%2e/secret.txt")" 403
chk "رابط رمزي هارب"           "$(c -H "X-Indsin-Key: $K" "$B/f/leak.png")" 403
chk "ملف مصدر غير مخدوم"       "$(c -H "X-Indsin-Key: $K" "$B/f/main.indsin")" 403
chk "Content-Length ضخم"       "$(c -X POST -H "X-Indsin-Key: $K" -H 'Content-Length: 99999999999' --data x "$B/api")" 400
chk "الصفحة تحقن meta المفتاح" "$(curl -s "$U" | grep -c "name=\"indsin-key\" content=\"$K\"")" 1
chk "كوكي HttpOnly+Strict"     "$(curl -s -D - -o /dev/null "$U" | grep -ci "^set-cookie: indsin_k_[0-9]*=$K; Path=/; HttpOnly; SameSite=Strict")" 1
chk "الكوكي وحده يكفي (تحديث الصفحة)" "$(c -H "Cookie: indsin_k_${B##*:}=$K" "$B/")" 200
chk "كوكي منفذ آخر لا يكفي"    "$(c -H "Cookie: indsin_k_1=$K" "$B/")" 401
chk "كوكي خاطئ"                "$(c -H "Cookie: indsin_k_${B##*:}=bad" "$B/")" 401
python3 -I - "${B##*:}" > "$T/flood.txt" <<'PY'
import socket,sys,time
port=int(sys.argv[1]); held=[]
for i in range(90):
    try: s=socket.create_connection(('127.0.0.1',port),timeout=3); held.append(s)
    except Exception: pass
time.sleep(0.5)
s=socket.create_connection(('127.0.0.1',port),timeout=5); s.send(b'GET / HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n'%port)
print(s.recv(200).split(b'\r\n')[0].decode())
PY
chk "فيضان الاتصالات → 503" "$(cat "$T/flood.txt")" "HTTP/1.1 503 Service Unavailable"
sleep 16  # تنتهي مهلة الاتصالات المعلّقة (15 ث) ويعود الخادم للعمل
chk "الخادم يتعافى بعد الفيضان" "$(c "$U")" 200
api(){ curl -s -X POST -H "X-Indsin-Key: $K" -H 'Content-Type: application/json' -d "$1" "$B/api"; }
# ---- الميزات الأساسية: تحرير حيّ + مراقب الملفات + رفع الوسائط
api '{"cmd":"open","cid":"live1","width":390,"vp":800}' >/dev/null
curl -sN --max-time 9 -H "X-Indsin-Key: $K" "$B/events?cid=live1" > "$T/sse.txt" 2>/dev/null &
SSEPID=$!; sleep 1
api '{"cmd":"source","cid":"live1","page":"main.indsin","text":"EDITED_LIVE_1"}' >/dev/null; sleep 1.5
chk "تحرير حيّ يصل SSE قبل الحفظ" "$(grep -c EDITED_LIVE_1 "$T/sse.txt")" 1
printf 'DISK_EDIT_2' > "$T/proj/main.indsin"; sleep 2.5
chk "مراقب الملفات يدفع تعديل القرص" "$(grep -c DISK_EDIT_2 "$T/sse.txt")" 1
wait $SSEPID 2>/dev/null || true
R=$(curl -s -X POST -H "X-Indsin-Key: $K" -H 'Content-Type: application/octet-stream' --data-binary 'PNGDATA' "$B/m?name=pic.png&mime=image/png")
chk "رفع وسائط ينجح" "$(echo "$R" | grep -c '"ok":true')" 1
chk "الملف المرفوع داخل media/" "$(ls "$T"/proj/media/pic_*.png 2>/dev/null | wc -l)" 1
curl -s -o /dev/null -X POST -H "X-Indsin-Key: $K" --data-binary x "$B/m?name=../../evil.txt&mime=text/plain"
chk "اسم رفع هارب لا يخرج من media/" "$(ls "$T/evil.txt" "$T/proj/evil.txt" 2>/dev/null | wc -l)" 0
chk "رفع بلا مفتاح مرفوض" "$(c -X POST --data-binary x "$B/m?name=a.png")" 401
for i in $(seq 1 15); do api "{\"cmd\":\"open\",\"cid\":\"tab$i\",\"width\":390,\"vp\":800}" >/dev/null; done
N=$(api '{"cmd":"info","cid":"tab15"}' | grep -o '"clients":[0-9]*' | cut -d: -f2)
chk "سقف الجلسات (15 تبويباً → 12)" "$N" 12
chk "الخدمة بدأت عند الفتح" "$(grep -c '^FGS_START' "$T/run.log")" 1
exit $fail
