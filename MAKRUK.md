# Makruk tablebases

This document describes the Makruk (Thai chess) support of the generator:
which rules are implemented, what the generated files contain, and how an
engine such as Fairy-Stockfish has to interpret them. The rules follow
Fairy-Stockfish's `makruk` variant exactly.

## Rules

| Piece | Letter | Movement |
|-------|--------|----------|
| King  | K | one step in any direction |
| Rook  | R | slides orthogonally |
| Khon  | S | one step diagonally, or one step straight forward |
| Knight| N | knight jump |
| Met   | M | one step diagonally (ferz) |
| Bia   | P | one step forward, captures one step diagonally forward |

* Pawns start on the 3rd (white) / 6th (black) rank, have no double step
  and no en passant. A pawn reaching the 6th (white) / 3rd (black) rank
  promotes to a Met. Hence white pawns only occur on ranks 3–5 and black
  pawns on ranks 4–6.
* Checkmate wins, stalemate is a draw. There is no bare-king rule and no
  50-move rule.
* **Counting rules** (Fairy-Stockfish `MAKRUK_COUNTING`). Counting is only
  active when no pawns are left on the board:
  * *Board's honour*: both sides have at least one piece besides the king.
    Limit 64 moves (128 plies); the count starts at 0 when the last pawn
    disappears. Captures do **not** reset the count.
  * *Pieces' honour*: one side has a bare king. The limit depends on the
    material of the strong side (2 rooks: 8, 1 rook: 16, 2 khons: 22,
    2 knights: 32, 1 khon: 44, otherwise 64 moves). The count is (re)started
    at 2·n plies, n = number of pieces on the board, when the bare king
    arises (2·n − 1 if the bare king itself captured the last pawn). Later
    captures by the bare king do not reset it.
  * The game is drawn as soon as `countingPly > countingLimit`, unless the
    side to move is checkmated.

## Table classes

### Pawnless tables (`KRSvKN`, `KMMvK`, ...)

The Khon only moves forward, so the 8-fold board symmetry used by Syzygy
for pawnless tables does not apply. Makruk pawnless tables therefore only
use the left-right mirror symmetry and a simple "raw" index (see below).

Counting makes the value of a pawnless position depend on the current
count, so these tables store a *distance* `D` (in plies) together with a
WDL value:

* `D` is the number of plies the winning side needs to force either
  checkmate or a capture that leaves the opponent with a bare king in a
  position that is won under the freshly started pieces' honour count.
  Captures that do not create a bare king do not end the distance
  (the count continues), so `D` is measured across them. Within
  bare-king tables `D` is simply the distance to mate.
* With the current count `c` and limit `L` (both in plies, as in
  Fairy-Stockfish's `StateInfo`), a position with distance `D` is decisive
  if and only if `D <= L - c + 1`; otherwise it is a draw.
* **WDL file**: the value assuming the count has just been started
  (board's honour: `c = 0`, `L = 128`; pieces' honour: `c = 2n`, `L` from
  the material). Values are loss (0), draw (2) and win (4) only. Every
  position is stored explicitly; unlike chess Syzygy tables there are no
  "capture don't-cares", so no capture search is needed (and a capture
  search would give wrong results, because captures do not reset the
  count).
* **DTZ file**: `D` for every non-drawn position, *for both sides to move*
  (the file has two sub-tables, like a WDL file, unless the material is
  symmetric). Decoding is the standard Syzygy DTZ decoding with
  ply-accurate flags, i.e. the stored value is `D - 1`.

Since the count at any reachable state is never more favourable than a
fresh count, a WDL draw is always a draw, and `D` only matters for
positions whose WDL value is a win or a loss.

### Pawnful tables (`KRPvKR`, `KSPvKM`, ...)

Counting is not active while pawns are on the board, and Makruk has no
50-move rule. These tables use the standard Syzygy pawnful encoding and
format (generator based on the Shatranj pawnful generator):

* WDL values ±2 and ±1 are both decisive. ±1 ("cursed win"/"blessed loss"
  in chess terms) only means that DTZ exceeds 140 plies (the internal
  `DRAW_RULE` inherited from the Shatranj generator); it has no meaning in
  Makruk and must be treated as a win/loss.
* DTZ is the usual distance to the next zeroing move (capture or pawn
  move), stored as in the Shatranj tables (cursed values relative to 140).
* Captures and promotions into pawnless positions are evaluated with the
  pawnless WDL values above, which is exact because counting starts fresh
  at that moment.

## Files

* Suffixes `.mkw` (WDL) and `.mkz` (DTZ). Tables are looked up in the
  directories listed in `$MKTBPATH`.
* Table names list the white pieces after `K` in the order `R S N M P`,
  e.g. `KRSvKN`. Both colour orders of a material combination are valid
  names; the generator and the probing code accept whichever file exists.
* Container format, Huffman coding and the pawnful index are those of
  Syzygy (see `src/compress.c`, `src/probe.c`).

### Raw index of pawnless tables

Pieces are stored in the order given by the header of each sub-table
(white king, black king, then the other pieces). The white king is mirrored
to files a–d (if it is on files e–h, all squares are mirrored `sq ^ 7`).
Then

```
idx = ((wk_rank * 4 + wk_file) * 64 + bk) << (6 * (n - 2))
      | sq[2] << (6 * (n - 3)) | ... | sq[n - 1]
```

Identical pieces are not sorted; every permutation is stored. Positions with
black to move in a non-symmetric table are looked up in the second
sub-table. For symmetric material and for probes where the colours of the
table are swapped, colours are swapped and the board is flipped vertically
(`sq ^ 0x38`) before mirroring, exactly as in Syzygy.

## Engine integration (Fairy-Stockfish)

* Pawnful position: probe WDL as for chess Syzygy tables, treat ±1 as ±2,
  ignore the 50-move rule.
* Pawnless position: look up WDL (no capture search). If it is a draw the
  position is drawn. Otherwise look up `D` in the DTZ file for the side to
  move and compare it with `countingLimit - countingPly + 1`; if counting
  has not started yet, use the WDL value.
* At the root in a pawnless position, the winning side plays a move to a
  position with minimal `D` (the opponent's loss distance + 1), the losing
  side one with maximal `D`.

## Generator

* `mtbgen` generates pawnless tables, `mtbgenp` pawnful tables
  (`make makruk`). Subtables must be generated first (see `src/run.py`).
* Generating a pawnless table needs `2 · 2048 · 64^(n-2)` bytes of RAM
  (about 1 GB for 5 pieces, 69 GB for 6 pieces).
