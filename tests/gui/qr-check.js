// qr-check.js PAGE.html GOLDEN
// Extract qrGen() from the served page, run it for a FIXED payload, and compare
// the module matrix to a golden that is known to decode (tests/gui/qr.golden was
// generated from the corrected encoder and verified with a real QR decoder).
// This catches an encoder regression the old suite missed -- it only grepped for
// the string "function qrGen", never ran or decoded it. Prints MATCH / DIFF.
const fs = require('fs');
const page = fs.readFileSync(process.argv[2], 'utf8');
const s = page.indexOf('function qrGen(s){');
const e = page.indexOf('function qrDraw', s);
if (s < 0 || e < 0) { console.log('DIFF (qrGen not found in page)'); process.exit(1); }
const src  = page.slice(s, e);                       // function qrGen(s){ ... return m}
const body = src.slice(src.indexOf('{') + 1, src.lastIndexOf('}'));
const qrGen = new Function('s', body);
const payload = 'ais://sync?host=127.0.0.1%3A8766&token=' + 'a'.repeat(32);
const got    = qrGen(payload).map(r => r.join('')).join('\n').trim();
const golden = fs.readFileSync(process.argv[3], 'utf8').trim();
console.log(got === golden ? 'MATCH' : 'DIFF');
process.exit(got === golden ? 0 : 1);
