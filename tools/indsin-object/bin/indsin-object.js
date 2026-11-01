#!/usr/bin/env node
'use strict';
/**
 * indsin-object — command-line translator: Indsin Fabric JSON (or a .indsin/.rin file via the engine) → HTML.
 * Works on Linux, macOS and Windows with Node.js ≥ 14. No dependencies.
 */
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const IO = require('../src/indsin_object.js');

const HELP = `indsin-object ${IO.version} — Indsin Object translator & interpreter

Usage:
  indsin-object [options] <input>        input: Fabric JSON file, "-" for stdin,
                                         or a .indsin/.rin source (needs --engine)

Output:
  -o, --output <file>     Write to a file instead of stdout
  -f, --format <kind>     document (default) | fragment | json
      --no-script         Omit the embedded interpreter (static HTML only)

Rendering:
  -t, --theme <name>      dark (default) | light | auto | ${IO.themes().join(' | ')}
      --dir <dir>         auto (default) | ltr | rtl
  -w, --width <px>        Viewport width for responsive overrides (default 390)
      --lang <code>       Document language, e.g. en, ar
      --title <text>      Document title
      --allow-file-urls   Allow file:, content: and android-asset: URLs for media

Engine (source files):
      --engine <path>     Path to the rin_indsin_run binary (or set RIN_INDSIN_RUN)

Inspection:
      --validate          Validate the tree and print a JSON report (exit 1 on errors)
      --kinds             List every supported element kind
      --catalog           Element catalog as JSON (category, aliases, flags)
      --tokens            Design tokens as JSON
      --themes            List built-in themes
      --self-test         Run the built-in coverage self-test
  -v, --version           Print the version
  -h, --help              Show this help
`;

function parse(argv) {
  const o = { _: [], format: 'document', theme: 'dark', dir: 'auto', width: 390, script: true };
  const need = (i, n) => { if (i + 1 >= argv.length) fail(`option ${n} requires a value`); return argv[i + 1]; };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    switch (a) {
      case '-h': case '--help': o.help = true; break;
      case '-v': case '--version': o.version = true; break;
      case '-o': case '--output': o.output = need(i, a); i++; break;
      case '-f': case '--format': o.format = need(i, a); i++; break;
      case '-t': case '--theme': o.theme = need(i, a); i++; break;
      case '--dir': o.dir = need(i, a); i++; break;
      case '-w': case '--width': o.width = parseInt(need(i, a), 10) || 390; i++; break;
      case '--lang': o.lang = need(i, a); i++; break;
      case '--title': o.title = need(i, a); i++; break;
      case '--engine': o.engine = need(i, a); i++; break;
      case '--no-script': o.script = false; break;
      case '--allow-file-urls': o.allowFileUrls = true; break;
      case '--validate': o.validate = true; break;
      case '--kinds': o.kinds = true; break;
      case '--catalog': o.catalog = true; break;
      case '--tokens': o.tokens = true; break;
      case '--themes': o.themes = true; break;
      case '--self-test': o.selfTest = true; break;
      case '-': o._.push('-'); break;
      default:
        if (a.startsWith('-')) fail(`unknown option ${a}\n\n${HELP}`);
        o._.push(a);
    }
  }
  return o;
}
function fail(msg, code) { process.stderr.write(`indsin-object: ${msg}\n`); process.exit(code || 2); }
function readStdin() { try { return fs.readFileSync(0, 'utf8'); } catch (e) { fail('cannot read stdin'); } }

function loadInput(o) {
  const src = o._[0];
  if (!src) fail(`no input given\n\n${HELP}`);
  if (src === '-') return readStdin();
  if (!fs.existsSync(src)) fail(`cannot open ${src}`);
  if (/\.(indsin|rin)$/i.test(src)) {
    const engine = o.engine || process.env.RIN_INDSIN_RUN;
    if (!engine) fail('source files need the engine: pass --engine <rin_indsin_run> or set RIN_INDSIN_RUN');
    try {
      return execFileSync(engine, ['render', src, String(o.width)], { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
    } catch (e) { fail(`engine failed: ${(e.stderr || e.message || '').toString().trim()}`, 3); }
  }
  return fs.readFileSync(src, 'utf8');
}

function main() {
  const o = parse(process.argv.slice(2));
  if (o.help) return process.stdout.write(HELP);
  if (o.version) return process.stdout.write(IO.version + '\n');
  if (o.kinds) return process.stdout.write(IO.kinds().join('\n') + '\n');
  if (o.themes) return process.stdout.write(IO.themes().join('\n') + '\n');
  if (o.catalog) return process.stdout.write(JSON.stringify(IO.catalog(), null, 2) + '\n');
  if (o.tokens) return process.stdout.write(JSON.stringify(IO.tokens(), null, 2) + '\n');
  if (o.selfTest) {
    const r = IO.selfTest();
    process.stdout.write(JSON.stringify(r, null, 2) + '\n');
    process.exit(r.ok ? 0 : 1);
  }
  const input = loadInput(o);
  const options = { theme: o.theme, dir: o.dir, width: o.width, lang: o.lang, title: o.title, allowFileUrls: !!o.allowFileUrls, script: o.script };
  let out;
  try {
    if (o.validate) {
      const rep = IO.validate(input);
      process.stdout.write(JSON.stringify(rep, null, 2) + '\n');
      process.exit(rep.ok ? 0 : 1);
    }
    if (o.format === 'document') out = IO.toDocument(input, options);
    else if (o.format === 'fragment') out = IO.toHtml(input, options);
    else if (o.format === 'json') out = JSON.stringify(IO.translate(input, options), null, 2);
    else fail(`unknown format "${o.format}"`);
  } catch (e) {
    fail(e.message + (e.line ? ` (line ${e.line})` : ''), 1);
  }
  if (o.output) { fs.mkdirSync(path.dirname(path.resolve(o.output)), { recursive: true }); fs.writeFileSync(o.output, out); }
  else process.stdout.write(out);
}
main();
