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

Since a player can almost always avoid checkmating the opponent, nearly all
positions are draws: up to 4 pieces, and in the 5-piece tables inspected
so far, only the checkmate positions themselves are wins. The tables are
correspondingly small (KQNvKQ: 172 KB WDL, 61 KB DTZ). With 4 threads, a
5-piece pawnless table takes about half a minute to generate and verify, a
5-piece pawnful table one to two and a half minutes. Memory requirements
are those of the chess generator.

## Limitations

* As for chess, pawnful tables with symmetric material (e.g. `KPvKP`)
  cannot be verified.
* Because nearly all positions are drawn, the DTZ rules for longer forced
  wins and losses (including cursed results) are hardly exercised by the
  tables tested so far (all tables with up to 5 pieces).

## Tables and probing

No generated tables are published yet. The tables are intended for
[Fairy-Stockfish](https://github.com/fairy-stockfish/Fairy-Stockfish).
