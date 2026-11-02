#!/usr/bin/env python3
"""
Smoke test for the tablebase generators of all supported variants.

For one variant this script
  1. generates every (canonical) table within the requested piece range,
     in dependency order, using the variant's generator binaries,
  2. runs the variant's verifier on each table (where a verifier exists),
  3. checks the checksum embedded in every generated file with tbcheck, and
  4. compares those checksums against the reference list in
     tests/checksums/<variant>.txt (regression check).

It is used by CI (.github/workflows/ci.yml), but can just as well be run
locally after building the binaries, e.g.

    make -C src all
    tests/variant_test.py regular

To (re)create the reference checksums after an intended change to the
generated tables, run with --update-checksums and commit the result.

Adding a new variant: add an entry to VARIANTS below, create its reference
checksums with --update-checksums, and add the variant to the CI matrix.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from itertools import combinations_with_replacement
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PIECES = "KQRBNP"


@dataclass
class Variant:
    prefix: str        # binary prefix: <prefix>tbgen, <prefix>tbgenp, ...
    suffix: str        # file suffix stem: .<suffix>w / .<suffix>z
    path_env: str      # environment variable with the table search path
    royal_king: bool   # one king per side (False for suicide/giveaway)
    min_pieces: int    # default piece range tested in CI
    max_pieces: int
    verify: bool = True        # verifier binaries exist
    verify_pawns: bool = True  # pawnful verifier exists
    pawns: bool = True         # variant has pawns
    bare_king_tables: bool = True  # KXvK tables exist (not in shatranj)
    color_symmetry: bool = True    # KXvKY also covers KYvKX


VARIANTS = {
    "regular":  Variant("r", "rtb", "RTBPATH", True, 3, 3),
    "atomic":   Variant("a", "atb", "ATBPATH", True, 3, 3),
    "suicide":  Variant("s", "stb", "STBPATH", False, 2, 3,
                        verify_pawns=False),
    "giveaway": Variant("g", "gtb", "GTBPATH", False, 2, 3,
                        verify_pawns=False),
    # Bare king loses in shatranj, so the smallest tables have 4 pieces.
    "shatranj": Variant("j", "jtb", "JTBPATH", True, 4, 4,
                        bare_king_tables=False),
    # Racing kings has no pawns, and white and black material cannot be
    # swapped, so e.g. KRvK and KvKR are separate tables.
    "racing":   Variant("k", "ktb", "KTBPATH", True, 3, 3, pawns=False,
                        color_symmetry=False),
}


def order(pieces):
    return [PIECES.index(c) for c in pieces]


def canonical_tables(v: Variant, npieces: int):
    """Canonical table names with npieces pieces (same naming as probe.c):
    side with more pieces first, pieces of each side in KQRBNP order and,
    for equal piece counts, the lexicographically smaller side first.
    Without color symmetry, white is always the first side."""
    names = set()
    if v.royal_king:
        others = npieces - 2
        for nw in range(others, -1, -1):
            nb = others - nw
            if nw < nb and v.color_symmetry:
                continue
            if nb == 0 and not v.bare_king_tables:
                continue
            for w in combinations_with_replacement(PIECES[1:], nw):
                for b in combinations_with_replacement(PIECES[1:], nb):
                    if (nw == nb and order(w) > order(b)
                            and v.color_symmetry):
                        continue
                    names.add("K" + "".join(w) + "vK" + "".join(b))
    else:
        # Suicide/giveaway: the king is an ordinary piece.
        for nw in range(npieces, 0, -1):
            nb = npieces - nw
            if nb < 1 or nw < nb:
                continue
            for w in combinations_with_replacement(PIECES, nw):
                for b in combinations_with_replacement(PIECES, nb):
                    if nw == nb and order(w) > order(b):
                        continue
                    names.add("".join(w) + "v" + "".join(b))
    if not v.pawns:
        names = {n for n in names if "P" not in n}
    # Dependency order: pawnless first, then by number of pawns (promotions
    # lead to tables with fewer pawns but the same number of pieces).
    return sorted(names, key=lambda n: (n.count("P"), n))


def is_symmetric(name: str) -> bool:
    w, b = name.split("v")
    return w == b


def run(cmd, cwd, env):
    res = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    return res.returncode, res.stdout


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    p.add_argument("variant", choices=sorted(VARIANTS))
    p.add_argument("--min", type=int, help="minimum number of pieces")
    p.add_argument("--max", type=int, help="maximum number of pieces")
    p.add_argument("--threads", "-t", type=int, default=os.cpu_count() or 1)
    p.add_argument("--bindir", type=Path, default=ROOT / "src",
                   help="directory containing the binaries (default: src/)")
    p.add_argument("--workdir", type=Path,
                   help="directory for the generated tables "
                        "(default: temporary directory, deleted afterwards)")
    p.add_argument("--update-checksums", action="store_true",
                   help="write the reference checksum file instead of "
                        "comparing against it")
    args = p.parse_args()

    v = VARIANTS[args.variant]
    lo = args.min if args.min is not None else v.min_pieces
    hi = args.max if args.max is not None else v.max_pieces
    bindir = args.bindir.resolve()

    tmp = None
    if args.workdir:
        workdir = args.workdir.resolve()
        workdir.mkdir(parents=True, exist_ok=True)
    else:
        tmp = tempfile.mkdtemp(prefix=f"tb-{args.variant}-")
        workdir = Path(tmp)

    env = dict(os.environ)
    env[v.path_env] = str(workdir)

    def binary(kind):
        return str(bindir / f"{v.prefix}tb{kind}")

    failures = []
    checksums = {}
    tables = [t for n in range(lo, hi + 1) for t in canonical_tables(v, n)]
    print(f"== {args.variant}: {len(tables)} tables with {lo}-{hi} pieces "
          f"in {workdir}", flush=True)

    for tb in tables:
        pawnful = "P" in tb
        files = [f"{tb}.{v.suffix}w", f"{tb}.{v.suffix}z"]

        rc, out = run([binary("genp" if pawnful else "gen"),
                       "-t", str(args.threads), tb], workdir, env)
        missing = [f for f in files if not (workdir / f).exists()]
        if rc != 0 or missing:
            failures.append(f"{tb}: generation failed (rc={rc})")
            print(f"{tb:12} generation FAILED\n{out}", flush=True)
            continue
        status = "generated"

        if v.verify and not (pawnful and (is_symmetric(tb)
                                          or not v.verify_pawns)):
            rc, out = run([binary("verp" if pawnful else "ver"),
                           "-t", str(args.threads), tb], workdir, env)
            if rc != 0 or "No errors." not in out:
                failures.append(f"{tb}: verification failed (rc={rc})")
                print(f"{tb:12} verification FAILED\n{out}", flush=True)
                continue
            status += ", verified"

        rc, out = run([str(bindir / "tbcheck")] + files, workdir, env)
        if rc != 0 or out.count("OK!") != len(files):
            failures.append(f"{tb}: embedded checksum check failed")
            print(f"{tb:12} tbcheck FAILED\n{out}", flush=True)
            continue

        rc, out = run([str(bindir / "tbcheck"), "--print"] + files,
                      workdir, env)
        for line in out.splitlines():
            name, _, sum_ = line.partition(": ")
            checksums[name] = sum_.strip()
        print(f"{tb:12} {status}, checksums OK", flush=True)

    ref_file = ROOT / "tests" / "checksums" / f"{args.variant}.txt"
    if args.update_checksums:
        if failures:
            print("Not updating reference checksums because of failures.")
        else:
            # Keep entries outside the tested piece range.
            ref = {}
            if ref_file.exists():
                for line in ref_file.read_text().splitlines():
                    name, _, sum_ = line.partition(": ")
                    ref[name] = sum_
            ref.update(checksums)
            ref_file.parent.mkdir(parents=True, exist_ok=True)
            ref_file.write_text("".join(f"{k}: {ref[k]}\n"
                                        for k in sorted(ref)))
            print(f"Wrote {len(ref)} checksums to {ref_file}")
    else:
        ref = {}
        if ref_file.exists():
            for line in ref_file.read_text().splitlines():
                name, _, sum_ = line.partition(": ")
                ref[name] = sum_
        else:
            failures.append(f"reference checksum file {ref_file} missing")
        for name, sum_ in sorted(checksums.items()):
            if name not in ref:
                failures.append(f"{name}: no reference checksum")
            elif ref[name] != sum_:
                failures.append(f"{name}: checksum {sum_} differs from "
                                f"reference {ref[name]}")

    if tmp:
        shutil.rmtree(tmp)

    if failures:
        print(f"\n{len(failures)} failure(s):")
        for f in failures:
            print(f"  {f}")
        return 1
    print(f"\nAll {len(tables)} {args.variant} tables OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
