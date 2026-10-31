# Makruk tablebases

This document describes the Makruk (Thai chess) support of the generator:
which rules are implemented, what the generated files contain, and how an
engine such as Fairy-Stockfish has to interpret them. The rules follow
Fairy-Stockfish's `makruk` variant exactly. Cambodian chess (Ouk Chatrang)
and ASEAN chess are supported as well, see
[Cambodian and ASEAN chess](#cambodian-and-asean-chess).

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

The Khon only moves forward, so tables with a khon only have the
left-right mirror symmetry; tables without khon have the full 8-fold
symmetry. Both use a simple "raw" index in which only canonical positions
are stored (see below).

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
* **DTZ file**: `D` for every non-drawn position, for *one* side to move
  (as in Syzygy: the generator compresses both sides and keeps the smaller
  one; flag bit 0 of the sub-table gives the stored side). The symbol `s`
  read from the sub-table is mapped as in Syzygy DTZ files
  (`v = map[0][s]` for wins and `v = map[1][s]` for losses if flag bit 2
  is set, otherwise `v = s`). Then `D = v + 1` for wins and `D = v` for
  losses (so that mated positions can be stored with `D = 0`).
* For the other side to move, `D` follows from a 1-ply search over all
  legal moves. A non-capture leads to a position of the stored side. A
  capture that leaves the opponent with a bare king counts as `D = 1` for
  the capturing side if the resulting position is a WDL loss for the bare
  king (a fresh count starts), otherwise as a draw. Any other capture
  leads to a subtable, whose `D` is used as is (probed recursively, since
  that may again need a search). A move into a position that the
  opponent loses in `D'` wins in `D' + 1`, a move into a position that the
  opponent wins in `D'` loses in `D' + 1`. A won position has the minimum
  over its winning moves, a lost position the maximum over all its moves. Probe the child's WDL first: a mated child has `D = 0`, which
  must not be mistaken for a draw. `probe_dtz_makruk()` in `src/probe.c`
  is the reference implementation.

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

Each sub-table header lists the piece types in index order (white king,
black king, then the other pieces; identical pieces are adjacent, and the
order of the remaining groups is chosen per sub-table for best
compression). The white king is mirrored to files a–d (if it is on files
e–h, all squares are mirrored `sq ^ 7`). Then

```
idx = ((wk_rank * 4 + wk_file) * 64 + bk) << (6 * (n - 2))
      | sq[2] << (6 * (n - 3)) | ... | sq[n - 1]
```

Only canonical positions are stored; all other indices are don't cares.
To probe, the position is transformed into its canonical form first:

1. Tables without khon: mirror the board horizontally (`sq ^ 7`) if the
   white king is on files e–h, vertically (`sq ^ 0x38`) if it is on ranks
   5–8, and along the a1–h8 diagonal if it is above the diagonal, so that
   it ends up in the triangle a1–d1–d4. Tables with khon: only mirror
   horizontally if the white king is on files e–h.
2. Sort the squares of identical pieces in ascending order.
3. Tables without khon, white king on the diagonal: also reflect the
   position in the diagonal and sort as in step 2, and use whichever of
   the two square sequences (in index order) is lexicographically smaller.

Positions with black to move in a non-symmetric table are looked up in the
second sub-table. For symmetric material and for probes where the colours
of the table are swapped, colours are swapped and the board is flipped
vertically (`sq ^ 0x38`) first, exactly as in Syzygy. `mk_probe_index()`
in `src/probe.h` is the reference implementation.

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

## Cambodian and ASEAN chess

Both variants use the Makruk pieces and letters, the raw index of the
pawnless tables, and `D` and WDL values with the same meaning as above;
only the rules that decide what a fresh count is differ. The counting rules
are collected in `src/mkrules.h`.

### Cambodian chess (`make cambodian`: `cmtbgen`, `cmtbver`, `cmtbprobe`)

Files `.cmw`/`.cmz`, path `$CMTBPATH`. Pieces, pawns and promotion are as
in Makruk. The special first moves of king and met are not supported (the
tables assume that these rights are gone, like Syzygy assumes no castling
rights). Counting (Fairy-Stockfish `CAMBODIAN_COUNTING`) differs from
Makruk:

* Board's honour (63 moves = 126 plies, count starting at 0) starts as soon
  as the side to move has at most two pieces besides its king, **also with
  pawns on the board**. Up to 5 pieces this is always the case, so the
  tables assume that the board's honour count is running in every position
  that is not under pieces' honour.
* Pieces' honour needs a bare king and no pawns; the limits are 7, 15, 21,
  31, 43 and 63 moves (count starting at 2n plies).
* A fresh count starts (Fairy-Stockfish `do_move()`) when, after a capture
  or promotion, the side to move has a bare king and no pawns are left, or
  when a bare king captures the last pawn. Any other move, including pawn
  moves and captures, continues the count.

Because the count also runs with pawns, **all** Cambodian tables, also
those with pawns, store `D` and are generated by `cmtbgen` with the raw
index (left-right mirroring only). Fresh budget: 127 plies, or
`2L - 2n + 1` with a bare king and no pawns. A move that starts a fresh
count after which the mover loses (e.g. a bare king capturing the last pawn
into a lost pieces' honour ending) loses at once: it counts as `D = 0` for
the mover, like a mate.

### ASEAN chess (`make asean`: `astbgen`, `astbgenp`, `astbver`, `astbverp`, `astbprobe`)

Files `.asw`/`.asz`, path `$ASTBPATH`. Pawns start on the 3rd rank, have no
double step and no en passant, and promote on the 8th rank to met, rook,
khon or knight. The 50-move rule applies. Counting (`ASEAN_COUNTING`) only
exists with a bare king and no pawns: 16 moves if the strong side has a
rook, 44 with a khon, 64 with a knight, none with mets only; the count
starts at 0.

* Pawnful tables are ordinary Syzygy tables with the 50-move rule
  (`astbgenp`, derived from the chess generator): WDL ±1 are cursed
  wins/blessed losses as in chess, DTZ is the distance to zeroing.
* Pawnless tables without a bare king (`astbgen`): every capture resets the
  50-move counter, so `D` is the distance to a winning capture or mate (as
  DTZ in Syzygy, but in plies and without cursed values: wins beyond the
  50-move rule are draws). A position is won iff `D <= 100 - rule50`. A
  capture into a position the opponent wins with a fresh counter loses at
  once (`D = 0`).
* Pawnless tables with a bare king: `D` is the distance to mate, also
  across captures by the bare king. Fresh budget `min(2L + 1, 100)`; a
  position is won iff `D <= min(countingLimit - countingPly + 1,
  100 - rule50)`. This is exact for rook and khon endings (the count is
  always the tighter limit there). For endings with knights or only mets,
  where the 50-move counter can be the tighter limit, a capture by the bare
  king resets the 50-move counter but not `D`, so the tables may call a
  position drawn that is actually won after such a capture (never the
  other way round).

## Tools

* `mtbgen KRSvKN` generates a pawnless table, `mtbgenp KRPvKR` a pawnful
  table (`make makruk` builds both, the verifiers, `mprobe` and
  `tbcheck`). Option
  `-t n` sets the number of threads.
* `src/run_makruk.py --max 5 --threads 4` generates all tables up to the
  given number of pieces in dependency order (stronger side as white);
  `--variant cambodian|asean` selects the variant, `--verify` verifies
  every table after generating it, `--stats FILE` records time, memory and
  file sizes.
* `mprobe` reads FENs (Makruk letters, only board and side to move are
  used) from stdin and prints the WDL value and `D`.
* `mtbver KRSvKN` / `mtbverp KRPvKR` verify every position of a table
  against all its successors (the subtables must be present). `mtbver` is
  an independent implementation of the rules; `mtbverp` is derived from
  the Shatranj verifier and cannot handle pawnful tables with symmetric
  material (e.g. KPvKP), like the chess verifier. `run_makruk.py --verify`
  verifies each table after generating it.
* `src/mktest.py [--variant ...] TABLE...` checks random positions of the
  given tables (for ASEAN only pawnless tables):
  it recomputes each stored value from the stored values of all successor
  positions with an independent Python implementation of the rules and of
  the semantics above.

All Makruk, Cambodian and ASEAN tables up to 4 pieces pass the verifiers
and `mktest.py`, and the counting limit `D <= L - c + 1` was checked against
Fairy-Stockfish's search.

## Resource usage

Measured on 4 cores (Xeon, 2.8 GHz) with `run_makruk.py --stats`.
"CPU" is user + system time, the wall clock time is about 40% of it with
4 threads. RAM is the peak resident size of a single generator run.

| Pieces | Tables | RAM (peak) | CPU time | Disk |
|--------|--------|------------|----------|------|
| 3 | 5 | 22 MB | 0.3 s | 19 KB |
| 4 | 30 | 220 MB | 5.5 min | 12 MB |
| 5 | 110 | 1.9 GB | ≈ 15 h (est.) | ≈ 4.5 GB (est.) |
| 6 | 365 | ≈ 120 GB (est.) | ≈ 3300 h (est.) | ≈ 1.4 TB (est.) |

3 and 4 pieces: all tables measured. 5 pieces: 11 tables measured and
extrapolated per table class (28 pawnless without khon, 32 pawnless with
khon, 50 pawnful):

| Class (5 pieces) | CPU per table | RAM | Disk per table |
|------------------|---------------|-----|----------------|
| pawnless, no khon (KRNvKM, KRNMvK, KRMvKR, KMMvKM) | 2.5–15 min | 1.6–1.8 GB | 0.9–22 MB |
| pawnless, khon (KSMvKM, KRSvKN) | 10–14 min | 1.8–1.9 GB | 104–138 MB |
| pawnful (KSPvKM, KRPvKR, KMMvKP, KMPvKM, KMPvKP) | 5–9 min | 0.4–0.65 GB | 0.3–20 MB |

Verification (`--verify`) costs somewhat more CPU time than generation:
for the first 14 Makruk 5-piece tables 5700 s against 4100 s. A complete
5-piece run of one variant including verification therefore takes roughly
a day on 4 cores. An interrupted run of `run_makruk.py --verify --stats`
can be resumed; tables that were generated but not verified are verified
first.

6 pieces cannot be measured on this machine. Estimates: pawnless tables
need `3 · 2048 · 64^4` bytes ≈ 103 GB plus about 20% (measured ratio at 5
pieces), pawnful tables about 16 GB with `-d` (as for chess). CPU time and
file sizes grow by a factor of 64–100 per piece in the measurements from 4
to 5 pieces; the disk estimate is dominated by the pawnless tables with a
khon (≈ 12 GB each) and is uncertain by a factor of 2–3.

## Limitations

* Generating a pawnless table needs `3 · 2048 · 64^(n-2)` bytes of RAM
  (two tables and one output buffer). The generator does not yet exploit
  the 8-fold symmetry internally, only in the files.
* `D` behaves like a distance to mate, so pawnless DTZ files are much
  larger than chess DTZ files (e.g. 22 MB for KRNvKM, 69 MB for KSMvKM).
  Tables with a khon only have the mirror symmetry and dominate the total
  size.
* KPvKP can only be checked with `mktest.py` (random positions).
