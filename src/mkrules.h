/*
  Counting rules of the Makruk family as implemented by Fairy-Stockfish
  (Position::count_limit() and do_move()), see MAKRUK.md.

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef MKRULES_H
#define MKRULES_H

// largest possible budget in plies: distances beyond it are draws
#if defined(ASEAN)
#define MK_CAP 100   // 50-move rule
#elif defined(CAMBODIAN)
#define MK_CAP 127   // board's honour: 63 moves, count starts at 0
#else
#define MK_CAP 129   // board's honour: 64 moves, count starts at 0
#endif

// Counting limit in moves for a bare king against the given material of
// the strong side; 0 means that no counting starts.
static inline int mk_count_limit(int rooks, int khons, int knights)
{
#if defined(ASEAN)
  if (rooks) return 16;
  if (khons) return 44;
  if (knights) return 64;
  return 0;
#elif defined(CAMBODIAN)
  if (rooks > 1) return 7;
  if (rooks == 1) return 15;
  if (khons > 1) return 21;
  if (knights > 1) return 31;
  if (khons == 1) return 43;
  return 63;
#else
  if (rooks > 1) return 8;
  if (rooks == 1) return 16;
  if (khons > 1) return 22;
  if (knights > 1) return 32;
  if (khons == 1) return 44;
  return 64;
#endif
}

// Budget in plies with a freshly started count (and, for ASEAN, a fresh
// 50-move counter) for a position with n pieces. bare: one side has a bare
// king; rooks, khons, knights: material of the other side.
static inline int mk_fresh_budget(int n, int bare, int rooks, int khons,
    int knights)
{
  if (!bare) return MK_CAP;
  int l = mk_count_limit(rooks, khons, knights);
#if defined(ASEAN)
  // the count starts at 0; the 50-move rule applies as well
  return l && 2 * l + 1 < MK_CAP ? 2 * l + 1 : MK_CAP;
#else
  // the count starts at 2n plies
  return 2 * l - 2 * n + 1;
#endif
}

// ASEAN: a capture resets the 50-move counter, so the capturing move
// itself decides with the fresh value of the resulting position; this does
// not apply to captures by a bare king, where the count continues.
#if defined(ASEAN)
#define MK_CAPTURE_RESETS(both_sides_have_pieces) (both_sides_have_pieces)
#else
#define MK_CAPTURE_RESETS(both_sides_have_pieces) 0
#endif

// Pawns (ranks counted from 0): Makruk and Cambodian pawns stand on ranks
// 2-4 (white) / 3-5 (black) and promote to a met on rank 5 / 2; ASEAN pawns
// stand on ranks 2-6 / 1-5 and promote on rank 7 / 0.
#if defined(ASEAN)
#define MK_PAWN_RANK_OK(c, r) ((c) ? (r) >= 1 && (r) <= 5 : (r) >= 2 && (r) <= 6)
#define MK_PROMO_RANK(c) ((c) ? 0 : 7)
#else
#define MK_PAWN_RANK_OK(c, r) ((c) ? (r) >= 3 && (r) <= 5 : (r) >= 2 && (r) <= 4)
#define MK_PROMO_RANK(c) ((c) ? 2 : 5)
#endif

// Does a capture or promotion start a fresh pieces' honour count
// (Fairy-Stockfish do_move())? Arguments describe the position after the
// move: non-king pieces (including pawns) of the moving side and of the
// other side, the number of pawns left, and whether a pawn was captured.
// Returns 1 if the other side, which is to move, now counts with a bare
// king, 2 if the moving side counts (a bare king captured the last pawn),
// and 0 otherwise (the current count continues).
static inline int mk_fresh_count(int mover_pcs, int other_pcs, int pawns,
    int pawn_captured)
{
  if (pawns) return 0;
  if (other_pcs == 0 && mover_pcs > 0) return 1;
#if !defined(ASEAN)
  if (pawn_captured && mover_pcs == 0 && other_pcs > 0) return 2;
#endif
  (void)pawn_captured;
  return 0;
}

#endif
