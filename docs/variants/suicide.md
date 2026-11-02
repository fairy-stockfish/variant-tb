# Suicide

## Rules

Losing chess with FICS rules: capturing is compulsory, the king is an
ordinary piece (it can be captured, pawns can promote to it), and a player
who loses all pieces wins. If a player is stalemated, the player with
fewer pieces wins; equal piece counts are a draw. 50-move rule as in
chess.

## Building and usage

`make -C src suicide` builds `stbgen`, `stbgenp` and `tbcheck`. Files have
the suffixes `.stbw`/`.stbz`; subtables are looked up in `$STBPATH`,
statistics are written to `$STBSTATSDIR`. As there are no royal kings,
table names have no implicit kings (e.g. `KvQ`, `RNvP`), and the smallest
tables have 2 pieces.

## File format

WDL magic `0x1593f67b`, DTZ magic `0x23e7cfe4`. The stalemate rule only
matters in tables with pawns, so pawnless suicide and giveaway tables are
interchangeable: pawnless giveaway WDL tables (magic `0x21bc55bc`) are
accepted as subtables.

## Limitations

There is no verifier (`stbver`/`stbverp` are not built).
