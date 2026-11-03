# Racing Kings

## Rules

There are no pawns. Checks are illegal: no move may leave either king in
check, so positions with a king in check are illegal. A player whose king
reaches the 8th rank wins, except that it is a draw if white reached the
8th rank first and black can reach it with its very next move. Stalemate
and the 50-move rule are as in chess.

## Building and usage

`make -C src racing` builds `ktbgen`, `ktbver` and `tbcheck`. Files have
the suffixes `.ktbw`/`.ktbz`; subtables are looked up in `$KTBPATH`,
statistics are written to `$KTBSTATSDIR`, verification logs to
`ktblog.txt`. Usage is the same as for `rtbgen`/`rtbver`, e.g.
`ktbgen -t 8 KRvKN`. Tables must be generated in order of increasing piece
count.

There is no color symmetry: black has the tie rule in its favour, so white
and black material cannot be swapped. `KRvK` and `KvKR` (and e.g. `KRvKN`
and `KNvKR`) are separate tables, and the white pieces always come first in
a table name. There are 8 tables with 3 pieces, 36 with 4, 120 with 5 and
330 with 6.

## File format

WDL magic `0x6b4f2ad9`, DTZ magic `0x3c91e75b`. Existing Syzygy probing
code cannot read the files; `probe_tb()` in `src/probe.c` shows how to
probe them.

* Only the left-right mirror symmetry is used. The generator keeps the
  white king on files a-d (1806 king pairs instead of 462), and the file
  format encodes the leading piece on files a-d.
* `KvK` is not stored; its value follows from the race rules and is
  computed by the probing code.
* Positions in which the race is over (a king on the 8th rank) are stored
  with their real value: a loss for the side to move if the opponent's king
  arrived, a draw if both kings are on the 8th rank or if black to move can
  still reach it. Unreachable positions (e.g. white to move with only the
  white king on the 8th rank) are "don't care". Probing code must
  therefore check for a finished race before resolving captures.
* DTZ values are rounded as for the chess tables generated without `-p`.

## Resources

A table has about 3.9 times as many positions as a chess table with the
same number of pieces.

| Pieces | Tables | RAM per table | Disk (WDL + DTZ) |
|---|---|---|---|
| 3 | 8 | ~25 MB | 60 KB |
| 4 | 36 | ~250 MB | 16 MB |
| 5 | 120 | 1.4-1.8 GB | 0.66 GB + 2.18 GB |
| 6 (estimate) | 330 | ~62 GB with `-d` | 350-750 GB |

A 5-piece table takes about 5 CPU minutes. 7-piece tables are not
realistic (about 4 TB of RAM).

## Tables and probing

The tables up to 5 pieces are available at
https://huggingface.co/datasets/ianfab/racing-kings-syzygy (CC0). Their
checksums (as printed by `tbcheck --print`) are in `checksums/racing/`.
