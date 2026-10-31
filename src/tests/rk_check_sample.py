#!/usr/bin/env python3
"""Usage: rk_check_sample.py N TABLE...   (needs python-chess, kprobe in $PATH)

Random-sample check of Racing Kings tables against python-chess move
generation: for each sampled position, the sign of the WDL value returned
by kprobe must equal the minimax of the (signs of the) values of all
successors. Successor values come from kprobe as well (or from the race
rules for decided positions). DTZ: for won positions dtz > 0, for lost
positions dtz < 0, and the DTZ of a won position must be reachable."""
import sys, random, subprocess
import chess, chess.variant

def terminal(b):
    wk = b.king(chess.WHITE); bk = b.king(chess.BLACK)
    w8 = chess.square_rank(wk) == 7; b8 = chess.square_rank(bk) == 7
    if not w8 and not b8: return None
    if w8 and b8: return 0
    if b.turn == chess.WHITE: return -2 if b8 else 2
    if b8: return 2
    for m in b.legal_moves:
        if m.from_square == bk and chess.square_rank(m.to_square) == 7:
            return 0
    return -2

names = {'Q': chess.QUEEN, 'R': chess.ROOK, 'B': chess.BISHOP, 'N': chess.KNIGHT}

def random_position(table, rng):
    w, bl = table.split('v')
    pcs = [(chess.KING, chess.WHITE), (chess.KING, chess.BLACK)]
    pcs += [(names[c], chess.WHITE) for c in w[1:]] + [(names[c], chess.BLACK) for c in bl[1:]]
    while True:
        sqs = rng.sample(range(64), len(pcs))
        b = chess.variant.RacingKingsBoard(None)
        for (p, c), s in zip(pcs, sqs):
            b.set_piece_at(s, chess.Piece(p, c))
        b.turn = rng.choice([chess.WHITE, chess.BLACK])
        wk, bk = sqs[0], sqs[1]
        if chess.square_distance(wk, bk) <= 1: continue
        if b.is_attacked_by(chess.BLACK, wk) or b.is_attacked_by(chess.WHITE, bk): continue
        if terminal(b) is not None: continue
        return b

def sgn(x): return (x > 0) - (x < 0)

def main():
    n = int(sys.argv[1]); tables = sys.argv[2:]
    rng = random.Random(12345)
    jobs = []
    for t in tables:
        for _ in range(n):
            b = random_position(t, rng)
            ch = []
            for m in b.legal_moves:
                b.push(m)
                ch.append((b.fen(), terminal(b)))
                b.pop()
            jobs.append((t, b.fen(), ch))
    fens = set()
    for t, f, ch in jobs:
        fens.add(f)
        for cf, tv in ch:
            if tv is None: fens.add(cf)
    fens = sorted(fens)
    out = subprocess.run(["kprobe"], input="\n".join(fens) + "\n", capture_output=True, text=True).stdout
    res = {}
    for line in out.splitlines():
        if ": wdl " not in line: continue
        fen, rest = line.split(": wdl ")
        parts = rest.split()
        res[fen] = (int(parts[0]), int(parts[2]))
    bad = 0
    for t, f, ch in jobs:
        v, dtz = res[f]
        vals = []
        for cf, tv in ch:
            vals.append(-tv if tv is not None else -res[cf][0])
        expect = max([sgn(x) for x in vals]) if vals else 0
        ok = sgn(v) == expect and sgn(dtz) == sgn(v)
        if v in (2, -2) and abs(dtz) > 100: ok = False
        if v in (1, -1) and abs(dtz) <= 100: ok = False
        if not ok:
            bad += 1
            if bad <= 10: print("MISMATCH", t, f, "wdl", v, "dtz", dtz, "expected sign", expect)
    print(f"{len(jobs)} positions ({len(fens)} probes), {bad} mismatches")

main()
