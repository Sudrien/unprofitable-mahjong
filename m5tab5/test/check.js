// check.js -- compare m5tab5/core against the web version's own rules.
//
// Loads the game logic straight out of ../../index.html (everything from
// `const HAND` to the first render marker, the same slice the sims in
// ../../sim use) and checks the C build, ./umtest, against it two ways:
//
//   classify   tens of thousands of tile groups: same set, same points
//   play       random games, move by move: same table, melds, wall,
//              discard pile, points, penalty, hint and warnings
//   deal       the walls dealt from 400 seeds, C and web, tile for tile
//   guide      main/guide.c against the web version's GUIDE: sections,
//              names, notes, tiles, and the set and points each makes
//
// Usage: node check.js [umtest path] [games] [guidecheck path]
//
// SPDX-License-Identifier: MIT
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const UMTEST = path.resolve(process.argv[2] || path.join(__dirname, 'umtest'));
const GAMES = +process.argv[3] || 2000;
const GUIDECHECK = process.argv[4] ? path.resolve(process.argv[4]) : null;
const html = fs.readFileSync(path.join(__dirname, '..', '..', 'index.html'), 'utf8');
const js = html.split('<script>')[1].split('</script>')[0];
const core = js.slice(js.indexOf('const HAND'), js.indexOf('/* ---------- render'));
const make = new Function(`function render(){}; ${core};
  return { deal, setS: v => { S = v; }, get S() { return S; }, sel, clearSet, meldSet, addSix, discardSix, discardSet, sortHand,
           findSet, penalty, classify, setValue, hasBonus, remaining, meldKeys, limitGroups, LIMIT };`);

let failures = 0;
const fail = msg => { failures++; if (failures <= 20) console.error('MISMATCH ' + msg); };

/* ---------- classify ---------- */
function classifyCheck() {
  const G = make();
  let seed = 12345;
  const rnd = n => { seed = (seed * 1103515245 + 12345) >>> 0; return (seed >>> 8) % n; };
  const cases = [];
  // whole sets the game knows, so every rule fires, then noise around them
  const pieces = [];
  for (let k = 0; k < 34; k++) { pieces.push([k, k], [k, k, k], [k, k, k, k]); if (k < 27 && k % 9 <= 6) pieces.push([k, k + 1, k + 2]); }
  for (let i = 0; i < 60000; i++) {
    let ks = [];
    const mode = rnd(4);
    if (mode === 0) { const n = 2 + rnd(17); for (let j = 0; j < n; j++) ks.push(rnd(34)); }
    else {
      const parts = 1 + rnd(5);
      for (let j = 0; j < parts; j++) ks.push(...pieces[rnd(pieces.length)]);
      if (mode === 2 && ks.length) ks[rnd(ks.length)] = rnd(34);
      if (mode === 3) { const s = rnd(3) * 9; ks = ks.map(k => k < 27 ? s + (k % 9) : k); }
    }
    const cnt = {}; let ok = ks.length >= 2 && ks.length <= 18;
    for (const k of ks) { cnt[k] = (cnt[k] || 0) + 1; if (cnt[k] > 4) ok = false; }
    if (ok) cases.push(ks);
  }
  // limit hands, which random tiles seldom make: a few thousand of each kind
  const pick = (arr, n) => { const a = arr.slice(), o = []; while (o.length < n) o.push(a.splice(rnd(a.length), 1)[0]); return o; };
  const ORPH = [0, 8, 9, 17, 18, 26, 27, 28, 29, 30, 31, 32, 33], TERM = [0, 8, 9, 17, 18, 26], GREENS = [19, 20, 21, 23, 25, 32];
  const sz = () => 3 + rnd(2);
  for (let i = 0; i < 600; i++) {
    cases.push(ORPH.concat(ORPH[rnd(13)]));
    const s9 = rnd(3) * 9; cases.push([0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 8, rnd(9)].map(r => s9 + r));
    cases.push([27, 28, 29, 30].flatMap(w => Array(sz()).fill(w)));
    const lw = pick([27, 28, 29, 30], 4); cases.push(lw.slice(0, 3).flatMap(w => Array(sz()).fill(w)).concat([lw[3], lw[3]]));
    cases.push([31, 32, 33].flatMap(d => Array(sz()).fill(d)));
    cases.push(pick([...Array(34).keys()], 7).flatMap(k => [k, k]));
    const hp = pick(ORPH.slice(6), 5); cases.push(hp.slice(0, 4).flatMap(k => Array(sz()).fill(k)).concat([hp[4], hp[4]]));
    const tp = pick(TERM, 5); cases.push(tp.slice(0, 4).flatMap(k => Array(sz()).fill(k)).concat([tp[4], tp[4]]));
    const gp = pick(GREENS, 5); cases.push(gp.slice(0, 4).flatMap(k => Array(3).fill(k)).concat([gp[4], gp[4]]));
    cases.push([19, 20, 21, 19, 20, 21].concat(Array(3).fill(GREENS[3 + rnd(3)]), [32, 32]));
    const kp = pick([...Array(34).keys()], 5); cases.push(kp.slice(0, 4).flatMap(k => Array(4).fill(k)).concat([kp[4], kp[4]]));
  }
  const limits = cases.filter(ks => G.LIMIT.has(G.classify(ks) || '')).length;
  const out = execFileSync(UMTEST, ['classify'], { input: cases.map(c => c.join(' ')).join('\n') + '\n', maxBuffer: 1 << 28 })
    .toString().trim().split('\n');
  cases.forEach((ks, i) => {
    const t = G.classify(ks);
    const want = `${t || ''}|${t ? G.setValue(t, ks) : 0}|${t ? (G.hasBonus(t, ks) ? 1 : 0) : 0}` +
      (t && G.LIMIT.has(t) ? '|' + G.limitGroups(t, ks).map(g => g.join(',')).join('/') : '');
    if (out[i] !== want) fail(`classify [${ks}] C=${out[i]} JS=${want}`);
  });
  console.log(`classify: ${cases.length} groups compared, ${limits} of them limit hands, with their grouping`);
}

/* ---------- deal ---------- */
// The same seed deals the same wall in both: seeds 0 (remapped) and 1..,
// and some from the top of the range.
function dealCheck() {
  const G = make();
  let n = 0;
  for (const first of [0, 4294967000]) {
    const out = execFileSync(UMTEST, ['deal', String(first), '200'], { maxBuffer: 1 << 26 }).toString().trim().split('\n');
    out.forEach((line, i) => {
      const seed = (first + i) >>> 0, js = G.deal(seed).join(' ');
      if (line !== js) fail(`deal ${seed}\n   C: ${line.slice(0, 60)}...\n  JS: ${js.slice(0, 60)}...`);
      n++;
    });
  }
  console.log(`deal: ${n} seeds dealt the same`);
}

/* ---------- guide ---------- */
function guideCheck() {
  const G = make();
  const a = js.indexOf('const R = (k, n)'), b = js.indexOf('function buildGuide');
  const GUIDE = new Function(js.slice(a, b) + '; return GUIDE;')();
  const out = execFileSync(GUIDECHECK).toString().trim().split('\n');
  const want = [];
  let first = 0;
  for (const [head, items] of GUIDE) { want.push(`H ${first} ${items.length} ${head[0]}`); first += items.length; }
  for (const [head, items] of GUIDE) for (const [name, groups, , note] of items) {
    const ks = groups.flat(), t = G.classify(ks);
    want.push(`I ${t || ''}|${t ? G.setValue(t, ks) : 0}|${groups.map(g => g.length).join(',')}|${ks.join(' ')}|${name[0]}|${note[0]}`);
  }
  if (out.length !== want.length) fail(`guide: C has ${out.length} lines, the web version ${want.length}`);
  for (let i = 0; i < Math.max(out.length, want.length); i++)
    if (out[i] !== want[i]) fail(`guide line ${i + 1}\n   C: ${out[i]}\n  JS: ${want[i]}`);
  console.log(`guide: ${first} sets in ${GUIDE.length} sections compared`);
}

/* ---------- play ---------- */
function jsState(G) {
  const S = G.S;
  const mask = keys => (keys || []).reduce((m, k) => m | (1 << +k.slice(1)), 0) >>> 0;
  const rem = G.remaining();
  let lone = 0;
  for (let k = 27; k < 34; k++) {
    const grp = k < 31 ? [27, 28, 29, 30] : [31, 32, 33];
    if (rem[k] === 1 && !grp.every(g => rem[g] > 0)) lone |= 1 << (k - 27);
  }
  return `S ${S.hand.length} ${S.hand.map(k => k === null ? -1 : k).join(' ')} | ${S.melds.length}` +
    (S.melds.length ? ' ' + S.melds.map(m => m.join(',')).join(' ') : '') +
    ` | ${S.wall.length} ${S.discards.length}${S.discards.length ? ' ' + S.discards.join(' ') : ''}` +
    ` | ${S.points} ${S.score} ${G.penalty()} ${S.done ? 1 : 0} | ${mask(G.findSet(true))} ${mask(G.findSet(false))} | ${lone}`;
}

function playCheck() {
  const lines = execFileSync(UMTEST, ['play', '7', String(GAMES)], { maxBuffer: 1 << 30 }).toString().trim().split('\n');
  let G = null, game = -1, moves = 0;
  for (let i = 0; i < lines.length; i++) {
    const l = lines[i];
    if (l.startsWith('G ')) { game = +l.slice(2); continue; }
    if (l.startsWith('W ')) {
      const wall = l.slice(2).split(' ').map(Number);
      G = make();
      G.setS({ wall: wall.slice(18), discards: [], hand: wall.slice(0, 18), melds: [], log: [], score: 0, points: 0, done: false });
      continue;
    }
    if (l.startsWith('A ')) {
      const [, op, hm, mm] = l.split(' ');
      G.sel.clear();
      for (let b = 0; b < 24; b++) if ((+hm >>> b) & 1) G.sel.add('h' + b);
      for (let b = 0; b < 4; b++) if ((+mm >>> b) & 1 && G.S.melds[b]) G.meldKeys(b).forEach(k => G.sel.add(k));
      ({ C: G.clearSet, M: G.meldSet, A: G.addSix, D: G.discardSix, X: G.discardSet, S: G.sortHand })[op]();
      moves++;
      continue;
    }
    if (l.startsWith('S ')) {
      const want = jsState(G);
      if (l !== want) fail(`game ${game}, line ${i + 1} after "${lines[i - 1]}"\n   C: ${l}\n  JS: ${want}`);
    }
  }
  console.log(`play: ${GAMES} games, ${moves} moves compared`);
}

classifyCheck();
dealCheck();
playCheck();
if (GUIDECHECK) guideCheck();
if (failures) { console.error(`${failures} mismatches`); process.exit(1); }
console.log('core matches the web version');
