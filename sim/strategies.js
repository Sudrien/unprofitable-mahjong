// Strategy bots. Usage: node sim/strategies.js web/index.html [games] [min set value] [melds|nomelds] [rows|summary] [mode]
// SPDX-License-Identifier: MIT
// Modes: big, noadd, hybrid<wall>, carefulmelds, stash<n>, wind, honor
// "None left" bot: only clears sets that keep every remaining tile (shown + unseen) groupable into sets.
const fs = require('fs');
const html = fs.readFileSync(process.argv[2], 'utf8');
const js = html.split('<script>')[1].split('</script>')[0];
const core = js.slice(js.indexOf('const HAND'), js.indexOf('/* ---------- render'));
const make = new Function(`function render(){}; ${core};
  return { newGame, get S(){ return S; }, clearSet, addSix, discardSix, canAdd, pool, inHand, visible, sel, classify, setValue, get, penalty, remaining, meldSet, meldKeys };`);

// Can a 34-count multiset be split entirely into pairs, chows, pungs, kongs, wind sets and dragon sets?
const memo = new Map();
function suitOK(c) { // c: 9 counts
  const key = c.join('');
  if (memo.has(key)) return memo.get(key);
  const r = c.findIndex(x => x > 0);
  let ok = false;
  if (r < 0) ok = true;
  else {
    const tryTake = (idx, n) => { if (idx.some(i => c[i] < n)) return false; idx.forEach(i => c[i] -= n); const res = suitOK(c); idx.forEach(i => c[i] += n); return res; };
    ok = (c[r] >= 2 && tryTake([r], 2)) || (c[r] >= 3 && tryTake([r], 3)) || (c[r] === 4 && tryTake([r], 4)) ||
      (r <= 6 && tryTake([r, r + 1, r + 2], 1));
  }
  memo.set(key, ok);
  return ok;
}
const hmemo = new Map();
function honorOK(c) { // c: 7 counts, E S W N R G Wh
  const key = 'h' + c.join('');
  if (hmemo.has(key)) return hmemo.get(key);
  const r = c.findIndex(x => x > 0);
  let ok = false;
  if (r < 0) ok = true;
  else {
    const tryTake = (idx) => { if (idx.some(i => c[i] < 1)) return false; idx.forEach(i => c[i]--); const res = honorOK(c); idx.forEach(i => c[i]++); return res; };
    const same = n => { if (c[r] < n) return false; c[r] -= n; const res = honorOK(c); c[r] += n; return res; };
    ok = same(2) || same(3) || same(4) || (r < 4 ? tryTake([0, 1, 2, 3]) : tryTake([4, 5, 6]));
  }
  hmemo.set(key, ok);
  return ok;
}
const groupable = c => [0, 9, 18].every(b => suitOK(c.slice(b, b + 9))) && honorOK(c.slice(27, 34));

// All distinct sets (as kind lists) that the visible tiles can make.
function candidates(vc, classify) {
  const out = new Map();
  const add = ks => { const k = [...ks].sort((a, b) => a - b); const t = classify(k); if (t) out.set(k.join(','), k); };
  const has = (k, n = 1) => vc[k] >= n;
  for (let k = 0; k < 34; k++) for (let n = 2; n <= 4; n++) if (has(k, n)) add(Array(n).fill(k));
  for (const b of [0, 9, 18]) for (let r = 0; r < 9; r++) for (let len = 3; r + len <= 9; len++) {
    const ks = Array.from({ length: len }, (_, i) => b + r + i); if (!ks.every(k => has(k))) break; add(ks);
  }
  for (let a = 0; a < 34; a++) for (let b = 0; b < 34; b++) if (a !== b) {
    if (has(a, 3) && has(b, 2)) add([a, a, a, b, b]);
    if (a < b && has(a, 2) && has(b, 2)) add([a, a, b, b]);
  }
  for (const b of [0, 9, 18]) { // flushes
    const pick = (r, left, acc) => {
      if (!left) return add(acc);
      if (r > 8) return;
      for (let n = Math.min(left, vc[b + r]); n >= 0; n--) pick(r + 1, left - n, acc.concat(Array(n).fill(b + r)));
    };
    pick(0, 5, []);
  }
  for (let r = 0; r <= 4; r++) { // straights
    const rec = (d, acc) => { if (d === 5) return add(acc); for (const b of [0, 9, 18]) if (has(b + r + d)) rec(d + 1, acc.concat(b + r + d)); };
    rec(0, []);
  }
  for (let r = 0; r <= 6; r++) { // double / triple chows
    const run = s => [s + r, s + r + 1, s + r + 2];
    for (const s of [0, 9, 18]) {
      if (run(s).every(k => has(k, 2))) add(run(s).concat(run(s)));
      if (run(s).every(k => has(k, 3))) add(run(s).concat(run(s), run(s)));
      for (const s2 of [0, 9, 18]) if (s2 > s && run(s).concat(run(s2)).every(k => has(k))) add(run(s).concat(run(s2)));
    }
    if ([0, 9, 18].every(s => run(s).every(k => has(k)))) add([0, 9, 18].flatMap(run));
  }
  if ([27, 28, 29, 30].every(k => has(k))) add([27, 28, 29, 30]);
  if ([31, 32, 33].every(k => has(k))) add([31, 32, 33]);
  if ([31, 32, 33].every(k => has(k, 3))) add([31, 32, 33].flatMap(k => Array(Math.min(vc[k], 4)).fill(k)));
  const w3 = [27, 28, 29, 30].filter(k => has(k, 3));
  if (w3.length === 4) add([27, 28, 29, 30].flatMap(k => Array(Math.min(vc[k], 4)).fill(k)));
  if (w3.length >= 3) for (const p of [27, 28, 29, 30]) if (has(p, 2)) { const t = [27, 28, 29, 30].filter(k => k !== p).slice(0, 3); if (t.every(k => has(k, 3))) add(t.flatMap(k => Array(Math.min(vc[k], 4)).fill(k)).concat(p, p)); }
  // newer classic hands the bot can spot: little three dragons, triple pung, mixed straight, three kongs
  for (const p of [31, 32, 33]) { const o = [31, 32, 33].filter(x => x !== p); if (has(p, 2) && o.every(x => has(x, 3))) add(o.flatMap(x => Array(Math.min(vc[x], 4)).fill(x)).concat(p, p)); }
  for (let r = 0; r < 9; r++) if ([0, 9, 18].every(b => has(b + r, 3))) add([0, 9, 18].flatMap(b => Array(Math.min(vc[b + r], 4)).fill(b + r)));
  for (const [x, y, z] of [[0,9,18],[0,18,9],[9,0,18],[9,18,0],[18,0,9],[18,9,0]]) { const ks = [x, x + 1, x + 2, y + 3, y + 4, y + 5, z + 6, z + 7, z + 8]; if (ks.every(k => has(k))) add(ks); }
  { const ko = []; for (let k = 0; k < 34; k++) if (has(k, 4)) ko.push(k); for (let i = 0; i < ko.length; i++) for (let j = i + 1; j < ko.length; j++) for (let l = j + 1; l < ko.length; l++) add([ko[i], ko[j], ko[l]].flatMap(k => [k, k, k, k])); }
  const O = [0, 8, 9, 17, 18, 26, 27, 28, 29, 30, 31, 32, 33];
  if (O.every(k => has(k))) for (const d of O) if (has(d, 2)) add(O.concat(d));
  return [...out.values()];
}


const N = +process.argv[3] || 5, T = +process.argv[4] || 8, USE_MELDS = process.argv[5] !== 'nomelds';
const MODE = process.argv[7] || 'big';
// Which set to meld: honor pungs/kongs, honor pairs matching a melded group, or a run of 4+.
function meldPick(vc, M, skip = new Set()) {
  const hasGroup = k => M.some(m => m[0] >= 27 && ((m[0] < 31) === (k < 31)));
  let pick = null;
  for (const k of [31,32,33,27,28,29,30]) if (!pick && !skip.has(k) && vc[k] >= 3) pick = Array(Math.min(vc[k], 4)).fill(k);
  for (let k = 27; k < 34 && !pick; k++) if (!skip.has(k) && vc[k] === 2 && hasGroup(k)) pick = [k, k];
  if (!pick) for (const b of [0, 9, 18]) {
    let cur = [];
    for (let r = 0; r < 9 && !pick; r++) { cur = vc[b + r] ? cur.concat(b + r) : []; if (cur.length >= 4 && (r === 8 || !vc[b + r + 1])) pick = cur.slice(); }
    if (pick) break;
  }
  return pick;
}
const LIMITS = ['thirteen orphans','nine gates','big four winds','little four winds','all honors','all terminals','big three dragons','seven pairs'];
const BIG = LIMITS.concat(['full straight','pure triple chow','mixed triple chow','little three dragons','triple pung','mixed straight','three kongs','all green','four kongs']);
const rows = [], bigTally = {};
const isEdge = k => k >= 27 || k % 9 === 0 || k % 9 === 8;
for (let g = 1; g <= N; g++) {
  const G = make();
  G.newGame();
  let adds = 0, discards = 0, stashes = 0, melds = 0, turns = 0, dry = 0, end = '';
  const sets = {};
  for (;;) {
    if (++turns > 3000) { end = 'turn limit'; break; }
    const vis = G.visible(), M = G.S.melds;
    if (!vis.length && !G.pool() && !M.length) break;
    const vc = new Array(34).fill(0); vis.forEach(([, k]) => vc[k]++);
    const total = G.remaining();
    // candidate sets: table only, plus every combination of melds with table tiles
    const cands = [];
    const subsets = USE_MELDS ? (1 << M.length) : 1;
    for (let mask = 0; mask < subsets; mask++) {
      const mIdx = M.map((_, i) => i).filter(i => mask & (1 << i));
      const mk = mIdx.flatMap(i => M[i]);
      const comb = vc.slice(); mk.forEach(k => comb[k]++);
      for (const ks of candidates(comb, G.classify)) {
        const rest = ks.slice(), mc = {};
        let ok = true;
        for (const k of mk) { const p = rest.indexOf(k); if (p < 0) { ok = false; break; } rest.splice(p, 1); }
        if (!ok || ks.length > 14) continue;
        const t = G.classify(ks), c = total.slice(); ks.forEach(k => c[k]--);
        cands.push({ ks, rest, mIdx, t, v: G.setValue(t, ks), safe: groupable(c) });
      }
      // a meld on its own is always clearable
      if (mIdx.length === 1) { const ks = M[mIdx[0]]; const t = G.classify(ks); const c = total.slice(); ks.forEach(k => c[k]--); cands.push({ ks, rest: [], mIdx, t, v: G.setValue(t, ks), safe: groupable(c) }); }
    }
    const take = cd => {
      G.sel.clear();
      cd.mIdx.forEach(i => G.meldKeys(i).forEach(k => G.sel.add(k)));
      const used = new Set();
      for (const k of cd.rest) { const hit = vis.find(([key, kk]) => kk === k && !used.has(key)); used.add(hit[0]); G.sel.add(hit[0]); }
      G.clearSet(); sets[cd.t] = (sets[cd.t] || 0) + 1; dry = 0;
      if (BIG.includes(cd.t)) bigTally[cd.t] = (bigTally[cd.t] || 0) + 1;
    };
    const byBest = (a, b) => (b.safe - a.safe) || (b.v - a.v) || (b.ks.length - a.ks.length);
    if (!G.pool()) { const c = cands.sort(byBest)[0]; if (!c) { end = 'stranded'; break; } take(c); continue; }
    // bank: keep winds (and dragons) out of ordinary sets and park them in the discard pile 6 at a time
    const BANKING = MODE === 'wind' || MODE === 'honor';
    const bank = new Set(MODE === 'honor' ? [27, 28, 29, 30, 31, 32, 33] : [27, 28, 29, 30]);
    const BANK_HANDS = new Set(['big four winds', 'little four winds', 'all honors', 'big three dragons', 'little three dragons', 'thirteen orphans']);
    if (BANKING) {
      const outThere = G.S.wall.concat(G.S.discards).some(k => bank.has(k));
      if (outThere) {
        for (let i = cands.length - 1; i >= 0; i--) if (cands[i].ks.some(k => bank.has(k)) && !BANK_HANDS.has(cands[i].t)) cands.splice(i, 1);
      }
      const onTable = vis.filter(([, k]) => bank.has(k));
      if (onTable.length >= 6 && G.S.wall.length > 0) {
        G.sel.clear(); onTable.slice(0, 6).forEach(([key]) => G.sel.add(key)); G.discardSix(); discards++; dry = 0;
        continue;
      }
    }
    const meldSum = c => c.mIdx.reduce((a, i) => a + G.setValue(G.classify(M[i]), M[i]), 0);
    const doMeld = pick => {
      const used = new Set(); G.sel.clear();
      for (const k of pick) { const hit = vis.find(([key, kk]) => kk === k && !used.has(key)); used.add(hit[0]); G.sel.add(hit[0]); }
      G.meldSet(); melds++; dry = 0;
    };
    const upgrade = () => {
      const i = M.findIndex(m => m.length === 3 && m[0] === m[2] && vc[m[0]] > 0);
      if (i < 0) return false;
      G.sel.clear(); G.meldKeys(i).forEach(k => G.sel.add(k)); G.sel.add(vis.find(([, kk]) => kk === M[i][0])[0]); G.meldSet(); dry = 0; return true;
    };
    const careful = MODE === 'carefulmelds' || (MODE.startsWith('hybrid') && G.S.wall.length < +MODE.slice(6));
    if (careful) {
      // if the remaining tiles can no longer all be grouped, 'safe' means nothing: treat every set as fair game
      const safe = (groupable(total) ? cands.filter(c => c.safe) : cands.slice()).sort(byBest);
      if (MODE === 'carefulmelds') {
        if (upgrade()) continue;
        const combo = safe.find(c => c.mIdx.length && c.v > meldSum(c) + 4);
        if (combo) { take(combo); continue; }
        if (safe.length && safe[0].v >= T) { take(safe[0]); continue; }
        const pick = M.length < 4 ? meldPick(vc, M) : null;
        if (pick) { doMeld(pick); continue; }
      }
      if (safe.length) { take(safe[0]); continue; }
      if (G.canAdd()) { G.addSix(); adds++; dry++; continue; }
      if (G.inHand() >= 6) {
        const seen = total.map((t, k) => t - vc[k]);
        const pick = vis.slice().sort((a, b) => seen[b[1]] - seen[a[1]]).slice(0, 6);
        G.sel.clear(); pick.forEach(([key]) => G.sel.add(key)); G.discardSix(); discards++; dry++;
        if (dry > 40) { end = 'looping'; break; }
        continue;
      }
      const any = cands.sort(byBest)[0]; if (any) { take(any); continue; }
      end = 'stuck'; break;
    }
    // 1) a big hand that uses melds, or anything worth 24+
    const huge = cands.filter(c => (c.mIdx.length && c.v > c.mIdx.reduce((a, i) => a + G.setValue(G.classify(M[i]), M[i]), 0) + 4) || c.v >= 24).sort(byBest);
    if (huge.length) { take(huge[0]); continue; }
    // grow a melded pung into a kong when its fourth tile shows up
    if (USE_MELDS) {
      const i = M.findIndex(m => m.length === 3 && m[0] === m[2] && vc[m[0]] > 0);
      if (i >= 0) { G.sel.clear(); G.meldKeys(i).forEach(k => G.sel.add(k)); G.sel.add(vis.find(([, kk]) => kk === M[i][0])[0]); G.meldSet(); dry = 0; continue; }
    }
    // meld honor / terminal pungs and kongs, and honor pairs that match a melded group
    if (USE_MELDS && M.length < 4) {
      const hasGroup = k => M.some(m => m[0] >= 27 && ((m[0] < 31) === (k < 31)));
      let pick = null;
      for (const k of [31,32,33,27,28,29,30]) if (!pick && !(BANKING && bank.has(k)) && vc[k] >= 3) pick = Array(Math.min(vc[k], 4)).fill(k);
      for (let k = 27; k < 34 && !pick; k++) if (!(BANKING && bank.has(k)) && vc[k] === 2 && hasGroup(k)) pick = [k, k];
      if (!pick) for (const b of [0, 9, 18]) {
        let cur = [];
        for (let r = 0; r < 9 && !pick; r++) { cur = vc[b + r] ? cur.concat(b + r) : []; if (cur.length >= 4 && (r === 8 || !vc[b + r + 1])) pick = cur.slice(); }
        if (pick) break;
      }
      if (pick) {
        const used = new Set(); G.sel.clear();
        for (const k of pick) { const hit = vis.find(([key, kk]) => kk === k && !used.has(key)); used.add(hit[0]); G.sel.add(hit[0]); }
        G.meldSet(); melds++; dry = 0; continue;
      }
    }
    // stash: with all meld slots full, park a would-be meld (plus filler) in the discard pile.
    // Discards come back in order once the wall runs out, so the group returns together.
    if (MODE.startsWith('stash') && M.length >= 4 && stashes < +(MODE.slice(5) || 3) && G.S.wall.length >= 12 && G.inHand() >= 6) {
      const pick = meldPick(vc, M);
      if (pick) {
        const part = pick.slice(0, 6), used = new Set(), keys = [];
        for (const k of part) { const hit = vis.find(([key, kk]) => kk === k && !used.has(key)); used.add(hit[0]); keys.push(hit[0]); }
        // filler: tiles with the fewest neighbours on the table
        const near = k => k >= 27 ? vc[k] : [-2, -1, 0, 1, 2].reduce((a, d) => { const j = k + d; return a + (Math.floor(j / 9) === Math.floor(k / 9) && j >= 0 ? vc[j] : 0); }, 0);
        const rest = vis.filter(([key]) => !used.has(key)).sort((a, b) => near(a[1]) - near(b[1]));
        while (keys.length < 6) keys.push(rest.shift()[0]);
        G.sel.clear(); keys.forEach(k => G.sel.add(k)); G.discardSix(); discards++; stashes++; dry = 0;
        continue;
      }
    }
    // 3) otherwise clear sets worth T or more from the table
    const big = cands.filter(c => !c.mIdx.length && c.v >= T).sort(byBest);
    if (big.length) { take(big[0]); continue; }
    if (MODE !== 'noadd' && !MODE.startsWith('stash') && !BANKING && G.canAdd()) { G.addSix(); adds++; dry++; continue; }
    const small = cands.filter(c => !c.mIdx.length).sort((a, b) => (b.safe - a.safe) || (a.v - b.v) || (a.ks.length - b.ks.length))[0];
    if (small) { take(small); continue; }
    if (G.inHand() >= 6) {
      const seen = total.map((t, k) => t - vc[k]);
      const pick = vis.slice().sort((a, b) => seen[a[1]] - seen[b[1]]).slice(0, 6);
      G.sel.clear(); pick.forEach(([key]) => G.sel.add(key)); G.discardSix(); discards++; dry++;
    } else { end = 'stuck'; break; }
    if (dry > 40) { end = 'looping'; break; }
  }
  const bigs = Object.entries(sets).filter(([k]) => BIG.includes(k)).map(([k, v]) => k + (v > 1 ? ' x' + v : '')).join(', ');
  rows.push([g, G.S.points, G.penalty(), G.inHand() + G.pool() + G.S.melds.flat().length, adds, melds, bigs || '-', discards]);
}
if (process.argv[6] === 'rows') for (const r of rows) console.log(r.join(' | '));
const avg = i => (rows.reduce((a, r) => a + r[i], 0) / rows.length).toFixed(0);
console.log(`T=${T} melds=${USE_MELDS}  points ${avg(1)}  penalty ${avg(2)}  left ${avg(3)}  adds ${avg(4)}  discards ${avg(7)}  melds ${avg(5)}  big hands: ${JSON.stringify(bigTally)}`);
