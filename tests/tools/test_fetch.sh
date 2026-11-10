#!/bin/bash
# fetch.request() الحقيقي ضدّ tools/mock_fetch_server.py (docs/fetch.md).  usage: RIN=build/rin_run tests/tools/test_fetch.sh   (من جذر المستودع)
RIN=${RIN:-rin}; ROOT=$(cd "$(dirname "$0")/../.." && pwd); D=$(mktemp -d); PORT=$((20000 + RANDOM % 20000))
python3 $ROOT/tools/mock_fetch_server.py $PORT & SP=$!; trap 'kill $SP 2>/dev/null; rm -rf $D' EXIT; sleep 1
U=http://127.0.0.1:$PORT; fail=0; echo "FILE-BYTES" > $D/up.txt
run() { printf 'let U = "%s";\n%s\n' "$U" "$1" > $D/t.rin; $RIN $D/t.rin 2>&1 | grep -v '^🌐\|^✅'; }
chk() { local got; got=$(run "$2"); if [ "$got" == "$3" ]; then echo "  [PASS] $1"; else echo "  [FAIL] $1"; echo "    want: $3"; echo "    got : $got"; fail=1; fi; }
chk "json body + query + headers" 'let r = fetch.request(U + "/echo", {query: {a: 1, tag: ["x","y z"]}, headers: {"X-Foo": "bar"}, json: {n: 5}}); print r.status, r.ok, r.method, r.json["query"], r.json["headers"]["x-foo"], r.json["headers"]["content-type"], r.json["body"];' '200 true POST {"a": ["1"], "tag": ["x", "y z"]} bar application/json {"n":5}'
chk "redirects followed / not followed" 'let g = fetch.get(U + "/redirect/3"); let n = fetch.get(U + "/redirect/3", {follow: false}); print g.status, g.redirects, n.status, n.headers["location"];' '200 3 302 /redirect/2'
chk "shorthand methods" 'print fetch.post(U + "/echo", {a: 1}).json["method"], fetch.put(U + "/echo", "raw").json["body"], fetch.patch(U + "/echo").json["method"], fetch.delete(U + "/echo").json["method"], fetch.head(U + "/echo").status;' 'POST raw PATCH DELETE 200'
chk "404 is not ok but not an error" 'let r = fetch.get(U + "/status/404"); print r.ok, r.status, r.statusText, r.json["message"];' 'false 404 Not Found status 404'
chk "connection refused reports error" 'let r = fetch.get("http://127.0.0.1:1/x"); print r.ok, r.status, r.error;' 'false 0 connection refused / unreachable (curl 7)'
chk "retry on 503 until success" 'let f = fetch.request(U + "/flaky", {retry: 3, backoff: 10ms}); print f.status, f.attempts;' '200 3'
chk "retryOn limits retries" 'let n = fetch.request(U + "/status/503", {retry: 2, retryOn: [500]}); print n.status, n.attempts;' '503 1'
chk "Retry-After honored" 'let t = fetch.request(U + "/rate", {retry: 1}); print t.status, t.attempts;' '200 2'
chk "timeout" 'let s = fetch.request(U + "/slow?s=3", {timeout: 500ms}); print s.ok, s.error;' 'false timed out (curl 28)'
chk "cache" 'let a = fetch.request(U + "/hits", {cache: 10s}); let b = fetch.request(U + "/hits", {cache: 10s}); print a.cached, b.cached, a.json["hits"] == b.json["hits"];' 'false true true'
chk "cookies jar" 'fetch.request(U + "/cookie/set", {cookies: true}); print fetch.request(U + "/cookie/get", {cookies: true}).json["cookie"] != "", fetch.request(U + "/cookie/get").json["cookie"] == "", fetch.clearCookies(), fetch.request(U + "/cookie/get", {cookies: true}).json["cookie"] == "";' 'true true 1 true'
chk "basic + bearer" 'print fetch.request(U + "/basic").status, fetch.request(U + "/basic", {basic: ["sara", "pw"]}).status, fetch.request(U + "/echo", {bearer: "T"}).json["headers"]["authorization"];' '401 200 Bearer T'
chk "gzip transparently decoded" 'print fetch.request(U + "/gzip").json["gz"];' 'true'
chk "multipart upload" 'let up = fetch.request(U + "/upload", {multipart: {title: "hi", doc: {path: "'$D'/up.txt", type: "text/plain", filename: "a.txt"}}}); print up.json["ctype"], up.json["filenames"], up.json["hasFileBytes"];' 'multipart/form-data ["a.txt"] true'
chk "form urlencoded" 'print fetch.request(U + "/echo", {form: {a: "1 2"}}).json["body"];' 'a=1%202'
chk "image response" 'let p = fetch.request(U + "/pic"); print p.status, substr(p.image, 0, 22);' '200 data:image/png;base64,'
chk "download" 'let d = fetch.download(U + "/big", "'$D'/big.bin"); print d.ok, d.bytes, d.status;' 'true 200000 200'
chk "fetch.json / fetch.text" 'print fetch.json(U + "/echo", {query: {z: 1}})["query"], fetch.text(U + "/text");' '{"z": ["1"]} hello text'
got=$(run 'fetch.json(U + "/status/404");' | grep -c "failed: HTTP 404 Not Found"); if [ "$got" == 1 ]; then echo "  [PASS] strict throws"; else echo "  [FAIL] strict throws"; fail=1; fi
chk "into container + then + every" '@container.api=feed
    let data = nil; let status = 0; let error = nil; let loading = false;
.end/container.api
fetch.request(U + "/status/500", {into: "feed"}); print feed.status, feed.error;
let got = []; fun onr(x) { push(got, x.status); }
time.freeze(); let sc = fetch.request(U + "/echo", {every: 30s, times: 3, then: onr}); print sc.scheduled, sc.in; time.advance(95s); print got; time.unfreeze();' '500 HTTP 500: status 500
true 30000
[200, 200, 200]'
[ $fail == 0 ] && echo ALL PASS || { echo FAILED; exit 1; }
