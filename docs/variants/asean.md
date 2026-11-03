# ASEAN chess

## Rules

ASEAN chess as implemented by Fairy-Stockfish's `asean` variant: the pieces
of [Makruk](makruk.md) (khon `S`, met `M`). Pawns start on the 3rd rank,
have no double step and no en passant, and promote on the 8th rank to met,
rook, khon or knight (without restrictions, as in Fairy-Stockfish).
Checkmate wins, stalemate is a draw, the 50-move rule applies.

Counting (Fairy-Stockfish `ASEAN_COUNTING`) only exists with a bare king and
no pawns: 16 moves if the strong side has a rook, 44 with a khon, 64 with a
knight, none with mets only. The count starts at 0 when the bare king
arises and is not reset by later captures of the bare king.

## Building and usage

`make -C src asean` builds `astbgen` (pawnless tables), `astbgenp` (pawnful
tables), the verifiers `astbver` and `astbverp`, `astbprobe` and `tbcheck`.
Files have the suffixes `.asw`/`.asz`; subtables are looked up in
`$ASTBPATH`, statistics of `astbgenp` are written to `$ASTBSTATSDIR`,
verification logs to `astblog.txt`. Table names and the helper scripts
(`src/run_makruk.py --variant asean`, `src/mktest.py --variant asean`) are
as for [Makruk](makruk.md).

## File format

WDL magic `0x6f1d2a93`, DTZ magic `0x2c8e47b5`.

* Pawnful tables are ordinary Syzygy tables with the 50-move rule
  (`astbgenp` is derived from the chess generator): WDL ±1 are cursed
  wins/blessed losses as in chess, DTZ is the distance to zeroing.
* Pawnless tables use the raw index, the WDL value for fresh counters and
  the distance `D` of the
  [Makruk pawnless tables](makruk.md#pawnless-tables-krsvkn-kmmvk-):
  * Without a bare king, every capture resets the 50-move counter, so `D`
    is the distance to a winning capture or mate (as DTZ in Syzygy, but in
    plies and without cursed values: wins beyond the 50-move rule are
    draws). A position is won iff `D <= 100 - rule50`. A capture into a
    position the opponent wins with a fresh counter loses at once (`D = 0`).
  * With a bare king, `D` is the distance to mate, also across captures by
    the bare king. Fresh budget `min(2L + 1, 100)`; a position is won iff
    `D <= min(countingLimit - countingPly + 1, 100 - rule50)`.

## Resources

Up to 4 pieces (measured, 4 cores): 35 tables, 6.5 CPU minutes for
generation and 1.5 for verification, 13 MB. The 5-piece tables were not
generated; an estimated 10–13 hours on 4 cores including verification,
2 GB RAM and 4–5 GB of disk space are needed (the pawnless tables cost about
as much as in Makruk, the pawnful ones as in chess).

## Limitations

* Bare king endings with knights or only mets: there the 50-move counter can
  be the tighter limit. A capture by the bare king resets it but not `D`, so
  the tables may call a position drawn that is actually won after such a
  capture (never the other way round). Rook and khon endings are exact,
  because the count is always the tighter limit there.
* Pawnful tables with symmetric material (KPvKP) cannot be checked by
  `astbverp`; `mktest.py` does not support ASEAN pawnful tables.
* Tested: all tables up to 4 pieces pass the verifiers (except KPvKP) and
  `mktest.py` (pawnless tables). 5-piece tables are untested.

## Tables and probing

No tables are published yet, and Fairy-Stockfish does not probe them yet;
see [Makruk](makruk.md#probing-in-an-engine).
