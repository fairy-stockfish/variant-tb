# Giveaway

## Rules

Losing chess with the antichess stalemate rule: capturing is compulsory,
the king is an ordinary piece, and a player who loses all pieces or is
stalemated wins. 50-move rule as in chess.

## Building and usage

`make -C src giveaway` builds `gtbgen`, `gtbgenp` and `tbcheck`. Files
have the suffixes `.gtbw`/`.gtbz`; subtables are looked up in `$GTBPATH`,
statistics are written to `$GTBSTATSDIR`. Table names have no implicit
kings (e.g. `KvQ`, `RNvP`), and the smallest tables have 2 pieces.

## File format

WDL magic `0x21bc55bc`, DTZ magic `0x501bf5d6`. As for suicide, pawnless
suicide WDL tables (magic `0x1593f67b`) are accepted as subtables.

## Limitations

There is no verifier (`gtbver`/`gtbverp` are not built).

## Tables and probing

Lichess serves antichess tables at https://tablebase.lichess.ovh (probed with
[shakmaty-syzygy](https://github.com/niklasf/shakmaty-syzygy)).
