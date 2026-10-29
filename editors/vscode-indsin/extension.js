// indsin — VS Code integration for the Indsin Live Preview server (tools/indsin-live/).
//
// "Indsin: Open Live Preview" starts rin_indsin_live for the active .indsin/.rin file, shows its page in a
// webview beside the editor and pushes the *unsaved editor buffer* to it on every keystroke, so the preview
// is live while typing (the real native engine renders every frame; this file only wires the editor to it).
// Works on Windows, macOS and Linux (and in remote workspaces through vscode.env.asExternalUri).
"use strict";
const vscode = require("vscode");
const cp = require("child_process");
const fs = require("fs");
const http = require("http");
const path = require("path");

/** @type {Map<string, {proc: any, info: any, lan: string[], panel: any, key: string}>} */
const servers = new Map();
let statusItem;
let debounce = new Map();

function cfg() { return vscode.workspace.getConfiguration("indsin.livePreview"); }
function exeName() { return process.platform === "win32" ? "rin_indsin_live.exe" : "rin_indsin_live"; }

function findBinary() {
  const candidates = [];
  const configured = cfg().get("serverPath");
  if (configured) candidates.push(configured);
  if (process.env.RIN_INDSIN_LIVE) candidates.push(process.env.RIN_INDSIN_LIVE);
  for (const f of vscode.workspace.workspaceFolders || []) {
    candidates.push(path.join(f.uri.fsPath, "dist", "live", exeName()));
    candidates.push(path.join(f.uri.fsPath, "build-live", exeName()));
  }
  candidates.push(path.join(__dirname, "bin", exeName()));
  for (const c of candidates) { try { if (c && fs.statSync(c).isFile()) return c; } catch (_) {} }
  // fall back to PATH lookup
  const sep = process.platform === "win32" ? ";" : ":";
  for (const dir of (process.env.PATH || "").split(sep)) {
    const c = path.join(dir, exeName()); try { if (fs.statSync(c).isFile()) return c; } catch (_) {}
  }
  return null;
}

function startServer(file, lan) {
  return new Promise((resolve, reject) => {
    const bin = findBinary();
    if (!bin) return reject(new Error("NO_BINARY"));
    const args = [file, "--no-open", "--json", "--port", String(cfg().get("port") || 5757)];
    if (lan) args.push("--lan");
    const proc = cp.spawn(bin, args, { cwd: path.dirname(file), windowsHide: true });
    let buf = "", done = false; const lanUrls = [];
    const finish = (err, info) => { if (done) return; done = true; err ? reject(err) : resolve({ proc, info, lan: lanUrls }); };
    const onData = (d) => {
      buf += d.toString("utf8");
      let i;
      while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1);
        if (line.startsWith("INDSIN_LIVE ")) { try { finish(null, JSON.parse(line.slice(12))); } catch (e) { finish(e); } }
        const m = /network\s*:\s*(http\S+)/.exec(line); if (m) lanUrls.push(m[1]);
      }
    };
    proc.stdout.on("data", onData); proc.stderr.on("data", (d) => console.error("[indsin-live]", d.toString()));
    proc.on("error", (e) => finish(e));
    proc.on("exit", (code) => { finish(new Error("server exited with code " + code)); for (const [k, v] of servers) if (v.proc === proc) { servers.delete(k); } updateStatus(); });
    setTimeout(() => finish(new Error("timed out starting the live preview server")), 15000);
  });
}

function post(info, body) {
  return new Promise((resolve) => {
    const data = Buffer.from(JSON.stringify(body), "utf8");
    const req = http.request({ host: "127.0.0.1", port: info.port, path: "/api", method: "POST",
      headers: { "X-Indsin-Key": info.key, "Content-Type": "application/json", "Content-Length": data.length } },
      (res) => { res.resume(); res.on("end", () => resolve(res.statusCode)); });
    req.on("error", () => resolve(0)); req.end(data);
  });
}

function webviewHtml(url, nonce) {
  const origin = new URL(url).origin;
  return `<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; frame-src ${origin} http://127.0.0.1:* http://localhost:* https:; style-src 'unsafe-inline';">
<style>html,body,iframe{margin:0;padding:0;width:100%;height:100%;border:0;overflow:hidden;background:transparent}</style></head>
<body><iframe src="${url}" allow="clipboard-read; clipboard-write; fullscreen"></iframe></body></html>`;
}

async function ensureServer(doc, lan) {
  const file = doc.uri.fsPath;
  let s = servers.get(file);
  if (s && lan !== undefined && (s.lan.length > 0) !== !!lan) { stopServer(file); s = null; }
  if (!s) {
    s = await startServer(file, !!lan);
    s.key = file; servers.set(file, s);
    if (cfg().get("pushUnsavedChanges") && doc.isDirty) post(s.info, { cmd: "source", text: doc.getText() });
  }
  updateStatus();
  return s;
}

function stopServer(file) {
  const s = servers.get(file); if (!s) return;
  try { if (s.panel) s.panel.dispose(); } catch (_) {}
  try { s.proc.kill(); } catch (_) {}
  servers.delete(file); updateStatus();
}

function activeDoc() {
  const ed = vscode.window.activeTextEditor;
  if (ed && /\.(indsin|rin)$/i.test(ed.document.fileName) && ed.document.uri.scheme === "file") return ed.document;
  vscode.window.showWarningMessage("Open a saved .indsin or .rin file first.");
  return null;
}

async function noBinary() {
  const pick = await vscode.window.showErrorMessage(
    "Indsin live preview server (rin_indsin_live) was not found. Build it with tools/indsin-live/build.sh, or set indsin.livePreview.serverPath.",
    "Open settings");
  if (pick) vscode.commands.executeCommand("workbench.action.openSettings", "indsin.livePreview.serverPath");
}

async function openPreview(inBrowser) {
  const doc = activeDoc(); if (!doc) return;
  let s;
  try { s = await ensureServer(doc); }
  catch (e) { if (e.message === "NO_BINARY") return noBinary(); return vscode.window.showErrorMessage("Indsin live preview: " + e.message); }
  const url = `http://127.0.0.1:${s.info.port}/?k=${s.info.key}`;
  if (inBrowser) { vscode.env.openExternal(vscode.Uri.parse(url)); return; }
  if (s.panel) { s.panel.reveal(undefined, true); return; }
  const ext = await vscode.env.asExternalUri(vscode.Uri.parse(url));
  const panel = vscode.window.createWebviewPanel("indsinLive", "Indsin Live — " + path.basename(doc.fileName),
    { viewColumn: vscode.ViewColumn.Beside, preserveFocus: true }, { enableScripts: true, retainContextWhenHidden: true });
  panel.webview.html = webviewHtml(ext.toString(true));
  s.panel = panel;
  panel.onDidDispose(() => { if (s.panel === panel) s.panel = null; });
}

async function shareOnNetwork() {
  const doc = activeDoc(); if (!doc) return;
  let s;
  try { s = await ensureServer(doc, true); } catch (e) { if (e.message === "NO_BINARY") return noBinary(); return vscode.window.showErrorMessage(e.message); }
  const urls = s.lan.length ? s.lan : [`http://127.0.0.1:${s.info.port}/?k=${s.info.key}`];
  const pick = await vscode.window.showInformationMessage(
    "Open this URL on a phone/tablet on the same network (anyone with the key in the URL can drive the preview):\n" + urls[0], "Copy URL");
  if (pick) vscode.env.clipboard.writeText(urls[0]);
}

function stopAll() { for (const f of [...servers.keys()]) stopServer(f); }
function updateStatus() {
  if (!statusItem) return;
  if (servers.size) { statusItem.text = "$(broadcast) Indsin Live"; statusItem.tooltip = "Indsin live preview is running — click to stop"; statusItem.command = "indsin.stopLivePreview"; statusItem.show(); }
  else statusItem.hide();
}

function activate(context) {
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
  context.subscriptions.push(statusItem,
    vscode.commands.registerCommand("indsin.openLivePreview", () => openPreview(false)),
    vscode.commands.registerCommand("indsin.openLivePreviewInBrowser", () => openPreview(true)),
    vscode.commands.registerCommand("indsin.shareLivePreviewOnNetwork", shareOnNetwork),
    vscode.commands.registerCommand("indsin.stopLivePreview", stopAll),
    // live as you type: push the unsaved buffer (debounced) to the server, which hot-diffs it into every open preview
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (!cfg().get("pushUnsavedChanges")) return;
      const file = e.document.uri.fsPath, s = servers.get(file); if (!s) return;
      clearTimeout(debounce.get(file));
      debounce.set(file, setTimeout(() => post(s.info, { cmd: "source", text: e.document.getText() }), 60));
    }),
    { dispose: stopAll });
}
function deactivate() { stopAll(); }
module.exports = { activate, deactivate };
