# Chess

## Rules

Standard chess with the 50-move rule. Tables store WDL and DTZ50
information as described in the main [README](../../README.md#tablebase-files).

## Building and usage

`make -C src all` builds `rtbgen`, `rtbgenp`, `rtbver`, `rtbverp` and
`tbcheck`. Files have the suffixes `.rtbw`/`.rtbz`; subtables are looked up
in `$RTBPATH`, statistics are written to `$RTBSTATSDIR`. Usage and options
are documented in the [README](../../README.md#tablebase-generator).

## File format

The original Syzygy format (WDL magic `0x5d23e871`, DTZ magic
`0xa50c66d7`). The generated tables are identical to the official Syzygy
tables; reference checksums are in `checksums/`.

## Resources

The 6-piece tables take 68.2 GB (WDL) plus 81.9 GB (DTZ); up to 5 pieces
378 MB plus 561 MB. Generating 6-piece tables needs at least 16 GB of RAM
(with `-d`), 7-piece tables at least 1 TB.

## Tables and probing

The tables are widely available (e.g. http://tablebase.sesse.net/) and are
supported by most engines and GUIs; probing code:
https://github.com/syzygy1/probetool
