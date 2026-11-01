#!/usr/bin/env node
'use strict';
/**
 * Generates the indsin language lexicon and TextMate grammar from the translator's own catalogue,
 * so highlighting can never drift from the elements/attributes/tokens the interpreter supports.
 *   node scripts/gen_syntax.js [repoRoot]
 * Writes: syntaxes/indsin-lexicon.json, syntaxes/indsin.tmLanguage.json,
 *         editors/vscode-indsin/syntaxes/indsin.tmLanguage.json
 */
const fs = require('fs');
const path = require('path');
const IO = require('../src/indsin_object.js');

const root = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', '..'));
const src = fs.readFileSync(path.join(__dirname, '..', 'src', 'indsin_object.js'), 'utf8');

const uniq = (a) => Array.from(new Set(a));
const attrSet = new Set();
for (const m of src.matchAll(/\ba\.(?:str|get|num|bool|int|has|first|list|json)\(\s*(\[[^\]]*\]|'[^']*')/g))
  for (const x of m[1].matchAll(/'([A-Za-z_][A-Za-z0-9_]*)'/g)) if (x[1].length > 1) attrSet.add(x[1]);
['kind', 'sourceTag', 'stagger', 'effect', 'theme', 'dir', 'lang', 'a11y_hint', 'onChange', 'onSelect', 'onSubmit', 'onClose'].forEach((x) => attrSet.add(x));
const attributes = Array.from(attrSet).sort();

const T = IO.tokens();
const kinds = IO.kinds();
const aliasNames = Object.keys(IO.aliases());
const events = attributes.filter((x) => /^on[A-Z]/.test(x));
const tokenValues = uniq([].concat(
  Object.keys(T.spacing), Object.keys(T.radius), Object.keys(T.typography), T.effects, Object.keys(T.easing),
  T.bannerTypes, Object.keys(T.button), Object.keys(T.elevationNames), Object.keys(T.duration),
  IO.themes().map((x) => x.toLowerCase()),
  ['primary', 'secondary', 'success', 'danger', 'warning', 'info', 'neutral', 'surface', 'background',
   'fill', 'outline', 'ghost', 'link', 'glass', 'gradient', 'filled', 'outlined', 'tonal', 'elevated',
   'start', 'center', 'end', 'stretch', 'left', 'right', 'top', 'bottom', 'rtl', 'ltr', 'auto',
   'cover', 'contain', 'bold', 'italic', 'underline']
)).filter((x) => /^[A-Za-z][A-Za-z0-9]*$/.test(x) && !/^\d+$/.test(x)).sort();

const lexicon = { version: IO.version, kinds, aliases: aliasNames.sort(), attributes, events, tokenValues,
  responsiveSuffixes: ['_sm', '_md', '_lg', '_xl'], blocks: { open: '@view.<Kind>=<name>', close: '.end/view', container: '@container=<Name>', containerClose: '.end/container' } };

const alt = (a) => a.slice().sort((x, y) => y.length - x.length).join('|');
const kindAlt = alt(uniq(kinds.concat(aliasNames.filter((x) => /^[A-Za-z]/.test(x)))));
const attrAlt = alt(attributes);
const tokAlt = alt(tokenValues);

const grammar = {
  $schema: 'https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json',
  name: 'indsin',
  scopeName: 'source.indsin',
  fileTypes: ['indsin'],
  patterns: [
    { include: '#comments' }, { include: '#import' }, { include: '#container-open' }, { include: '#container-close' },
    { include: '#view-open' }, { include: '#view-close' }, { include: '#attribute' }, { include: '#token-string' },
    { include: '#strings' }, { include: '#colors' }, { include: '#lengths' }, { include: '#numbers' },
    { include: '#response-binding' }, { include: 'source.rin' }
  ],
  repository: {
    comments: { patterns: [{ name: 'comment.line.double-slash.indsin', match: '//.*$' }, { name: 'comment.block.indsin', begin: '/\\*', end: '\\*/' }] },
    import: { match: '^\\s*(@import)\\s+("[^"\\n]*"|\'[^\'\\n]*\')\\s*;?', captures: { 1: { name: 'keyword.control.import.indsin' }, 2: { name: 'string.quoted.double.indsin' } } },
    'container-open': { match: '^\\s*(@container)\\s*=\\s*([A-Za-z_][\\w.-]*)', captures: { 1: { name: 'keyword.control.container.indsin' }, 2: { name: 'entity.name.type.container.indsin' } } },
    'container-close': { match: '^\\s*(\\.end/container)\\b', captures: { 1: { name: 'keyword.control.end.indsin' } } },
    'view-open': {
      patterns: [
        { match: '^\\s*(@view)(\\.)(' + kindAlt + ')(?![\\w.])(?:\\s*(=)\\s*([A-Za-z_][\\w.-]*))?',
          captures: { 1: { name: 'keyword.control.view.indsin' }, 2: { name: 'punctuation.accessor.indsin' }, 3: { name: 'support.class.element.indsin' }, 4: { name: 'keyword.operator.assignment.indsin' }, 5: { name: 'entity.name.tag.view-name.indsin' } } },
        { match: '^\\s*(@view)(\\.)([A-Za-z_][\\w.]*)(?:\\s*(=)\\s*([A-Za-z_][\\w.-]*))?',
          captures: { 1: { name: 'keyword.control.view.indsin' }, 2: { name: 'punctuation.accessor.indsin' }, 3: { name: 'entity.name.type.custom-element.indsin' }, 4: { name: 'keyword.operator.assignment.indsin' }, 5: { name: 'entity.name.tag.view-name.indsin' } } }
      ]
    },
    'view-close': { match: '^\\s*(\\.end/view)\\b', captures: { 1: { name: 'keyword.control.end.indsin' } } },
    attribute: {
      patterns: [
        { match: '^\\s*(' + attrAlt + ')(_(?:sm|md|lg|xl))?(?=\\s*=(?!=))', captures: { 1: { name: 'variable.parameter.attribute.indsin' }, 2: { name: 'storage.modifier.responsive.indsin' } } },
        { match: '^\\s*([A-Za-z_][\\w]*?)(_(?:sm|md|lg|xl))?(?=\\s*=(?!=)\\s*[^=])', captures: { 1: { name: 'variable.other.property.indsin' }, 2: { name: 'storage.modifier.responsive.indsin' } } }
      ]
    },
    'token-string': { match: '(?<=")(' + tokAlt + ')(?=")', name: 'constant.language.token.indsin' },
    strings: { patterns: [{ include: 'source.rin#template-strings' }, { include: 'source.rin#strings' }] },
    colors: { patterns: [
      { name: 'constant.other.color.rgb-value.indsin', match: '(?<=["=\\s(,])#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6})(?![\\w])' },
      { name: 'constant.other.color.rgb-value.indsin', match: '(?<=")#(?:[0-9a-fA-F]{4}|[0-9a-fA-F]{3})(?=")' }
    ] },
    lengths: { name: 'constant.numeric.length.indsin', match: '(?<![\\w.])-?\\d+(?:\\.\\d+)?(?:px|dp|sp|pt|ms|s|%|vw|vh)(?![\\w])' },
    numbers: { patterns: [{ include: 'source.rin#numbers' }] },
    'response-binding': { name: 'variable.other.binding.indsin', match: '\\{\\{\\s*[A-Za-z_][\\w.\\[\\]]*\\s*\\}\\}' }
  }
};
// not every Rin repository key is guaranteed; fall back gracefully when a name is absent
const out = JSON.stringify(grammar, null, 2) + '\n';
const w = (p, s) => { fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, s); console.log('wrote', path.relative(root, p)); };
w(path.join(root, 'syntaxes', 'indsin.tmLanguage.json'), out);
w(path.join(root, 'editors', 'vscode-indsin', 'syntaxes', 'indsin.tmLanguage.json'), out);
w(path.join(root, 'syntaxes', 'indsin-lexicon.json'), JSON.stringify(lexicon, null, 2) + '\n');

// keep the standalone JS highlighter's lexicon in sync (between the LEX markers)
const hp = path.join(__dirname, '..', 'src', 'indsin_highlight.js');
let h = fs.readFileSync(hp, 'utf8');
const block = '/*LEX:BEGIN*/\n  var LEX = ' + JSON.stringify({ version: lexicon.version, kinds: lexicon.kinds, aliases: lexicon.aliases, attributes: lexicon.attributes, tokenValues: lexicon.tokenValues }) + ';\n  /*LEX:END*/';
h = h.replace(/\/\*LEX:BEGIN\*\/[\s\S]*?\/\*LEX:END\*\//, () => block);
fs.writeFileSync(hp, h);
console.log('updated src/indsin_highlight.js lexicon');
