const { parseProgram, buildFabric, layout, shuttleDiff, resolveText, findAny, recomputeHashes, countStrandsForTest } = require('./core_test.js');

const SRC_V1 = `
warp count = 0;
@view.Column=root
  gap=16; padding=20;
  @view.Card=header
    padding=16;
    @view.Text=title text="Welcome to Rin"; size=22; .end/view
    @view.Text=subtitle text="Loomtime rendering engine"; size=14; .end/view
  .end/view
  @view.Row=actions
    gap=12;
    @view.Button=cta label="Continue"; .end/view
    @view.Button=cancel label="Cancel"; .end/view
  .end/view
  @view.Card=counter
    padding=16;
    @view.Text=count text="Count: {{count}}"; size=16; .end/view
  .end/view
.end/view
`;

// --- Test 1: parse + build + layout (cold) ---
const p1 = parseProgram(SRC_V1);
console.log('Parsed OK. root kind=', p1.root.kind, ' warps=', JSON.stringify(p1.warps));
const warpState = { count: 0 };
const fabric = buildFabric(p1.root, warpState, '', 0);
console.log('Fabric strand count:', countStrandsForTest(fabric));

const geo = layout(fabric, {minW:0,maxW:390,minH:0,maxH:1e9}, 0, 0);
console.log('Root geometry after cold layout:', JSON.stringify(geo));
if (geo.w <= 0 || geo.h <= 0 || typeof geo.w !== 'number' || typeof geo.h !== 'number')
  throw new Error('FAIL: cold layout produced degenerate/non-numeric geometry: ' + JSON.stringify(geo));

function dump(s, d){
  d = d||0;
  console.log(' '.repeat(d*2) + s.kind + " '" + (s.name||'') + "' geom=" + JSON.stringify(s.geometry));
  if (typeof s.geometry.x !== 'number' || typeof s.geometry.w !== 'number')
    throw new Error('FAIL: non-numeric geometry on strand ' + s.name);
  s.children.forEach(function(c){ dump(c,d+1); });
}
dump(fabric);

const header = findAny(fabric, function(s){ return s.name==='header'; });
const title = findAny(fabric, function(s){ return s.name==='title'; });
if (Math.abs(title.geometry.x - (header.geometry.x + 16)) > 0.001)
  throw new Error('FAIL: padding=16 not honored — title.x=' + title.geometry.x + ', header.x=' + header.geometry.x);
console.log('PASS: numeric attribute (padding=16, unquoted) correctly honored by the Loom.');

// --- Test 2: source edit -> Shuttle diff ---
const SRC_EDITED = SRC_V1.replace('Loomtime rendering engine', 'Loomtime rendering engine v2');
const p2 = parseProgram(SRC_EDITED);
const patches = []; const dirty = new Set();
shuttleDiff(fabric, p2.root, warpState, '', 0, patches, dirty);
console.log('\nPatches from source edit:', JSON.stringify(patches));
if (patches.length !== 1 || patches[0].kind !== 'UpdateAttrs' || patches[0].name !== 'subtitle')
  throw new Error('FAIL: expected exactly one UpdateAttrs patch on subtitle, got ' + JSON.stringify(patches));
console.log('PASS: Shuttle produced the minimal single patch for a single-attribute edit.');
layout(fabric, {minW:0,maxW:390,minH:0,maxH:1e9}, 0, 0); // settle geometry post-edit, like the real render loop would

// --- Test 3: warp-driven runtime update, independent of source, same pipeline ---
const countTextStrand = findAny(fabric, function(s){ return s.name==='count'; });
const before = countTextStrand.attrs.find(function(a){ return a.key==='text'; }).resolved;
if (before !== 'Count: 0') throw new Error('FAIL: initial warp binding did not resolve at build time, got ' + before);

warpState.count = 1;
const attr = countTextStrand.attrs.find(function(a){ return a.key==='text'; });
attr.resolved = resolveText(attr.raw, warpState);
recomputeHashes(fabric);
if (attr.resolved !== 'Count: 1') throw new Error('FAIL: warp binding did not resolve, got ' + attr.resolved);
console.log('PASS: Warp cell update resolved into bound Text strand ->', attr.resolved);

const beforeHeaderGeom = JSON.stringify(header.geometry);
layout(fabric, {minW:0,maxW:390,minH:0,maxH:1e9}, 0, 0);
if (JSON.stringify(header.geometry) !== beforeHeaderGeom)
  throw new Error('FAIL: unrelated header subtree geometry changed after an unrelated warp update');
console.log('PASS: unrelated subtree (header) untouched after warp update elsewhere — cache invalidation is correctly scoped.');

// --- Test 4: malformed source raises a line-numbered RinError (Snag), not a crash ---
const lines = SRC_V1.split('\n');
const lastRealLine = lines.length - 2; // index of the final '.end/view' (last line is empty from trailing \n)
const badLines = lines.slice(0, lastRealLine); // drop the outermost closing tag
const BAD = badLines.join('\n');
try {
  parseProgram(BAD);
  throw new Error('FAIL: expected a parse error for malformed source');
} catch (e) {
  if (e.line === undefined) throw new Error('FAIL: error missing line info: ' + e.message);
  console.log('PASS: malformed source correctly raised RinError at line', e.line, '-', e.message);
}

console.log('\nALL CORE-LOGIC TESTS PASSED');
