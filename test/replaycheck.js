// replaycheck.js -- replay codes, from index.html's own code.
//
// Plays random games with every kind of move (clears with and without
// melds, melds, kong upgrades, Add 6, Discard 6, Discard set), turns each
// into a code and reads it back, and checks:
//
//   - the replayed game ends exactly where the played one did;
//   - one mistyped character anywhere is put right;
//   - a code changed and re-signed with a fresh proof of work still fails
//     when a move it now describes is not legal, and a changed code that
//     was not re-signed fails the proof of work;
//   - the full proof of work (REPLAY_WORK bits) is found and checked.
//
// Usage: node test/replaycheck.js [index.html] [games]
'use strict';
const fs = require('fs');
const path = require('path');
const html = fs.readFileSync(process.argv[2] || path.join(__dirname, '..', 'index.html'), 'utf8');
const js = html.split('<script>')[1].split('</script>')[0];
const core = js.slice(js.indexOf('const HAND'), js.indexOf('/* ---------- render'));
const G = new Function(`function render(){}; ${core};
  return { newGame, get S() { return S; }, sel, clearSet, meldSet, addSix, discardSix, discardSet, findSet, canAdd, canMeld,
           pool, visible, get, meldKeys, classify, packGame, proveWork, hammingAdd, hammingFix, readCode, B64, REPLAY_WORK, murmur2 };`)();
const GAMES = +process.argv[3] || 300, WORK = 8;      // a light proof of work for the many; the real one at the end

let seed = 99;
const rnd = n => { seed = (seed * 1103515245 + 12345) >>> 0; return (seed >>> 8) % n; };
let failures = 0;
const fail = m => { failures++; if (failures <= 15) console.error('FAIL ' + m); };
// Where a game is, as the rules see it: everything but the moves list, the
// game's number in this device's count (a replay is never numbered), and
// which table slot holds which tile (a replay picks tiles by kind, so the
// same tiles may sit in other places; nothing in the rules depends on it).
const strip = s => { const o = JSON.parse(s); delete o.moves; delete o.game; o.hand = o.hand.filter(k => k !== null).sort((a, b) => a - b); return JSON.stringify(o); };

// a careless player: hints, random groups (mostly refused), melds of the
// hint's set, kong upgrades, Add 6, Discard 6, Discard set, clears with melds
function play(gseed) {
  G.newGame(gseed);
  for (let step = 0; step < 500 && !G.S.done; step++) {
    const r = rnd(100), vis = G.visible();
    G.sel.clear();
    const pickHint = () => { const s = G.findSet(true) || G.findSet(false); (s || []).forEach(k => G.sel.add(k)); return !!s; };
    if (r < 45) { if (pickHint()) G.clearSet(); }
    else if (r < 55) {
      if (G.S.melds.length) { G.meldKeys(rnd(G.S.melds.length)).forEach(k => G.sel.add(k)); if (rnd(2)) pickHint(); G.clearSet(); }
    } else if (r < 65) {
      const m = G.S.melds.findIndex(m => m.length === 3 && m[0] === m[2] && vis.some(([, k]) => k === m[0]));
      if (m >= 0) { G.meldKeys(m).forEach(k => G.sel.add(k)); G.sel.add(vis.find(([, k]) => k === G.S.melds[m][0])[0]); G.meldSet(); }
      else {
        // meld a pung when one shows, so kong upgrades come up
        const cnt = {}; vis.forEach(([, k]) => { cnt[k] = (cnt[k] || 0) + 1; });
        const k3 = Object.keys(cnt).find(k => cnt[k] >= 3);
        if (k3 !== undefined && G.S.melds.length < 4) { vis.filter(([, k]) => k === +k3).slice(0, 3).forEach(([key]) => G.sel.add(key)); G.meldSet(); }
        else if (pickHint() && G.canMeld()) G.meldSet();
      }
    } else if (r < 72) { if (G.canAdd()) G.addSix(); }
    else if (r < 80) { if (vis.length >= 6) { for (let i = 0; i < 6; i++) G.sel.add(vis[(i * 5 + rnd(3)) % vis.length][0]); if (G.sel.size === 6) G.discardSix(); } }
    else if (r < 86) { if (pickHint()) G.discardSet(); }
    else if (r < 92) { for (let i = 0, n = 2 + rnd(4); i < n && vis.length; i++) G.sel.add(vis[rnd(vis.length)][0]); G.clearSet(); }
    else { if (!pickHint() && G.S.melds.length) G.meldKeys(0).forEach(k => G.sel.add(k)); G.clearSet(); }
  }
  G.sel.clear();
  return JSON.stringify(G.S);
}

const lens = [], ops = {};
for (let g = 0; g < GAMES; g++) {
  const gseed = (g * 2654435761 + 7) >>> 0;
  const final = play(gseed), moves = JSON.parse(final).moves;
  moves.forEach(m => { ops[m.o] = (ops[m.o] || 0) + 1; });
  const { head, rest } = G.packGame(gseed, moves);
  const payload = G.proveWork(head, rest, rnd(1 << 20), 1 << 20, WORK);
  if (!payload) { fail(`game ${g}: no nonce`); continue; }
  const code = G.hammingAdd(payload);
  lens.push(code.length);
  const back = G.readCode(code, WORK);
  if (back.error) { fail(`game ${g}: read back as ${back.error} ${back.at || ''}`); continue; }
  if (strip(back.snaps[back.snaps.length - 1]) !== strip(final)) fail(`game ${g}: the replay ended somewhere else`);
  if (JSON.stringify(back.moves) !== JSON.stringify(moves)) fail(`game ${g}: the moves read back differ`);
  // one wrong character, anywhere
  const at = rnd(code.length), wrong = G.B64[(G.B64.indexOf(code[at]) + 1 + rnd(63)) % 64];
  const typo = G.readCode(code.slice(0, at) + wrong + code.slice(at + 1), WORK);
  if (typo.error || typo.fixed !== 1 || strip(typo.snaps[typo.snaps.length - 1]) !== strip(final)) fail(`game ${g}: a typo at ${at} was not put right (${typo.error})`);
  // tampering: one more Add 6 at the start, re-signed; and a change not re-signed
  if (moves.length) {
    let forged = null;
    try { forged = G.packGame(gseed, [{ o: 'A' }, { o: 'A' }, { o: 'A' }].concat(moves)); } catch (e) { /* not even packable: fine */ }
    const signed = forged && G.proveWork(forged.head, forged.rest, 0, 1 << 22, WORK);
    const f = signed ? G.readCode(G.hammingAdd(signed), WORK) : { error: 'pack' };
    if (!f.error && strip(f.snaps[f.snaps.length - 1]) === strip(final)) fail(`game ${g}: a forged game replayed to the same end`);
  }
  const p2 = payload.slice(0, 20) + G.B64[(G.B64.indexOf(payload[20]) + 1) % 64] + payload.slice(21);
  const t2 = G.readCode(G.hammingAdd(p2), WORK);
  if (!t2.error) fail(`game ${g}: a payload changed without new work was accepted`);
}
lens.sort((a, b) => a - b);
console.log(`replay: ${GAMES} games, moves ${JSON.stringify(ops)}; code length median ${lens[lens.length >> 1]}, max ${lens[lens.length - 1]}`);

// the real proof of work, once
{
  const final = play(12345), moves = JSON.parse(final).moves, t0 = Date.now();
  const { head, rest } = G.packGame(12345, moves);
  let payload = null;
  for (let from = 0; !payload && from < 1 << 30; from += 1 << 16) payload = G.proveWork(head, rest, from, 1 << 16);
  const code = G.hammingAdd(payload), back = G.readCode(code);
  if (back.error || strip(back.snaps[back.snaps.length - 1]) !== strip(final)) fail('the full proof of work did not read back');
  console.log(`replay: full proof of work (${G.REPLAY_WORK} bits) in ${Date.now() - t0} ms; ${code.length} chars: ${code}`);
}
if (failures) { console.error(`${failures} failures`); process.exit(1); }
console.log('replay codes check out');
