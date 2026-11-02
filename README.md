# variant-tb

[![CI](https://github.com/fairy-stockfish/variant-tb/actions/workflows/ci.yml/badge.svg)](https://github.com/fairy-stockfish/variant-tb/actions/workflows/ci.yml)

**variant-tb** is a fork of Ronald de Man's Syzygy tablebase
generator ([syzygy1/tb](https://github.com/syzygy1/tb)) that extends it with
support for more chess variants. Its main purpose is to generate endgame
tablebases that can be used by
[Fairy-Stockfish](https://github.com/fairy-stockfish/Fairy-Stockfish).

The original upstream documentation follows below the variant-specific
sections; unless stated otherwise it applies to all variants, with the
binary names, file suffixes and environment variables replaced as listed in
the table below.

### Supported variants

| Variant  | Build target (`make -C src ...`) | Binaries | Table files | Path variable | Notes |
|----------|----------------------------------|----------|-------------|---------------|-------|
| [Chess](docs/variants/regular.md)    | `all`      | `rtbgen`, `rtbgenp`, `rtbver`, `rtbverp` | `.rtbw`/`.rtbz` | `RTBPATH` | Identical to upstream Syzygy tables. |
| [Atomic](docs/variants/atomic.md)   | `atomic`   | `atbgen`, `atbgenp`, `atbver`, `atbverp` | `.atbw`/`.atbz` | `ATBPATH` | |
| [Suicide](docs/variants/suicide.md)  | `suicide`  | `stbgen`, `stbgenp`, `stbver`, `stbverp` | `.stbw`/`.stbz` | `STBPATH` | Losing chess, FICS stalemate rule. |
| [Giveaway](docs/variants/giveaway.md) | `giveaway` | `gtbgen`, `gtbgenp`, `gtbver`, `gtbverp` | `.gtbw`/`.gtbz` | `GTBPATH` | Losing chess, antichess stalemate rule. |
| [Shatranj](docs/variants/shatranj.md) | `shatranj` | `jtbgen`, `jtbgenp`, `jtbver`, `jtbverp` | `.jtbw`/`.jtbz` | `JTBPATH` | 70-move rule, bare king loses. |
| [Racing Kings](docs/variants/racing-kings.md) | `racing` | `ktbgen`, `ktbver` | `.ktbw`/`.ktbz` | `KTBPATH` | No pawns, no color symmetry (`KRvK` and `KvKR` are separate tables). |

Rules, file format details, resource requirements and known limitations of
each variant are documented in [docs/variants/](docs/variants/).

All variants use `tbcheck` (built by every target) to check the embedded
checksums. Statistics are written to `$<X>TBSTATSDIR` and verification logs
to `<x>tblog.txt`, with the same prefix letter as the binaries. In suicide and
giveaway the king is an ordinary piece, so table names have no implicit
kings (e.g. `KvQ`, `RNvP`).

Note that the top-level `make -C src <target>` invocations should not be run
in parallel with each other (e.g. `make -j atomic suicide`), because the
sub-makes of one variant share their object directory.


### Continuous integration

Every push and pull request runs `.github/workflows/ci.yml`. For each
supported variant it builds the binaries and runs `tests/variant_test.py`,
which

1. generates all tables of the smallest sizes (3 pieces for chess and atomic,
   2 and 3 pieces for suicide and giveaway, 4 pieces for shatranj, where bare
   king tables do not exist, 3 pieces for racing kings) in dependency order,
2. verifies them with the variant's verifier, where one exists,
3. checks the checksum embedded in every file with `tbcheck`, and
4. compares these checksums against the reference list in
   `tests/checksums/<variant>.txt`.

Step 4 catches unintended changes to the generated tables. For chess the
reference checksums are the official Syzygy checksums (see
`checksums/wdl345.txt` and `checksums/dtz345.txt`). Each variant takes from a
few seconds to about two minutes.

The test can be run locally after building the binaries:

```
make -C src all
tests/variant_test.py regular
```

Use `--min`/`--max` to test other table sizes and `--workdir DIR` to keep the
generated tables. If a change intentionally alters the generated tables,
regenerate the reference checksums with `--update-checksums` and commit them
together with the change, explaining why the tables changed.


### Adding a new variant

A new variant is added in one pull request that

1. implements the variant in the generator (a new `-D<VARIANT>` define, its
   own `src/Makefile.<variant>` and targets in `src/Makefile`, and a unique
   file suffix, path variable and magic numbers in `src/probe.h`),
2. documents the variant in `docs/variants/<variant>.md` (starting from
   [TEMPLATE.md](docs/variants/TEMPLATE.md)) and adds it to the table of
   supported variants above,
3. adds the variant to `VARIANTS` in `tests/variant_test.py`, creates its
   reference checksums with `tests/variant_test.py <variant> --update-checksums`,
   and
4. adds the variant to the build matrix in `.github/workflows/ci.yml`.

CI then makes sure that the new variant keeps working as the code evolves.


### Overview

This a generator for generating chess endgame database ("tablebases") for up
to 7 pieces.

The generator requires at least 16 GB of RAM for 6-piece tables and at least
1 TB of RAM for 7-piece tables.

Probing code for adding tablebase support to a chess engine or GUI
as well as a simple command line tool for probing the tablebases can be
found here:
https://github.com/syzygy1/probetool


### Tablebase files

File names encode the type of tablebase: K+R+P vs K+R becomes KRPvKR.
Each tablebase corresponds to two files: KRPvKR.rtbw and KRPvKR.rtbz.
Note that KRPvKR also covers K+R vs K+R+P.

The .rtbw files store win/draw/loss (WDL) information including, where
applicable, information on the 50-move rule. During the search only the
.rtbw files are accessed. These files are "two-sided": they store
information for both white to move and black to move.

The .rtbz files store distance-to-zero (DTZ/DTZ50) information: the number
of moves to the next capture or pawn move. These files only need to be
accessed when the root position has 6 (or 7) pieces or less. They are
"single-sided".

The 6-piece WDL tables are 68.2 GB in total. The DTZ tables take up 81.9 GB.
For up to 5 pieces, the numbers are 378 MB and 561 MB. At least the WDL
tables should be stored on an SSD for decent performance (when used by
a chess engine during search).


### Tablebase generator

The directory src/ contains the tablebase generator code. It should be
easy to build on x86-64 Linux and on 64-bit Windows with MinGW installed
("make all"). It might be useful (or necessary) to edit src/Makefile.
The Makefile expects the ZSTD compression library and header files to be
available. If they are not installed or not found, you can switch to LZ4

There are five programs:
* rtbgen for generating pawnless tablebases.
* rtbgenp for generating pawnful tablebases.
* rtbver for verifying pawnless tablebases.
* rtbverp for verifying pawnful tablebases.
* tbcheck for verifying the integrity of tablebase files based on an embedded
checksum.

**Note 1:** Since a correct set of checksums is known, there is no real need
to run rtbver and rtbverp.

**Note 2:** The checksums are **not** md5sums. However, correct md5sums are
known as well, and these can also be used to verify integrity.
See http://kirill-kryukov.com/chess/tablebases-online/

**Usage:** `rtbgen KQRvKR`   (or `rtbgenp KRPvKR`)  
Produces two compressed files: KQRvKR.rtbw and KQRvKR.rtbz. Both files
contain an embedded checksum.  

**Options:**  
--threads n  (or -t n)  
Use n threads. If no other tasks are running, it is recommended to
set n to the number of CPU cores, or if the CPU supports hyperthreading,
to the number of CPU hyperthreads.

--wdl  (or -w)  
Only compress and save the WDL file (with .rtbw suffix).

--dtz  (or -z)  
Only compress and save the DTZ file (with .rtbz suffix).

-g  
Generate the table but do not compress and save.

--stats  (or -s)  
Save statistics. Statistics are written to $RTBSTATSDIR/KQRvKR.txt
or to ./KQRvKR.txt if $RTBSTATSDIR is not set.

--disk  (or -d)  
Reduce RAM usage during compression. This takes a bit more time because
tables are temporarily saved to disk. **This option is necessary to
generate 6-piece tables on systems with 16 GB RAM.** This option is
not needed on systems with 24 GB RAM or more.

-p  
Always store DTZ values for non-cursed positions ply-accurate (at the
cost of slightly larger DTZ tables). Without this option, DTZ can be off
by one unless the table has position with DTZ=100 ply. The original Syzygy
tables were generated without this option.

**Usage:** `rtbver KQRvKR`   (or `rtbverp KRPvKR`)  
Verifies consistency of KQRvKR.rtbw and KQRvKR.rtbz. This should detect
(hardware) errors during generation and compression. For technical reasons
pawnful tables with symmetric material such as KPvKP and KRPvKRP cannot
(at least currently) be verified.

**Options:**  
--threads n  (or -t n)  
See above.

--log  (or -l)  
Log verification results to rtblog.txt.

-d  
Look for the WDL file in directory $RTBWDIR and look for the DTZ file in
directory $RTBZDIR. Without this option, both files should be present in
the current working directory.

**Usage:** `tbcheck KQRvKR.rtbw KRPvKR.rtbz`  
Recalculates a checksum for each specified tablebase file and compares with
the embedded checksums. This should detect disk errors and transmission
errors.

**Options:**  
--threads n  (or -t n)  
See above.

--print  (or -p)  
Print embedded checksums. Do not check correctness.

**Alternative usage:** `tbcheck --compare wdl345.txt`
Compares the embedded checksum for each tablebase file listed in wdl345.txt
with the checksum specified in wdl345.txt. Note that these are not md5sums.

Note: The programs rtbgen, rtbgenp, rtbver and rtbverp require access
to WDL tablebase files for "subtables". These should be present in
directories/folders listed in the **$RTPATH** environment variable. The
program tbcheck only looks in the current working directory.


### Scripts

The perl script src/run.pl or the python script src/run.py can be used for
generating and verifying all or part of the tables. Make sure the location
of rtbgen, rtbgenp, rtbver and rtbverp is in your $PATH environment variable.

**Usage:** `run.pl --generate` (or `run.py --generate`)

**Options:**  
--threads n  (or -t n)  
See above.

--generate  
Generate tablebases. Tablebases that already have been generated and are
found in the current working directory are skipped.

--verify  
Verify tablebases.

--min n  
Only treat tablebases with at least n pieces.

--max n  
Only treat tablebases with at most n pieces.

--disk  
Use this option to generate 6-piece tables on a system with 16 GB of RAM.


### Adding tablebase support to your program
See here:
https://github.com/syzygy1/probetool

### Terms of use

The files lz4.c and lz4.h in src/ are copyrighted by Yann Collet and were
released under the BSD 2-Clause License. The files city-c.c, city-c.h and
citycrc.h in src/ (ported by me from C++ to C) are copyrighted by Google,
Inc. and were released under an even more liberal license. Both licenses
are compatible with the GPL. The files c11threads\_win32.c and c11threads.h
are copyrighted by John Tsiombikas and Oliver Old and were placed in the
public domain. All other files in src/ are released under the GNU Public
License, version 2 (only).

The files main.cpp, search.cpp and types.h in interface/ obviously are
copyrighted by the Stockfish authors and covered by the Stockfish GPL with
the exception of the code fragments preceded by // TB comments. These
fragments may be freely modified and redistributed in source and/or binary
format.

All tablebase files generated using this generator may be freely redistributed.
In fact, those files are free of copyright at least under US law (following
Feist Publications, Inc., v. Rural Telephone Service Co., 499 U.S. 340 (1991))
and under EU law (following Football Dataco and Others v. Yahoo! UK Ltd and
Others (C-604/10)).


Ronald de Man  
syzygy\_tb@yahoo.com

