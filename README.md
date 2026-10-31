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


### Racing Kings

The generator also supports Racing Kings ("make racing"), which builds:
* ktbgen for generating Racing Kings tablebases (.ktbw/.ktbz files).
* ktbver for verifying them.
* kprobe, a simple command line tool that prints WDL and DTZ values of
positions given as FEN (`kprobe -m FEN` also lists all legal moves).

Tables are looked up in the directories listed in **$KTBPATH**.
Usage is the same as for rtbgen/rtbver, e.g. `ktbgen -t 8 KRvKN`.
All tables can be generated with `run.py --racing --generate --max 6`
(the script generates them in the right order).

Rules implemented: no move may leave the own king in check or give check
(so positions with a king in check are illegal); a player whose king
reaches the 8th rank wins, except that it is a draw if white reached the
8th rank first and black can reach it (with a legal move) on the very next
move. Stalemate and the 50-move rule are handled as in normal chess.

Differences from the regular tables:
* There is no color symmetry: white and black material are not
interchangeable (black has the tie rule in its favour), so KRvK and KvKR
are separate tables. Up to 6 pieces there are 494 tables.
* Only the left-right mirror symmetry can be used. The generator places the
white king on files a-d (1806 king pairs instead of 462), and the file
format encodes the leading piece on files a-d (WDL_MAGIC 0x6b4f2ad9,
DTZ_MAGIC 0x3c91e75b). Existing Syzygy probing code cannot read these
files; see src/probe.c and src/kprobe.c for reference probing code.
* KvK is not stored in a file; its value follows from the race rules and
is computed by the probing code.
* Positions in which the game is over (a king on the 8th rank) are stored
with their real value (loss for the side to move if the opponent's king
arrived, draw if both kings are on the 8th rank or if black to move can
still reach it). Unreachable positions (e.g. white to move with only the
white king on the 8th rank) are "don't care". Probing code must check for
a finished race before searching captures, as src/probe.c does.

Independent checks (using the move generator of python-chess) are in
src/tests: `rk_check_full.py KRvK` solves all positions of a small table
and compares them with kprobe, `rk_check_sample.py 1000 KQvKR KRvKNN`
checks the minimax consistency of random positions.

#### Resource requirements (Racing Kings)

A Racing Kings table is about 3.9 times larger (in positions) than a
regular table with the same number of pieces, because only the left-right
symmetry can be used.

| Pieces | Tables | RAM (peak per run) | Disk (WDL + DTZ) | CPU time (total) | Wall time, 4 threads |
|---|---|---|---|---|---|
| 3 (measured) | 8 | ~25 MB | 60 KB (20 KB + 40 KB) | ~3 s | ~2 s |
| 4 (measured) | 36 | ~250 MB | 16 MB (4.5 MB + 11.7 MB) | ~13.5 min | ~5 min |
| 5 (7 tables measured, rest estimated) | 120 | 1.4-1.8 GB | ~3.4 GB (~0.8 GB + ~2.6 GB) | ~10 CPU hours | ~3 h |
| 6 (estimated) | 330 | ~62 GB with -d, up to ~90 GB without | ~550 GB (~130 GB + ~420 GB), range 350-750 GB | ~1800 CPU hours, range 1200-4000 | 3-6 days with 32 cores |

Measurements were taken on a 4-core cloud machine with `ktbgen -t 4`.

* RAM: for 3 and 4 pieces it is mostly fixed overhead. From 5 pieces on,
the generator holds two tables of 1806 * 64^(n-2) bytes each (white and
black to move) plus, without -d, a buffer for the permuted table
(5 pieces: 2 * 0.47 GB + 0.46 GB; 6 pieces: 2 * 30.3 GB plus up to
27 GB without -d). ktbver also needs about 61 GB for 6-piece tables.
* Disk: with -d the generator writes intermediate files; plan roughly
50-100 GB of extra free space for 6-piece tables (estimate). The 6-piece
size is the least certain number: it assumes a per-table growth factor of
50-64 from 5 to 6 pieces (measured from 4 to 5 pieces: about 63).
* CPU: for 4 pieces most time is spent searching the best compression
permutation (8-29 s per table). A 5-piece table took 230-373 s CPU time
(about 290 s on average). The 6-piece estimate assumes about 5.5 CPU hours
per table (64 times more positions); it may be considerably slower
because memory accesses on 60 GB tables miss the caches much more often.
Verification adds about 20-25 % of the generation time.
* 7-piece tables are not realistic (about 4 TB of RAM).


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

