// check.js -- compare m5tab5/core against the web version's own rules.
//
// Loads the game logic straight out of ../../web/index.html (everything from
// `const HAND` to the first render marker, the same slice the sims in
// ../../sim use) and checks the C build, ./umtest, against it two ways:
//
//   classify   tens of thousands of tile groups: same set, same points
//   play       random games, move by move: same table, melds, wall,
//              discard pile, points, penalty, hint and warnings
//   deal       the walls dealt from 400 seeds, C and web, tile for tile
//   code       the Tab5's replay codes: the web version reads them, replays
//              them to the same end, and packs the same payload
//   guide      main/guide.c against the web version's GUIDE: sections,
//              names, notes, tiles, and the set and points each makes
//   ach        core/umach.c against the web version's achievements: the
//              same script of games through both, every record compared
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
const CODES = +process.env.CODES || 300;
const html = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'index.html'), 'utf8');
const js = html.split('<script>')[1].split('</script>')[0];
const core = js.slice(js.indexOf('const HAND'), js.indexOf('/* ---------- render'));
const make = new Function(`function render(){}; ${core};
  return { deal, setS: v => { S = v; }, get S() { return S; }, sel, clearSet, meldSet, addSix, discardSix, discardSet, sortHand,
           findSet, penalty, classify, setValue, hasBonus, remaining, meldKeys, limitGroups, LIMIT, readCode, packGame };`);

// The web version's achievements: its "firsts" section, from the render
// half, run on the rules above with storage in a Map and the celebration
// written down rather than drawn.
function makeAch() {
  const a = js.indexOf('/* ---------- firsts'), b = js.indexOf('/* ---------- limit hand celebration');
  const g0 = js.indexOf('const R = (k, n)'), g1 = js.indexOf('function buildGuide');
  const f0 = js.indexOf('// every kind of set there is a first time for'), f1 = js.indexOf('buildGuide();', f0);
  return new Function(`
    const localStorage = (() => { const m = new Map(); return { getItem: k => m.has(k) ? m.get(k) : null, setItem: (k, v) => m.set(k, String(v)), removeItem: k => m.delete(k) }; })();
    const L = en => en, pick = en => en, celebs = [];
    function render() {} function buildGuide() {} function celebrate(t, kinds, v, award) { celebs.push(award); }
    const queueMicrotask = f => f();
    ${core}
    ${js.slice(g0, g1)}
    ${js.slice(f0, f1)}
    ${js.slice(a, b)}
    return { setS: v => { S = v; }, get S() { return S; }, nextGame, setAward, winAwards, secretAward, holdsPair, isPoker, celebs, FIRSTS, WINS, SECRETS, FLOOD_NAME,
             get firsts() { return firsts; }, get fulls() { return fulls; }, get wins() { return wins; }, get secrets() { return secrets; }, get flood() { return flood; }, gamesPlayed };`)();
}
const ACH = makeAch();
// what a game's clears included, as core's g->had: bit 0 poker, bit 1 a pair,
// bits 2-5 two pair, flush (a royal flush too), straight, full house; bit 7 tracked
const HAD_POKER = { 'two pair': 4, 'flush': 8, 'royal flush': 8, 'straight': 16, 'full house': 32 };
const hadOf = log => log.filter(e => e.t !== 'add' && e.t !== 'discard')
  .reduce((h, e) => h | (ACH.isPoker(e.t) ? 1 : 0) | (ACH.holdsPair(e) ? 2 : 0) | (HAD_POKER[e.t] || 0), 128);

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
    const want = `${t || ''}|${t ? G.setValue(t, ks) : 0}|${t ? (G.hasBonus(t, ks) ? 1 : 0) : 0}|${t && ACH.holdsPair({ t, k: ks }) ? 1 : 0}` +
      (t && G.LIMIT.has(t) ? '|' + G.limitGroups(t, ks).map(g => g.join(',')).join('/') : '');
    if (out[i] !== want) fail(`classify [${ks}] C=${out[i]} JS=${want}`);
  });
  console.log(`classify: ${cases.length} groups compared, ${limits} of them limit hands, with their grouping, and whether each holds a pair`);
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

/* ---------- replay codes ---------- */
// The Tab5's codes (core/umreplay.c) against the web version's reader:
// each must read, replay to exactly where the C game ended, and the web
// version packing the same moves must make the same payload, byte for byte.
function codeCheck() {
  const G = make();
  const lines = execFileSync(UMTEST, ['code', '5', String(CODES)], { maxBuffer: 1 << 28 }).toString().trim().split('\n');
  let n = 0, chars = 0;
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].startsWith('R ')) { if (lines[i].startsWith('X ')) fail(`code: C made no code for game ${lines[i].slice(2)}`); continue; }
    const [, payload, code, seed] = lines[i].split(' '), end = lines[i + 1];
    const r = G.readCode(code, 8);
    if (r.error) { fail(`code ${n}: the web version read it as ${r.error} ${r.at || ''}: ${code.slice(0, 40)}...`); continue; }
    const S = JSON.parse(r.snaps[r.snaps.length - 1]);
    const hand = S.hand.filter(k => k !== null).sort((a, b) => a - b);
    const js = `E ${S.points} ${S.score} ${S.done ? 1 : 0} |${hand.map(k => ' ' + k).join('')} |${S.melds.map(m => ' ' + m.join(',')).join('')} |${S.wall.map(k => ' ' + k).join('')} |${S.discards.map(k => ' ' + k).join('')}`;
    if (js !== end) fail(`code ${n}: replayed to a different end\n   C: ${end}\n  JS: ${js}`);
    const p = G.packGame(+seed, r.moves);
    if (p.head + p.rest !== payload) fail(`code ${n}: payloads differ\n   C: ${payload.slice(0, 60)}...\n  JS: ${(p.head + p.rest).slice(0, 60)}...`);
    n++; chars += code.length;
  }
  console.log(`code: ${n} Tab5 codes read and replayed by the web version, same payloads (${Math.round(chars / n)} chars on average)`);
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
    ` | ${S.points} ${S.score} ${G.penalty()} ${S.done ? 1 : 0} | ${mask(G.findSet(true))} ${mask(G.findSet(false))} | ${lone} | ${hadOf(S.log)}`;
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

/* ---------- achievements ---------- */
// A script of games: each deals, clears some sets and may end in a win.
// Games lean on a few sets at a time, so Full comes up, some clear no
// pairs or no poker hands, a few are saved before games were counted, and
// the last ones clear every set so the river floods.
function achCheck() {
  const A = ACH, C = [];   // C: the guide's examples, one or more for each set
  const g0 = js.indexOf('const R = (k, n)'), g1 = js.indexOf('function buildGuide');
  const GUIDE = new Function(js.slice(g0, g1) + '; return GUIDE;')();
  const G = make();
  for (const [, items] of GUIDE) for (const [, groups] of items) C.push(groups.flat());
  for (const n of [4, 6, 7, 8]) C.push([...Array(n).keys()].map(i => 9 + i));
  const sets = C.map(k => ({ t: G.classify(k), k })), byT = new Map(sets.map(e => [e.t, e]));
  if (A.FIRSTS.some(t => !byT.has(t)) || byT.size !== A.FIRSTS.length) fail(`ach: examples cover ${byT.size} sets, the web counts ${A.FIRSTS.length}`);
  let seed = 777;
  const rnd = n => { seed = (seed * 1103515245 + 12345) >>> 0; return (seed >>> 8) % n; };
  const script = [];
  const GAMES_N = 400;
  for (let gi = 0; gi < GAMES_N; gi++) {
    // now and then a seed played before, as a loaded deal: Full needs different deals
    const prev = script.filter(x => x.op === 'N' || x.op === 'U');
    const legacy = gi > 0 && rnd(25) === 0, again = prev.length && rnd(4) === 0;
    const s = again ? prev[prev.length - 1 - rnd(Math.min(4, prev.length))].seed : (rnd(1 << 24) * 256 + rnd(256)) >>> 0;
    script.push({ op: legacy ? 'U' : 'N', seed: s });
    const mode = rnd(5);   // 0 any, 1 no pairs, 2 no poker, 3 a few favorites, 4 poker hands
    const poker = sets.filter(e => A.isPoker(e.t));
    const pool = mode === 1 ? sets.filter(e => !A.holdsPair(e)) : mode === 2 ? sets.filter(e => !A.isPoker(e.t))
      : mode === 3 ? sets.slice(gi % 7, gi % 7 + 5) : mode === 4 ? poker : sets;
    const last = gi >= GAMES_N - 3;
    const clears = last ? sets.slice() : Array.from({ length: 1 + rnd(mode === 4 ? 14 : 10) }, () => pool[rnd(pool.length)]);
    for (const e of clears) {
      script.push({ op: 'C', e });
      if (rnd(60) === 0) script.push({ op: 'S' });   // a sort with tiles chosen
      if (rnd(90) === 0) script.push({ op: 'D' });   // Meld pressed on a melded pair and its third tile
    }
    if (rnd(2) === 0 || last) script.push({ op: 'W' });
  }
  const out = execFileSync(UMTEST, ['ach'], {
    input: script.map(x => x.op === 'C' ? `C ${x.e.t}|${x.e.k.join(',')}` : x.op === 'W' || x.op === 'S' || x.op === 'D' ? x.op : `${x.op} ${x.seed}`).join('\n') + '\n',
    maxBuffer: 1 << 28 }).toString().trim().split('\n');
  const norm = line => line.split(' | ').map(p => p.split(';').sort().join(';')).join(' | ');
  const recs = (name, m) => [name, ...[...m].map(([t, r]) => `${t}=${r.g}/${r.s}`)].join(';');
  let flooded = 0, fulls = 0, won = 0, legacy = false, legacyWins = 0, found = 0;
  script.forEach((x, i) => {
    A.celebs.length = 0;
    let award = '-';
    if (x.op === 'N' || x.op === 'U') legacy = x.op === 'U';
    if (x.op === 'N' || x.op === 'U') A.setS({ seed: x.seed, game: x.op === 'N' ? A.nextGame() : undefined, log: [], hand: [], wall: [], discards: [], melds: [] });
    else if (x.op === 'C') {
      A.S.log.push({ t: x.e.t, k: x.e.k.slice().sort((a, b) => a - b), v: 0 });
      const a = A.setAward(x.e.t);
      award = !a ? '-' : a.kicker.startsWith('First') ? 'first' : a.kicker.startsWith('Full') ? 'full' : '?';
    } else if (x.op === 'S') A.secretAward('wait');
    else if (x.op === 'D') A.secretAward('design');
    else if (!legacy) A.winAwards();
    // a game saved before games were counted had nothing tracked on the
    // Tab5, so it earns no win there; the web version would read its log.
    // A Place for Everything asks only that the table is empty, so it is
    // earned either way.
    else { if (A.S.log.length) A.secretAward('place'); legacyWins++; }
    const flood = A.celebs.some(c => c && c.name === A.FLOOD_NAME) ? 1 : 0;
    const wins = A.celebs.reduce((m, c) => { const w = c && A.WINS.findIndex(([, name]) => name === c.name); return w >= 0 && c ? m | (1 << w) : m; }, 0);
    const want = `R ${award} ${flood} ${wins} ${A.gamesPlayed()} | ${recs('first', A.firsts)} | ${recs('full', A.fulls)} | ${recs('win', A.wins)} | flood${A.flood ? `;${A.flood.g}/${A.flood.s}` : ''} | ${recs('secret', A.secrets)}`;
    if (norm(out[i] || '') !== norm(want)) fail(`ach step ${i + 1} (${x.op}${x.e ? ' ' + x.e.t : ''})\n   C: ${out[i]}\n  JS: ${want}`);
    flooded |= flood; fulls += award === 'full'; won |= wins; found = A.secrets.size;
  });
  if (!flooded || !fulls || won !== (1 << A.WINS.length) - 1 || found !== A.SECRETS.length)
    fail(`ach: the script never reached everything (flood ${flooded}, fulls ${fulls}, wins ${won}, secrets ${found})`);
  if (!legacyWins) fail('ach: no game saved before games were counted ended in a win');
  console.log(`ach: ${script.length} steps over ${GAMES_N} games, ${fulls} sets full, every win, every secret and the flood, ${legacyWins} older games won; records the same`);
}

classifyCheck();
achCheck();
dealCheck();
playCheck();
codeCheck();
if (GUIDECHECK) guideCheck();
if (failures) { console.error(`${failures} mismatches`); process.exit(1); }
console.log('core matches the web version');
