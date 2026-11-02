# Shatranj

## Rules

Shatranj with the ferz (`Q`, one step diagonally) and the alfil (`B`,
jumps two squares diagonally). Pawns move one step and promote to a ferz.
A player who is checkmated or stalemated loses. A bare king loses, unless
it can capture the opponent's last piece with its next move, which draws.
The generator uses a 70-move rule instead of the 50-move rule.

## Building and usage

`make -C src shatranj` builds `jtbgen`, `jtbgenp`, `jtbver`, `jtbverp` and
`tbcheck`. Files have the suffixes `.jtbw`/`.jtbz`; subtables are looked up
in `$JTBPATH`, statistics are written to `$JTBSTATSDIR`, verification logs
to `jtblog.txt`. Since a bare king loses, tables like `KQvK` do not exist
and the smallest tables have 4 pieces.

## File format

Same layout as the chess tables, with WDL magic `0xb4e9b3b7` and DTZ magic
`0x87c126fc`.
