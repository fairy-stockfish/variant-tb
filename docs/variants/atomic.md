# Atomic

## Rules

Atomic chess: a capture explodes the capturing piece and all non-pawn
pieces on the adjacent squares; a side wins by exploding the opposing king.
Kings cannot capture. 50-move rule as in chess.

## Building and usage

`make -C src atbgen atbver atbverp tbcheck` builds the pawnless generator
`atbgen` and the verifiers. Files have the suffixes `.atbw`/`.atbz`;
subtables are looked up in `$ATBPATH`, statistics are written to
`$ATBSTATSDIR`, verification logs to `atblog.txt`.

## File format

Same layout as the chess tables, with WDL magic `0x49a48d55` and DTZ magic
`0xeb5ea991`.

## Limitations

`atbgenp` (pawnful tables) currently does not compile, so `make atomic`
fails and CI only tests pawnless tables.

## Tables and probing

Lichess serves atomic tables up to 6 pieces at
https://tablebase.lichess.ovh (probed with
[shakmaty-syzygy](https://github.com/niklasf/shakmaty-syzygy)).
