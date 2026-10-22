# Simulations

Bots that play Unprofitable Mahjong with the game's own code. Each one loads the rules straight out of `web/index.html`, so they always play the current version. They were used to balance the scoring.

Requires Node.js. Run them from the repository root:

```sh
node sim/hint.js web/index.html 5                              # follow Hint
node sim/careful.js web/index.html 50                          # careful, no leftovers
node sim/strategies.js web/index.html 50 8 melds summary noadd # big hands, never add
node sim/strategies.js web/index.html 5 8 melds rows wind       # wind mode, one row per game
```

The second argument is the number of games.

## The bots

- `hint.js` takes Hint's suggestion every turn, adding or discarding 6 when there is none.
- `careful.js` only clears sets that leave every remaining tile, on the table or still to come, groupable into sets.
- `strategies.js` takes more arguments:

  ```
  node sim/strategies.js web/index.html [games] [min set value] [melds|nomelds] [rows|summary] [mode]
  ```

  - **min set value:** the smallest set the bot waits for.
  - **melds / nomelds:** whether it may set melds aside.
  - **rows / summary:** one line per game, or totals at the end.
  - **mode:** `big`, `noadd`, `hybrid<wall>` (careful once the wall drops below `<wall>` tiles), `carefulmelds`, `stash<n>`, `wind` or `honor`.
