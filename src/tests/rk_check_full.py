#!/usr/bin/env python3
"""Usage: rk_check_full.py TABLE...   (needs python-chess, kprobe in $PATH)

Independent check of Racing Kings tables: solve all positions of the
given material configurations (and their capture successors) with
python-chess move generation and compare WDL with kprobe."""
import sys, subprocess, itertools
import chess, chess.variant

RANK8 = chess.BB_RANK_8

def terminal(b):
    """None if the race is not decided, else value for the side to move."""
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

def reachable(b):
    wk = b.king(chess.WHITE); bk = b.king(chess.BLACK)
    w8 = chess.square_rank(wk) == 7; b8 = chess.square_rank(bk) == 7
    if b.turn == chess.WHITE: return not (w8 and not b8)
    return not b8

def key(b):
    return b.board_fen() + (' w' if b.turn else ' b')

def positions(white, black):
    """all legal positions with the given non-king pieces"""
    pcs = [(chess.WHITE, p) for p in white] + [(chess.BLACK, p) for p in black]
    n = 2 + len(pcs)
    for sqs in itertools.permutations(range(64), n):
        b = chess.variant.RacingKingsBoard(None)
        b.set_piece_at(sqs[0], chess.Piece(chess.KING, chess.WHITE))
        b.set_piece_at(sqs[1], chess.Piece(chess.KING, chess.BLACK))
        for (c, p), s in zip(pcs, sqs[2:]):
            b.set_piece_at(s, chess.Piece(p, c))
        if chess.square_distance(sqs[0], sqs[1]) <= 1: continue
        for turn in (chess.WHITE, chess.BLACK):
            b.turn = turn
            if b.is_attacked_by(chess.BLACK, sqs[0]) or b.is_attacked_by(chess.WHITE, sqs[1]):
                continue
            if not reachable(b): continue
            yield b.copy()

def solve(configs):
    val = {}
    children = {}
    todo = []
    for white, black in configs:
        for b in positions(white, black):
            k = key(b)
            if k in val or k in children: continue
            t = terminal(b)
            if t is not None:
                val[k] = t
                continue
            ch = []
            for m in b.legal_moves:
                b.push(m); ch.append(key(b)); b.pop()
            children[k] = ch
    print("positions:", len(val) + len(children), file=sys.stderr)
    missing = {c for ch in children.values() for c in ch if c not in val and c not in children}
    if missing:
        print("missing successors (configs incomplete):", len(missing), next(iter(missing)), file=sys.stderr)
        sys.exit(1)
    changed = True
    while changed:
        changed = False
        for k, ch in children.items():
            if k in val: continue
            vs = [val.get(c) for c in ch]
            if any(v == -2 for v in vs):
                val[k] = 2; changed = True
            elif all(v == 2 for v in vs):
                val[k] = -2 if ch else 0; changed = True
    for k in children:
        if k not in val: val[k] = 0
    return val

def main():
    # args: table names like KRvK KvKR ... ; KvK is always included
    configs = [((), ())]
    names = {'Q': chess.QUEEN, 'R': chess.ROOK, 'B': chess.BISHOP, 'N': chess.KNIGHT}
    for t in sys.argv[1:]:
        w, b = t.split('v')
        configs.append((tuple(names[c] for c in w[1:]), tuple(names[c] for c in b[1:])))
    val = solve(configs)
    fens = list(val.keys())
    out = subprocess.run(["kprobe"], input="\n".join(f + " - - 0 1" for f in fens) + "\n",
                         capture_output=True, text=True).stdout
    res = {}
    for line in out.splitlines():
        if ": wdl " not in line: continue
        fen, rest = line.split(": wdl ")
        res[" ".join(fen.split()[:2])] = int(rest.split()[0])
    bad = 0
    for f in fens:
        v = res.get(f)
        # cursed results do not occur in these small tables
        if v is None or (v > 0) - (v < 0) != (val[f] > 0) - (val[f] < 0):
            bad += 1
            if bad <= 10: print("MISMATCH", f, "python", val[f], "kprobe", v)
    print(f"{' '.join(sys.argv[1:])}: {len(fens)} positions, {bad} mismatches")

main()
