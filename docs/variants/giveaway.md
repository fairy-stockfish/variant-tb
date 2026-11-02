# Giveaway

## Rules

Losing chess with the antichess stalemate rule: capturing is compulsory,
the king is an ordinary piece, and a player who loses all pieces or is
stalemated wins. 50-move rule as in chess.

## Building and usage

`make -C src giveaway` builds `gtbgen`, `gtbgenp`, `gtbver`, `gtbverp` and
`tbcheck`. Files have the suffixes `.gtbw`/`.gtbz`; subtables are looked up
in `$GTBPATH`, statistics are written to `$GTBSTATSDIR`. Table names have no implicit
kings (e.g. `KvQ`, `RNvP`), and the smallest tables have 2 pieces.

## File format

WDL magic `0x21bc55bc`, DTZ magic `0x501bf5d6`. As for suicide, pawnless
suicide WDL tables (magic `0x1593f67b`) are accepted as subtables.

## Verification

`gtbver` (pawnless tables) and `gtbverp` (tables with pawns) recompute
all captures from the subtables and all "threats" (non-capture piece
moves that force the opponent to capture), and check every position's WDL
and DTZ values against its moves. Positions whose value is determined by
a capture or a threat have "don't care" DTZ values, and in tables with 6
or more pieces their WDL value is only checked as an upper bound, since
the probing code searches captures and threats itself (see
`src/sprobe.c`). Pawn moves are zeroing moves and are checked against the
WDL values of the positions they lead to; an en passant capture after a
double push is compulsory if it is the only capture. Stalemate positions
have DTZ 0. As for chess, pawnful tables with the same material on both
sides (e.g. `KPvKP`) cannot be verified.

## Limitations

Generating `KBNvK` currently fails: the generator exits in the
verification of the reconstructed table ("Verifying reconstructed table_w
based on collected statistics."), which is used for tables with long DTZ
values. This also happens with the original generator.

## Tables and probing

Lichess serves antichess tables at https://tablebase.lichess.ovh (probed with
[shakmaty-syzygy](https://github.com/niklasf/shakmaty-syzygy)).
