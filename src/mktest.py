#!/usr/bin/env python3
"""Consistency check of Makruk tablebases.

Independent re-implementation of the Makruk rules and of the semantics
described in MAKRUK.md. For random positions of a table the stored value is
recomputed from the stored values of all successor positions (probed with
mprobe) and compared.

Usage: mktest.py [-n samples] [-s seed] TABLE...   (MKTBPATH must be set)
"""

import argparse
import os
import random
import subprocess
import sys

KING, ROOK, KHON, KNIGHT, MET, PAWN = 'k', 'r', 's', 'n', 'm', 'p'
CAP = 129  # largest possible budget in plies (set by --variant)
VARIANT = 'makruk'


def pawn_rank_ok(color, r):
    if VARIANT == 'asean':  # pawns start on the 3rd rank, promote on the 8th
        return 2 <= r <= 6 if color == 0 else 1 <= r <= 5
    return 2 <= r <= 4 if color == 0 else 3 <= r <= 5


def promotion_rank(color):
    if VARIANT == 'asean':
        return 7 if color == 0 else 0
    return 5 if color == 0 else 2


def promotion_pieces():
    return [MET, ROOK, KHON, KNIGHT] if VARIANT == 'asean' else [MET]


def sq(f, r):
    return r * 8 + f


def on_board(f, r):
    return 0 <= f < 8 and 0 <= r < 8


def attacks(board, frm, piece, color):
    """Squares attacked by piece (lower case type) of color (0 white) on frm."""
    f, r = frm % 8, frm // 8
    fwd = 1 if color == 0 else -1
    res = []
    if piece == KING:
        steps = [(a, b) for a in (-1, 0, 1) for b in (-1, 0, 1) if a or b]
    elif piece == KNIGHT:
        steps = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1),
                 (-2, 1), (-1, 2)]
    elif piece == MET:
        steps = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
    elif piece == KHON:
        steps = [(1, 1), (1, -1), (-1, 1), (-1, -1), (0, fwd)]
    elif piece == PAWN:
        steps = [(1, fwd), (-1, fwd)]
    else:
        steps = None
    if steps is not None:
        for a, b in steps:
            if on_board(f + a, r + b):
                res.append(sq(f + a, r + b))
        return res
    for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        x, y = f + a, r + b
        while on_board(x, y):
            res.append(sq(x, y))
            if sq(x, y) in board:
                break
            x, y = x + a, y + b
    return res


def in_check(board, color):
    ksq = [s for s, (p, c) in board.items() if p == KING and c == color][0]
    for s, (p, c) in board.items():
        if c != color and ksq in attacks(board, s, p, c):
            return True
    return False


def moves(board, color):
    """Legal moves: list of (new_board, is_capture_or_promotion)."""
    res = []
    for s, (p, c) in list(board.items()):
        if c != color:
            continue
        targets = []
        if p == PAWN:
            fwd = 8 if color == 0 else -8
            if s + fwd not in board:
                targets.append(s + fwd)
            for t in attacks(board, s, p, c):
                if t in board and board[t][1] != color:
                    targets.append(t)
        else:
            for t in attacks(board, s, p, c):
                if t not in board or board[t][1] != color:
                    targets.append(t)
        for t in targets:
            if t in board and board[t][0] == KING:
                continue
            promo = p == PAWN and t // 8 == promotion_rank(color)
            for np in promotion_pieces() if promo else [p]:
                b = dict(board)
                capt = t in b
                del b[s]
                b[t] = (np, c)
                if in_check(b, color):
                    continue
                res.append((b, capt or np != p, capt, np != p))
    return res


def fen(board, color):
    rows = []
    for r in range(7, -1, -1):
        row, e = '', 0
        for f in range(8):
            x = board.get(sq(f, r))
            if x is None:
                e += 1
                continue
            if e:
                row += str(e)
                e = 0
            row += x[0].upper() if x[1] == 0 else x[0]
        if e:
            row += str(e)
        rows.append(row)
    return '/'.join(rows) + (' w' if color == 0 else ' b')


def non_king(board, color):
    return [p for p, c in board.values() if c == color and p != KING]


def count_limit(board, strong):
    pcs = non_king(board, strong)
    if VARIANT == 'cambodian':
        if pcs.count(ROOK) > 1: return 7
        if pcs.count(ROOK) == 1: return 15
        if pcs.count(KHON) > 1: return 21
        if pcs.count(KNIGHT) > 1: return 31
        if pcs.count(KHON) == 1: return 43
        return 63
    if VARIANT == 'asean':
        if ROOK in pcs: return 16
        if KHON in pcs: return 44
        if KNIGHT in pcs: return 64
        return 0
    if pcs.count(ROOK) > 1: return 8
    if pcs.count(ROOK) == 1: return 16
    if pcs.count(KHON) > 1: return 22
    if pcs.count(KNIGHT) > 1: return 32
    if pcs.count(KHON) == 1: return 44
    return 64


def fresh_budget(board):
    """Fresh counting budget in plies of a pawnless position."""
    w, b = non_king(board, 0), non_king(board, 1)
    if (w and b) or PAWN in w + b:
        return CAP  # board's honour (Cambodian: also with pawns)
    strong = 0 if w else 1
    if VARIANT == 'asean':
        # count starts at 0 plies; the 50-move rule applies as well
        lim = count_limit(board, strong)
        return min(2 * lim + 1, 100) if lim else 100
    return 2 * count_limit(board, strong) - 2 * len(board) + 1


class Prober:
    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True)
        self.p.stdout.readline()  # "Found ... tablebases."
        self.cache = {}

    def probe_many(self, fens):
        todo = [f for f in set(fens) if f not in self.cache]
        if todo:
            self.p.stdin.write(''.join(f + '\n' for f in todo))
            self.p.stdin.flush()
            for f in todo:
                line = self.p.stdout.readline().split()
                if line[0] == 'error':
                    raise RuntimeError('probe failed: ' + f)
                self.cache[f] = (int(line[0]), int(line[1]))
        return [self.cache[f] for f in fens]


def random_position(spec):
    w, b = spec.split('v')
    pieces = [(ch.lower(), 0) for ch in w] + [(ch.lower(), 1) for ch in b]
    while True:
        board = {}
        ok = True
        for p, c in pieces:
            s = random.randrange(64)
            if s in board:
                ok = False
                break
            if p == PAWN:
                r = s // 8
                if not pawn_rank_ok(c, r):
                    ok = False
                    break
            board[s] = (p, c)
        if not ok:
            continue
        color = random.randrange(2)
        if in_check(board, 1 - color):
            continue
        return board, color


def expected_pawnless(board, color, prober):
    """Expected (wdl, D) of a pawnless position from its successors."""
    succ = moves(board, color)
    if not succ:
        return (-2, 0) if in_check(board, color) else (0, 0)
    vals = prober.probe_many([fen(b, 1 - color) for b, _, _, _ in succ])
    best_win, worst_loss, has_draw, lost = None, 0, False, False
    both = non_king(board, 0) and non_king(board, 1)
    pawns_before = sum(1 for p, _ in board.values() if p == PAWN)
    for (b, _, capt, promo), (wdl, d) in zip(succ, vals):
        opp = 1 - color
        if VARIANT == 'cambodian':
            # Fairy-Stockfish do_move(): after a capture or promotion, a
            # fresh pieces' honour count starts if the opponent has a bare
            # king and no pawns are left, or if our bare king captured the
            # last pawn; otherwise the count continues
            pawns = sum(1 for p, _ in b.values() if p == PAWN)
            us, them = non_king(b, color), non_king(b, opp)
            fresh = (capt or promo) and not pawns and (
                (not them and us) or
                (capt and pawns < pawns_before - promo and not us and them))
            if (capt or promo) and not us and not them:
                has_draw = True
                continue
            if fresh:
                if wdl < 0:
                    best_win = 1
                elif wdl > 0:
                    lost = True
                else:
                    has_draw = True
                continue
            if wdl < 0:
                r = -d + 1
                best_win = r if best_win is None else min(best_win, r)
            elif wdl > 0:
                worst_loss = max(worst_loss, d + 1)
            else:
                has_draw = True
            continue
        if capt and VARIANT == 'asean' and both:
            # the capture resets the 50-move counter
            if wdl < 0:
                best_win = 1
            elif wdl > 0:
                lost = True
            else:
                has_draw = True
            continue
        if capt and not non_king(b, opp):
            # opponent left with a bare king: fresh pieces' honour count
            if non_king(b, color) and wdl < 0:
                r = 1
            else:
                has_draw = True
                continue
            best_win = r if best_win is None else min(best_win, r)
            continue
        if wdl < 0:
            r = -d + 1
            best_win = r if best_win is None else min(best_win, r)
        elif wdl > 0:
            worst_loss = max(worst_loss, d + 1)
        else:
            has_draw = True
    budget = fresh_budget(board)
    if best_win is not None and best_win <= CAP:
        return (2, best_win) if best_win <= budget else (0, 0)
    if has_draw or best_win is not None or worst_loss > CAP - 1:
        return (0, 0)
    if lost and not worst_loss:
        return (-2, 0)  # all moves are captures that lose at once
    return (-2, -worst_loss) if worst_loss <= budget else (0, 0)


def expected_pawnful(board, color, prober):
    succ = moves(board, color)
    if not succ:
        return -2 if in_check(board, color) else 0
    vals = prober.probe_many([fen(b, 1 - color) for b, _, _, _ in succ])
    best = -2
    for wdl, _ in vals:
        v = -wdl
        v = 2 if v > 0 else (-2 if v < 0 else 0)
        best = max(best, v)
    return best


def check_table(spec, n, prober):
    pawnful = 'P' in spec and VARIANT != 'cambodian'
    bad = 0
    for _ in range(n):
        board, color = random_position(spec)
        f = fen(board, color)
        wdl, d = prober.probe_many([f])[0]
        if pawnful:
            got = 2 if wdl > 0 else (-2 if wdl < 0 else 0)
            exp = expected_pawnful(board, color, prober)
            if got != exp:
                bad += 1
                print('MISMATCH %s: stored %d expected %d' % (f, wdl, exp))
        else:
            exp = expected_pawnless(board, color, prober)
            if (wdl, d) != exp:
                bad += 1
                print('MISMATCH %s: stored %s expected %s' % (f, (wdl, d), exp))
    print('%s: %d positions checked, %d mismatches' % (spec, n, bad))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-n', type=int, default=2000)
    ap.add_argument('-s', type=int, default=1)
    ap.add_argument('--variant', choices=['makruk', 'asean', 'cambodian'],
                    default='makruk')
    ap.add_argument('--mprobe', help='probing tool (default: mprobe, astbprobe)')
    ap.add_argument('tables', nargs='+')
    args = ap.parse_args()
    global VARIANT, CAP
    VARIANT = args.variant
    CAP = {'makruk': 129, 'asean': 100, 'cambodian': 127}[VARIANT]
    if not args.mprobe:
        args.mprobe = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   {'makruk': 'mprobe', 'asean': 'astbprobe',
                                    'cambodian': 'cmtbprobe'}[VARIANT])
    if VARIANT == 'asean' and any('P' in t for t in args.tables):
        # with the 50-move rule, WDL values of pawnful tables cannot be
        # checked without DTZ; use astbverp for them
        sys.exit('mktest.py: ASEAN pawnful tables are not supported')
    random.seed(args.s)
    prober = Prober(args.mprobe)
    bad = sum(check_table(t, args.n, prober) for t in args.tables)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
