#!/usr/bin/env node
// Smoke/integration test for rin_indsin_live: spawns the real binary against a temp project and drives it over HTTP.
//   node tools/indsin-live/test_live.js [path/to/rin_indsin_live]        (default: dist/live/rin_indsin_live)
"use strict";
const cp = require("child_process"), fs = require("fs"), http = require("http"), os = require("os"), path = require("path");
const bin = process.argv[2] || path.join(__dirname, "..", "..", "dist", "live", process.platform === "win32" ? "rin_indsin_live.exe" : "rin_indsin_live");
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "indsin-live-test-"));
fs.mkdirSync(path.join(dir, "pages"));
fs.writeFileSync(path.join(dir, "main.indsin"), '<warp n={0} />\n<Column#root padding=20><Text#t>count {n}</Text><Button#b onTap={increment(n,1)}>+</Button><Button#go onTap="navigate:pages/two.rin">go</Button></Column>\n');
fs.writeFileSync(path.join(dir, "pages", "two.rin"), '@view.Column=Root\n  padding=20;\n  @view.Text=T text="page two"; .end/view\n.end/view\n');
fs.writeFileSync(path.join(dir, "secret.txt"), "nope");
let pass = 0, fail = 0;
const ok = (c, m) => { if (c) { pass++; console.log("  ok   " + m); } else { fail++; console.log("  FAIL " + m); } };
let port, key;
function req(method, p, body, headers) {
  return new Promise((resolve, reject) => {
    const data = body === undefined ? null : Buffer.from(typeof body === "string" ? body : JSON.stringify(body));
    const r = http.request({ host: "127.0.0.1", port, path: p, method, headers: Object.assign({}, data ? { "Content-Length": data.length } : {}, headers || {}) }, (res) => {
      const chunks = []; res.on("data", (c) => chunks.push(c)); res.on("end", () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
    });
    r.on("error", reject); if (data) r.write(data); r.end();
  });
}
const api = async (b) => { const r = await req("POST", "/api", b, { "X-Indsin-Key": key }); return { status: r.status, j: JSON.parse(r.body.toString("utf8")) }; };
const texts = (env) => env.paint.filter((p) => p.op === "text").map((p) => p.text);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const proc = cp.spawn(bin, [path.join(dir, "main.indsin"), "--no-open", "--json", "--port", "5950"], { cwd: dir });
  let out = ""; const info = await new Promise((res, rej) => { proc.stdout.on("data", (d) => { out += d; const m = /INDSIN_LIVE (\{.*\})/.exec(out); if (m) res(JSON.parse(m[1])); }); proc.on("error", rej); setTimeout(() => rej(new Error("server did not start")), 10000); });
  port = info.port; key = info.key;
  try {
    console.log("security");
    ok((await req("GET", "/")).status === 401, "page without key -> 401");
    ok((await req("POST", "/api", { cmd: "info" })).status === 401, "api without key -> 401");
    ok((await req("GET", "/?k=" + key, undefined, { Host: "evil.example" })).status === 403, "foreign Host header -> 403 (DNS rebinding)");
    const page = await req("GET", "/?k=" + key); ok(page.status === 200 && /<title>Indsin Live Preview/.test(page.body.toString()), "client page served with key");
    ok(/Content-Security-Policy/i.test(Object.keys(page.headers).join(",")) || !!page.headers["content-security-policy"], "client page carries a CSP");
    ok((await req("GET", "/f/..%2F..%2Fetc%2Fpasswd?k=" + key)).status === 403, "path traversal -> 403");
    ok((await req("GET", "/f/secret.txt?k=" + key)).status === 403, "non-media file type not served");
    console.log("engine session");
    let r = await api({ cmd: "open", cid: "c1", width: 390, vp: 844 });
    ok(r.status === 200 && r.j.page === "main.indsin" && texts(r.j.env).includes("count 0"), "open renders real engine frame (count 0)");
    ok(r.j.env.canvas.width === 390, "canvas width = requested 390");
    const btn = (function find(n) { if (n.name === "b") return n; for (const c of n.children || []) { const f = find(c); if (f) return f; } })(r.j.env.fabric);
    r = await api({ cmd: "tap", cid: "c1", x: btn.x + btn.w / 2, y: btn.y + btn.h / 2 });
    ok(r.j.env.handled && texts(r.j.env).includes("count 1"), "tap runs onTap in the engine (count 1)");
    r = await api({ cmd: "resize", cid: "c1", width: 768, vp: 1024 });
    ok(r.j.env.canvas.width === 768 && texts(r.j.env).includes("count 1"), "resize keeps Warp state and relayouts at 768");
    console.log("live editing");
    const sse = []; const es = http.get({ host: "127.0.0.1", port, path: "/events?cid=c1&k=" + key }, (res) => { res.setEncoding("utf8"); res.on("data", (c) => { for (const l of c.split("\n")) if (l.startsWith("data: ")) sse.push(JSON.parse(l.slice(6))); }); });
    await sleep(200);
    r = await api({ cmd: "source", text: '<warp n={0} />\n<Column#root padding=20><Text#t>edited {n} مرحبا</Text><Button#b onTap={increment(n,1)}>+</Button></Column>\n' });
    await sleep(400);
    ok(sse.some((f) => f.reason === "edit" && texts(f.env).includes("edited 1 مرحبا")), "editor-buffer push reaches the browser via SSE with Warp state kept (n=1) + Arabic intact");
    fs.writeFileSync(path.join(dir, "main.indsin"), '<warp n={0} />\n<Column#root><Text#t>from disk {n}</Text></Column>\n');
    await sleep(900);
    ok(sse.some((f) => f.reason === "file" && texts(f.env).includes("from disk 1")), "saving the file on disk is picked up by the watcher");
    fs.writeFileSync(path.join(dir, "main.indsin"), "@view.Column=X\n  bogus(((\n");
    await sleep(900);
    const bad = sse.filter((f) => f.reason === "file").pop();
    ok(bad && bad.env.error && bad.env.line === 2 && Array.isArray(bad.env.paint), "parse error: error+line reported AND last good frame kept");
    fs.writeFileSync(path.join(dir, "main.indsin"), '<warp n={0} />\n<Column#root padding=20><Text#t>fixed {n}</Text><Button#go onTap="navigate:pages/two.rin">go</Button></Column>\n');
    await sleep(900);
    ok(sse.filter((f) => f.reason === "file").pop().env.error === undefined, "fixing the file clears the error");
    es.destroy();
    console.log("navigation");
    r = await api({ cmd: "navigate", cid: "c1", target: "pages/two.rin" });
    ok(r.status === 200 && r.j.page === "pages/two.rin" && r.j.depth === 1 && texts(r.j.env).includes("page two"), "navigate pushes history and loads page two");
    r = await api({ cmd: "back", cid: "c1" }); ok(r.j.page === "main.indsin" && r.j.depth === 0, "back returns to main");
    ok((await api({ cmd: "navigate", cid: "c1", target: "../outside.rin" })).status === 404, "navigate outside the project refused");
    ok((await api({ cmd: "navigate", cid: "c1", target: "/etc/passwd" })).status === 404, "navigate to absolute path refused");
    console.log("misc");
    const shot = await req("GET", "/shot.png?cid=c1&k=" + key); ok(shot.status === 200 && shot.body.slice(1, 4).toString() === "PNG", "GET /shot.png returns a real PNG");
    r = await api({ cmd: "inspect", cid: "c1", what: "audit" }); ok(r.j.ok && r.j.data && typeof r.j.data.score === "number", "audit endpoint returns the engine's a11y/layout report");
    r = await api({ cmd: "tick", cid: "c1" }); ok(r.j.reason === "tick" && "animating" in r.j.env, "tick endpoint (clock / effects)");
    ok((await api({ cmd: "tap", cid: "ghost", x: 1, y: 1 })).status === 410, "unknown session -> 410 (client re-opens)");
  } catch (e) { fail++; console.log("  FAIL exception: " + e.stack); }
  proc.kill(); fs.rmSync(dir, { recursive: true, force: true });
  console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
