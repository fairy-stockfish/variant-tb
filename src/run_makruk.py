#!/usr/bin/env python3
"""
Generate Makruk tablebases (see MAKRUK.md) in dependency order.

Each material combination is generated once, with the stronger side as
white. Tables are processed by number of pieces and, for the same number
of pieces, by number of pawns, so that the subtables reached by captures
and promotions (pawn -> met) always exist.

Requires mtbgen and mtbgenp in $PATH (or --bindir). Tables are written to
the current directory, which is also used as MKTBPATH for subtables.
"""
import argparse
import os
import subprocess
import time
from itertools import combinations_with_replacement
from pathlib import Path

PIECES = ["R", "S", "N", "M", "P"]  # order used in table names
VALUE = {"R": 5, "S": 4, "N": 3, "M": 2, "P": 1}


def sides(max_len):
    for k in range(0, max_len + 1):
        for combo in combinations_with_replacement(PIECES, k):
            yield "".join(combo)


def strength(side):
    return (len(side), sorted((VALUE[p] for p in side), reverse=True))


def tables(min_pcs, max_pcs):
    res = set()
    for w in sides(max_pcs - 2):
        for b in sides(max_pcs - 2 - len(w)):
            n = 2 + len(w) + len(b)
            if n < max(3, min_pcs) or n > max_pcs:
                continue
            if strength(w) < strength(b):
                w2, b2 = b, w
            else:
                w2, b2 = w, b
            res.add((n, (w2 + b2).count("P"), "K" + w2 + "vK" + b2))
    return [t for _, _, t in sorted(res)]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--threads", type=int, default=1)
    ap.add_argument("--min", dest="min_pcs", type=int, default=3)
    ap.add_argument("--max", dest="max_pcs", type=int, default=4)
    ap.add_argument("--bindir", default="", help="directory of mtbgen/mtbgenp")
    ap.add_argument("--list", action="store_true", help="only list the tables")
    ap.add_argument("--stats", help="append resource usage per table to this CSV file")
    args = ap.parse_args()

    os.environ.setdefault("MKTBPATH", ".")
    for tb in tables(args.min_pcs, args.max_pcs):
        if args.list:
            print(tb)
            continue
        if Path(tb + ".mkz").exists():
            continue
        prog = "mtbgenp" if "P" in tb else "mtbgen"
        cmd = [os.path.join(args.bindir, prog), "-t", str(args.threads), tb]
        print("$ " + " ".join(cmd), flush=True)
        t0 = time.time()
        proc = subprocess.Popen(cmd)
        _, status, ru = os.wait4(proc.pid, 0)
        proc.returncode = os.waitstatus_to_exitcode(status)
        if proc.returncode != 0:
            raise SystemExit("Generating %s failed" % tb)
        if args.stats:
            new = not Path(args.stats).exists()
            with open(args.stats, "a") as f:
                if new:
                    f.write("table,pieces,wall_s,cpu_s,maxrss_mb,wdl_bytes,dtz_bytes\n")
                f.write("%s,%d,%.1f,%.1f,%.1f,%d,%d\n" % (
                    tb, len(tb) - 1, time.time() - t0, ru.ru_utime + ru.ru_stime,
                    ru.ru_maxrss / 1024, Path(tb + ".mkw").stat().st_size,
                    Path(tb + ".mkz").stat().st_size))


if __name__ == "__main__":
    main()
