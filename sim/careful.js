// Careful bot: only clears sets that leave every remaining tile groupable. Usage: node sim/careful.js web/index.html [games]
// SPDX-License-Identifier: MIT
const fs = require('fs');
const html = fs.readFileSync(process.argv[2], 'utf8');
const js = html.split('<script>')[1].split('</script>')[0];
const core = js.slice(js.indexOf('const HAND'), js.indexOf('/* ---------- render'));
const make = new Function(`function render(){}; ${core};
  return { newGame, get S(){ return S; }, clearSet, addSix, discardSix, canAdd, pool, inHand, visible, sel, classify, setValue, get, penalty, remaining };`);

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
  if ([31, 32, 33].every(k => has(k, 3))) add([31, 31, 31, 32, 32, 32, 33, 33, 33]);
  const w3 = [27, 28, 29, 30].filter(k => has(k, 3));
  if (w3.length === 4) add([27, 28, 29, 30].flatMap(k => [k, k, k]));
  if (w3.length >= 3) for (const p of [27, 28, 29, 30]) if (has(p, 2)) { const t = [27, 28, 29, 30].filter(k => k !== p).slice(0, 3); if (t.every(k => has(k, 3))) add(t.flatMap(k => [k, k, k]).concat(p, p)); }
  const O = [0, 8, 9, 17, 18, 26, 27, 28, 29, 30, 31, 32, 33];
  if (O.every(k => has(k))) for (const d of O) if (has(d, 2)) add(O.concat(d));
  return [...out.values()];
}

const N = +process.argv[3] || 5;
const rows = [];
for (let g = 1; g <= N; g++) {
  const G = make();
  G.newGame();
  let adds = 0, discards = 0, turns = 0, dry = 0, end = '';
  const sets = {};
  for (;;) {
    if (++turns > 3000) { end = 'turn limit'; break; }
    const vis = G.visible();
    if (!vis.length && !G.pool()) break;
    const vc = new Array(34).fill(0); vis.forEach(([, k]) => vc[k]++);
    const total = G.remaining();
    let best = null, bestV = -1;
    for (const ks of candidates(vc, G.classify)) {
      const c = total.slice(); ks.forEach(k => c[k]--);
      if (!groupable(c)) continue;
      const t = G.classify(ks), v = G.setValue(t, ks);
      if (v > bestV || (v === bestV && ks.length > best.length)) { best = ks; bestV = v; }
    }
    if (best) {
      const used = new Set(); G.sel.clear();
      for (const k of best) { const hit = vis.find(([key, kk]) => kk === k && !used.has(key)); used.add(hit[0]); G.sel.add(hit[0]); }
      const t = G.classify(best); G.clearSet(); sets[t] = (sets[t] || 0) + 1; dry = 0;
      continue;
    }
    if (G.canAdd()) { G.addSix(); adds++; dry++; }
    else if (G.pool() > 0 && G.inHand() >= 6) {
      // discard the 6 tiles with the most copies still unseen (most likely to pair up later)
      const seen = total.map((t, k) => t - vc[k]);
      const pick = vis.slice().sort((a, b) => seen[b[1]] - seen[a[1]]).slice(0, 6);
      G.sel.clear(); pick.forEach(([key]) => G.sel.add(key)); G.discardSix(); discards++; dry++;
    } else { end = 'no safe set and nothing to draw'; break; }
    if (dry > 40) { end = 'looping through discards'; break; }
  }
  const top = Object.entries(sets).sort((a, b) => b[1] - a[1]).slice(0, 4).map(([k, v]) => k + ' ' + v).join(', ');
  rows.push([g, G.S.points, G.penalty(), G.inHand() + G.pool(), adds, discards, end || 'all cleared', top]);
}
for (const r of rows) console.log(r.join(' | '));
