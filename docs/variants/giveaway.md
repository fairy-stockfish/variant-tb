# Giveaway

## Rules

Losing chess with the antichess stalemate rule: capturing is compulsory,
the king is an ordinary piece, and a player who loses all pieces or is
stalemated wins. 50-move rule as in chess.

## Building and usage

`make -C src giveaway` builds `gtbgen`, `gtbgenp`, `gtbver` and `tbcheck`. Files
have the suffixes `.gtbw`/`.gtbz`; subtables are looked up in `$GTBPATH`,
statistics are written to `$GTBSTATSDIR`. Table names have no implicit
kings (e.g. `KvQ`, `RNvP`), and the smallest tables have 2 pieces.

## File format

WDL magic `0x21bc55bc`, DTZ magic `0x501bf5d6`. As for suicide, pawnless
suicide WDL tables (magic `0x1593f67b`) are accepted as subtables.

## Limitations

The verifier `gtbver` checks pawnless tables. It recomputes all captures
from the subtables and all "threats" (non-capture moves that force the
opponent to capture), and checks every position's WDL and DTZ values
against its moves. Positions whose value is determined by a capture or a
threat have "don't care" DTZ values, and in tables with 6 or more pieces
their WDL value is only checked as an upper bound, since the probing code
searches captures and threats itself (see `src/sprobe.c`). There is no
pawnful verifier yet (`gtbverp` is not built).

## Tables and probing

Lichess serves antichess tables at https://tablebase.lichess.ovh (probed with
[shakmaty-syzygy](https://github.com/niklasf/shakmaty-syzygy)).

Generating `KBNvK` currently fails: the generator exits in the
verification of the reconstructed table ("Verifying reconstructed table_w
based on collected statistics."), which is used for tables with long DTZ
values. This also happens without the verifier changes.
