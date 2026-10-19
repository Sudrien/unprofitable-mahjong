// Follows Hint every turn. Usage: node sim/hint.js index.html [games]
// SPDX-License-Identifier: MIT
const fs = require('fs');
const html = fs.readFileSync(process.argv[2], 'utf8');
const js = html.split('<script>')[1].split('</script>')[0];
const core = js.slice(js.indexOf('const HAND'), js.indexOf('/* ---------- render'));
const make = new Function(`function render(){}; ${core};
  return { newGame, get S(){ return S; }, findSet, clearSet, addSix, discardSix, canAdd, pool, inHand, visible, sel, classify, get, penalty };`);

const N = +process.argv[3] || 5;
const results = [];
for (let g = 1; g <= N; g++) {
  const G = make();
  G.newGame();
  let adds = 0, discards = 0, turns = 0, dry = 0, stuckReason = '';
  const sets = {};
  for (;;) {
    if (++turns > 3000) { stuckReason = 'turn limit'; break; }
    const s = G.findSet(true) || G.findSet(false);
    if (s) {
      const t = G.classify(s.map(G.get));
      G.sel.clear(); s.forEach(k => G.sel.add(k)); G.clearSet();
      sets[t] = (sets[t] || 0) + 1; dry = 0;
    } else if (G.canAdd()) { G.addSix(); adds++; dry++; }
    else if (G.pool() > 0 && G.inHand() >= 6) {
      G.sel.clear(); G.visible().slice(0, 6).forEach(([k]) => G.sel.add(k)); G.discardSix(); discards++; dry++;
    } else { stuckReason = G.inHand() ? 'no sets, nothing to draw' : ''; break; }
    if (dry > 40) { stuckReason = 'looping through discards'; break; }
  }
  const S = G.S;
  const names = G.visible().map(([,k]) => k).sort((a,b)=>a-b).map(k => k<27 ? (k%9+1)+"mps"[Math.floor(k/9)] : ["E","S","W","N","Red","Green","White"][k-27]);
  results.push({ leftTiles: names.join(" "), game: g, points: S.points, penalty: G.penalty(), adds, discards, left: G.inHand() + G.pool(), stuckReason, sets });
}
console.log(JSON.stringify(results, null, 1));
