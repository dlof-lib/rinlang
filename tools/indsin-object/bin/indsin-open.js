#!/usr/bin/env node
'use strict';
/**
 * indsin-open — the launcher registered for the .indsin file association (double-click / "Open with").
 *
 *   indsin-open <file.indsin> [--source] [--no-open] [--theme dark|light] [--width 390]
 *
 * With the Rin indsin engine available (RIN_INDSIN_RUN or --engine) it renders a live preview
 * (translator + interpreter). Without an engine — or with --source — it opens a syntax-highlighted
 * view of the file, so a double-click always shows something useful. Output goes to the OS temp dir
 * and opens in the default browser. No dependencies.
 */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn, spawnSync } = require('child_process');
const HL = require('../src/indsin_highlight.js');

function usage(code) {
  (code ? console.error : console.log)('usage: indsin-open <file.indsin> [--source] [--no-open] [--theme dark|light] [--width N] [--engine PATH]');
  process.exit(code);
}
const args = process.argv.slice(2), o = { theme: 'dark', width: 390 };
let file = null;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '-h' || a === '--help') usage(0);
  else if (a === '--source') o.source = true;
  else if (a === '--no-open') o.noOpen = true;
  else if (a === '--theme') o.theme = args[++i] === 'light' ? 'light' : 'dark';
  else if (a === '--width') o.width = parseInt(args[++i], 10) || 390;
  else if (a === '--engine') o.engine = args[++i];
  else if (!file) file = a; else usage(2);
}
if (!file) usage(2);
file = path.resolve(file);
if (!fs.existsSync(file)) { console.error('indsin-open: file not found: ' + file); process.exit(2); }

const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const outDir = fs.mkdtempSync(path.join(os.tmpdir(), 'indsin-open-'));
const outFile = path.join(outDir, path.basename(file).replace(/[^\w.-]/g, '_') + '.html');
const engine = o.engine || process.env.RIN_INDSIN_RUN;
let wrote = false;

if (!o.source && engine && fs.existsSync(engine)) {
  const r = spawnSync(process.execPath, [path.join(__dirname, 'indsin-object.js'), file, '--engine', engine, '--theme', o.theme, '--width', String(o.width), '--title', path.basename(file), '-o', outFile], { encoding: 'utf8' });
  if (r.status === 0 && fs.existsSync(outFile)) wrote = true; else console.error('indsin-open: engine render failed, showing source instead.\n' + (r.stderr || ''));
}
if (!wrote) {
  const src = fs.readFileSync(file, 'utf8');
  const page = '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>' + esc(path.basename(file)) + ' — indsin</title>' +
    '<style>:root{color-scheme:light dark}body{margin:0;padding:16px;font:14px system-ui,sans-serif;background:#f6f6fb;color:#24292f}@media (prefers-color-scheme:dark){body{background:#11111b;color:#cdd6f4}}h1{font-size:15px;margin:0 0 10px;font-weight:600}h1 small{opacity:.6;font-weight:400}' + HL.stylesheet() + '</style></head><body><h1>' + esc(path.basename(file)) + ' <small>— source (set RIN_INDSIN_RUN for a live preview)</small></h1>' +
    HL.highlight(src, { lineNumbers: true }) + '</body></html>';
  fs.writeFileSync(outFile, page);
}
console.log(outFile);
if (o.noOpen) process.exit(0);
const url = outFile;
let cmd, cargs;
if (process.platform === 'win32') { cmd = 'cmd'; cargs = ['/c', 'start', '', url]; }
else if (process.platform === 'darwin') { cmd = 'open'; cargs = [url]; }
else { cmd = 'xdg-open'; cargs = [url]; }
const ch = spawn(cmd, cargs, { stdio: 'ignore', detached: true });
ch.on('error', (e) => { console.error('indsin-open: could not launch a browser (' + e.message + '). Open the file above manually.'); process.exit(1); });
ch.unref();
