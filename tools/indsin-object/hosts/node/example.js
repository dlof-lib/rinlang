'use strict';
/**
 * Node example: translate an Indsin frame to a standalone HTML page and a highlighted source page.
 *   node hosts/node/example.js [frame.json] [out-dir]
 */
const fs = require('fs');
const path = require('path');
const IO = require('../../src/indsin_object.js');
const HL = require('../../src/indsin_highlight.js');

const frameFile = process.argv[2] || path.join(__dirname, '..', '..', 'tests', 'fixtures', 'showcase.frame.json');
const outDir = path.resolve(process.argv[3] || '.');
fs.mkdirSync(outDir, { recursive: true });

const frame = JSON.parse(fs.readFileSync(frameFile, 'utf8'));
const check = IO.validate(frame);
if (!check.ok) console.warn('validation issues:', check.errors);

// 1) Live, offline page (interpreter included)
const page = IO.toDocument(frame, { theme: 'dark', title: 'indsin preview' });
fs.writeFileSync(path.join(outDir, 'preview.html'), page);

// 2) Same tree in the light theme without the interpreter script (static snapshot)
fs.writeFileSync(path.join(outDir, 'snapshot.html'), IO.toDocument(frame, { theme: 'light', script: false }));

// 3) Highlighted indsin source
const src = '@view.Button=Go\n    label="Go";\n    variant="gradient";\n    onTap=count = count + 1;\n.end/view\n';
fs.writeFileSync(path.join(outDir, 'source.html'), '<!doctype html><meta charset="utf-8"><style>' + HL.stylesheet() + '</style>' + HL.highlight(src, { lineNumbers: true }));

console.log('wrote preview.html, snapshot.html, source.html →', outDir);
