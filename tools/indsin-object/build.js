#!/usr/bin/env node
'use strict';
/**
 * Builds the self-contained host page and copies it where hosts expect it.
 *   node build.js            → ./indsin_object.html (+ Android asset copy when the repo layout is present)
 * No dependencies. The core is inlined verbatim; "</script" sequences inside string literals are escaped.
 */
const fs = require('fs');
const path = require('path');

const here = __dirname;
const core = fs.readFileSync(path.join(here, 'src', 'indsin_object.js'), 'utf8');
const hl = fs.existsSync(path.join(here, 'src', 'indsin_highlight.js')) ? fs.readFileSync(path.join(here, 'src', 'indsin_highlight.js'), 'utf8') : '';
const tpl = fs.readFileSync(path.join(here, 'src', 'host.template.html'), 'utf8');
const version = JSON.parse(fs.readFileSync(path.join(here, 'package.json'), 'utf8')).version;

const safe = (s) => s.replace(/<\/script/gi, '<\\/script').replace(/<!--/g, '<\\!--');
const html = tpl.replace('__VERSION__', version).replace('/*__CORE__*/', () => safe(core) + (hl ? '\n' + safe(hl) : ''));
const out = path.join(here, 'indsin_object.html');
fs.writeFileSync(out, html);
console.log('wrote', path.relative(process.cwd(), out), (html.length / 1024).toFixed(1) + ' KiB');

// Android asset (repo layout: tools/indsin-object → app/src/main/assets/indsin)
const assets = path.resolve(here, '..', '..', 'app', 'src', 'main', 'assets', 'indsin');
if (fs.existsSync(path.resolve(here, '..', '..', 'app', 'src', 'main'))) {
  fs.mkdirSync(assets, { recursive: true });
  fs.copyFileSync(out, path.join(assets, 'indsin_object.html'));
  console.log('copied to', path.relative(process.cwd(), path.join(assets, 'indsin_object.html')));
}
