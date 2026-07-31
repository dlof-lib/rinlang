# Re-extracts the pure-logic (non-DOM) portion of the JS engine from
# mirror_loom_live_preview.html into core_test.js for Node-based testing.
import re
html = open('../mirror_loom_live_preview.html').read()
js = re.search(r'<script>(.*)</script>', html, re.S).group(1)
core = js[:js.find('// Application state & wiring')]
core += """
module.exports = { parseProgram, buildFabric, layout, shuttleDiff, fnv1a, resolveText, findAny, recomputeHashes,
  countStrandsForTest: function(s){ let n=1; s.children.forEach(c=>n+=module.exports.countStrandsForTest(c)); return n; } };
"""
open('core_test.js','w').write(core)
print('wrote core_test.js')
