// يطبع SVG لمشهد .puo بمُصيِّر JS:  node tools/piano_render.js <piano.js> <scene.puo> [render|renderAnim]
const P = require(require('path').resolve(process.argv[2]));
const fs = require('fs');
const fn = process.argv[4] || 'render';
process.stdout.write(P[fn](JSON.parse(fs.readFileSync(process.argv[3], 'utf8'))) + '\n');
