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

#endif
