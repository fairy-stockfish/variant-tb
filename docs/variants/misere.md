# Misère

## Rules

Misère chess: all rules are those of chess, including the 50-move rule and
stalemate being a draw, except that a player who is checkmated **wins**.

## Building and usage

`make -C src misere` builds `mtbgen`, `mtbgenp`, `mtbver`, `mtbverp` and
`tbcheck`. Files have the suffixes `.mtbw`/`.mtbz`; subtables are looked up
in `$MTBPATH`, statistics are written to `$MTBSTATSDIR`, verification logs
to `mtblog.txt`. Usage, options and table naming are the same as for
`rtbgen`/`rtbgenp`/`rtbver`/`rtbverp`.

The binaries are built from the chess code with `-DREGULAR -DMISERE`; the
differences are confined to `#ifdef MISERE` blocks in `rtbgen.c`,
`rtbgenp.c`, `rtbver.c`, `rtbverp.c`, `stats.c` and `statsp.c`.

## File format

Same layout as the chess tables, with WDL magic `0xd839966b` and DTZ magic
`0x027ea0f0`. Reference checksums of all tables with up to 5 pieces are in
`checksums/misere/`.

* A checkmated position is stored as a win for the side to move. A move
  that checkmates the opponent is a loss with DTZ = -1.
* The DTZ value stored for a checkmated position is not meaningful (the
  game is already over; it is stored like a win with DTZ = 1). Probing code
  must detect checkmate before probing DTZ.
* After a double pawn push, a position may be checkmate if en passant is
  ignored, so that the en passant capture is the only legal move. As for
  chess, the tables store the position without en passant rights, i.e. as a
  win for the "checkmated" side; probing code must handle en passant
  captures itself, as it does for chess.

## Resources

Since a player can usually avoid checkmating the opponent, most positions
are draws. Up to 4 pieces, and in 94 of the 110 5-piece tables, only the
checkmate positions themselves are wins. The other 16 5-piece tables (all
with a queen or a pawn, e.g. `KQQvKQ`, `KQBvKP`, `KRRvKQ`) contain real
forced wins and losses: in `KQQvKQ` white to move wins 14% of the
positions, the longest win takes 100 ply and the longest cursed win 318
ply.

The tables are correspondingly small: all tables up to 5 pieces take 35 MB
(WDL) plus 10 MB (DTZ). Generating and verifying all of them takes 42
minutes with 6 threads. Memory requirements are those of the chess
generator.

## Limitations

* As for chess, pawnful tables with symmetric material (e.g. `KPvKP`)
  cannot be verified.

## Tables and probing

The tables up to 5 pieces are available at
https://huggingface.co/datasets/ianfab/misere-syzygy (CC0). The tables are
intended for [Fairy-Stockfish](https://github.com/fairy-stockfish/Fairy-Stockfish).
