/*
  Makruk pawnless tablebase generator.

  This file is distributed under the terms of the GNU GPL, version 2.

  The value of a pawnless Makruk position depends on the counting rules
  (see MAKRUK.md). For every position we compute the counting distance D:
  the number of plies the winning side needs to force either checkmate or
  a capture that leaves the opponent with a bare king in a position that is
  won with a freshly started pieces' honour count. Captures that do not
  create a bare king do not reset the count, so D is measured across them
  (using the D values of the subtables).

  Wins always have odd D = 2j - 1, losses even D = 2j, so we store
  distances in moves: WIN(j) and LOSS(j). Values beyond the largest possible
  budget (MK_CAP plies) are draws.

  ASEAN (-DASEAN): the 50-move rule applies, so outside of bare king
  endings every capture resets the distance (as in Syzygy, see mkrules.h).

  The index is the raw mirror-symmetric index described in MAKRUK.md: the
  Khon only moves forward, so only left-right mirroring may be used.
*/

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "compress.h"
#include "huffman.h"
#include "defs.h"
#include "threads.h"
#include "util.h"

#define HAS_PAWNS
#include "board.h"
#include "mkrules.h"

static int white_king = 0, black_king = 1;

#include "probe.c"
#include "board.c"

#define MAX_PIECES 8

// value encoding (one byte per position and side to move)
#define ILLEGAL 0        // side to move can capture the opponent's king
#define WIN(j) (j)       // win in 2j - 1 plies, 1 <= j <= MAX_WIN
#define MAX_WIN ((MK_CAP + 1) / 2)
#define LOSS(j) (0x80 + (j)) // loss in 2j plies, 0 <= j <= MAX_LOSS
#define MATE LOSS(0)
#define MAX_LOSS (MK_CAP / 2)
#define DRAW 0xfc        // final draw (stalemate, drawing capture, ...)
#define CHANGED 0xfd     // needs to be checked for a loss
#define UNKNOWN 0xfe
#define BROKEN 0xff

#define IS_WIN(v) ((v) >= 1 && (v) <= MAX_WIN)
#define IS_LOSS(v) ((v) >= LOSS(0) && (v) <= LOSS(MAX_LOSS))

int numpcs;
int numpawns = 0;
int symmetric, split;
char *tablename;

static uint8_t *table[2];   // table[0]: white to move, table[1]: black to move
static uint64_t size;
static int pt[MAX_PIECES];
static int side_pcs[2][MAX_PIECES]; // slots of each side, terminated by -1
static int num_side[2];             // number of non-king pieces of each side
static int shift[MAX_PIECES];
static uint64_t mask[MAX_PIECES];
static uint64_t mirror_all;
static int budget;          // fresh budget in plies, see MAKRUK.md
static int num_pawns;       // number of pawns (Cambodian tables only)
static uint64_t *work_g;
static int minfreq = 8;

static inline int color_of(int k)
{
  return (pt[k] & 0x08) >> 3;
}

static inline int king_slot(int c)
{
  return c;
}

static inline void decode(uint64_t idx, int *p)
{
  for (int k = numpcs - 1; k >= 1; k--) {
    p[k] = idx & 0x3f;
    idx >>= 6;
  }
  p[0] = ((idx >> 2) << 3) | (idx & 0x03);
}

static inline uint64_t encode(const int *p)
{
  return mk_raw_index((int *)p, numpcs, 0);
}

// index after moving the piece in slot k to square sq
static inline uint64_t move_idx(uint64_t idx, const int *p, int k, int sq)
{
  if (k != 0)
    return (idx & ~mask[k]) | ((uint64_t)sq << shift[k]);
  int m = (sq & 0x04) ? 0x07 : 0;
  if (m) idx ^= mirror_all;
  sq ^= m;
  return (idx & ~mask[0]) | ((uint64_t)(((sq >> 3) << 2) | (sq & 0x03)) << shift[0]);
}

static inline bitboard occupancy(const int *p, int skip)
{
  bitboard occ = 0;
  for (int k = 0; k < numpcs; k++)
    if (k != skip) occ |= bit[p[k]];
  return occ;
}

static inline bitboard range_of(int k, int sq, bitboard occ)
{
  return PieceRange(sq, pt[k], occ);
}

// does side c attack square sq (slot skip is ignored, e.g. captured)
static inline int attacks(int c, int sq, const int *p, bitboard occ, int skip)
{
  for (int i = 0; side_pcs[c][i] >= 0; i++) {
    int k = side_pcs[c][i];
    if (k == skip) continue;
    if (range_of(k, p[k], occ) & bit[sq]) return 1;
  }
  return 0;
}

static inline int is_pawn(int k)
{
  return (pt[k] & 0x07) == PAWN;
}

// non-capture moves of the piece in slot k (for a pawn including a
// promotion, see is_promotion())
static inline bitboard quiet_moves(int k, const int *p, bitboard occ)
{
  if (!is_pawn(k))
    return range_of(k, p[k], occ) & ~occ;
  int to = p[k] + (color_of(k) ? -8 : 8);
  return (occ & bit[to]) ? 0 : bit[to];
}

static inline int is_promotion(int k, int to)
{
  return is_pawn(k) && (to >> 3) == MK_PROMO_RANK(color_of(k));
}

// squares from which the piece in slot k on sq can have made a non-capture
// (resp. capture) move to sq
static inline bitboard retro_quiet(int k, int sq, bitboard occ)
{
  if (!is_pawn(k))
    return PieceRetroMoves(sq, pt[k], occ);
  int c = color_of(k);
  int from = sq + (c ? 8 : -8);
  if ((sq >> 3) == MK_PROMO_RANK(c) || !MK_PAWN_RANK_OK(c, from >> 3)
      || (occ & bit[from]))
    return 0;
  return bit[from];
}

static inline bitboard retro_capture(int k, int sq, bitboard occ)
{
  if (!is_pawn(k))
    return PieceRetroMoves(sq, pt[k], occ);
  int c = color_of(k);
  if ((sq >> 3) == MK_PROMO_RANK(c)) return 0;
  int r = (sq >> 3) + (c ? 1 : -1);
  if (!MK_PAWN_RANK_OK(c, r)) return 0;
  return pawn_range[c ^ 1][sq] & ~occ;
}

static inline int pawns_ok(const int *p, int skip)
{
  for (int k = 2; k < numpcs; k++)
    if (k != skip && is_pawn(k) && !MK_PAWN_RANK_OK(color_of(k), p[k] >> 3))
      return 0;
  return 1;
}

static inline void atomic_min_u8(uint8_t *x, uint8_t v)
{
  uint8_t cur = __atomic_load_n(x, __ATOMIC_RELAXED);
  while (cur > v && cur != BROKEN && !IS_LOSS(cur)) {
    if (__atomic_compare_exchange_n(x, &cur, v, 0, __ATOMIC_RELAXED,
          __ATOMIC_RELAXED))
      break;
  }
}

static inline void set_changed(uint8_t *x)
{
  uint8_t cur = UNKNOWN;
  __atomic_compare_exchange_n(x, &cur, CHANGED, 0, __ATOMIC_RELAXED,
      __ATOMIC_RELAXED);
}

static int max_win_set;

static inline void note_win(int j)
{
  int cur = __atomic_load_n(&max_win_set, __ATOMIC_RELAXED);
  while (j > cur && !__atomic_compare_exchange_n(&max_win_set, &cur, j, 0,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

/*
  Result of a capture and/or promotion by side s (1 - c) leading to a
  position of a subtable with side c to move (captured: slot of the captured
  piece or -1, promoted: a pawn promoted).
  Returns j > 0 for WIN(j), -j - 1 for LOSS(j) and 0 for a draw.
*/
static int cross_result(int *pieces, int *gpos, int c, int captured,
    int promoted)
{
  int s = 1 - c;
  int s_left = num_side[s];
  int c_left = num_side[c] - (captured >= 0);
  int pawn_captured = captured >= 0 && is_pawn(captured);
  int pawns_left = num_pawns - pawn_captured - promoted;

  if (s_left == 0 && c_left == 0)
    return 0; // bare king against bare king

  if (captured >= 0 && MK_CAPTURE_RESETS(num_side[0] && num_side[1])) {
    // the 50-move counter starts afresh: the capture wins if c loses with
    // a fresh counter, and loses at once (D = 0) if c wins
    int v = probe_table(pieces, gpos, c == 0);
    return v < 0 ? WIN(1) : v > 0 ? -1 : 0;
  }

  if (mk_fresh_count(s_left, c_left, pawns_left, pawn_captured)) {
    // a pieces' honour count starts afresh: the move wins if c loses with
    // a fresh count, and loses at once (D = 0) if c wins
    int v = probe_table(pieces, gpos, c == 0);
    return v < 0 ? WIN(1) : v > 0 ? -1 : 0;
  }

  // the count continues; probe WDL first: a mated position has D = 0 like
  // a draw
  int w = probe_table(pieces, gpos, c == 0);
  int d = w ? probe_dtz_makruk(pieces, gpos, c == 0) : 0;
  if (w < 0) { // c loses in -d plies (even, 0 if mated)
    int j = (-d) / 2 + 1;
    return j <= MAX_WIN ? j : 0;
  } else if (w > 0) { // c wins in d plies (odd)
    int j = (d + 1) / 2;
    return j <= MAX_LOSS ? -j - 1 : 0;
  }
  return 0;
}

static void calc_init(struct thread_data *thread)
{
  int p[MAX_PIECES];

  for (uint64_t idx = thread->begin; idx < thread->end; idx++) {
    decode(idx, p);
    bitboard occ = occupancy(p, -1);
    if (__builtin_popcountll(occ) != numpcs || !pawns_ok(p, -1)) {
      table[0][idx] = table[1][idx] = BROKEN;
      continue;
    }
    table[0][idx] = attacks(0, p[1], p, occ, -1) ? ILLEGAL : UNKNOWN;
    table[1][idx] = attacks(1, p[0], p, occ, -1) ? ILLEGAL : UNKNOWN;
  }
}

static int capt_slot;

// Process captures of the piece in slot capt_slot: loop over the positions
// after the capture and mark all positions from which they are reached.
static void calc_captures(struct thread_data *thread)
{
  int p[MAX_PIECES], q[MAX_PIECES];
  int pieces[MAX_PIECES];
  int m = capt_slot;
  int c = color_of(m);
  int s = 1 - c;
  uint8_t *tbl = table[s];

  for (int k = 0; k < numpcs; k++)
    pieces[k] = pt[k];
  pieces[m] = 0;

  for (uint64_t idx = thread->begin; idx < thread->end; idx++) {
    if ((idx >> shift[m]) & 0x3f) continue;
    decode(idx, p);
    bitboard occ = occupancy(p, m);
    if (__builtin_popcountll(occ) != numpcs - 1 || !pawns_ok(p, m)) continue;
    // s just captured, so s must not be in check
    if (attacks(c, p[king_slot(s)], p, occ, m)) continue;

    int r = cross_result(pieces, p, c, m, 0);

    for (int i = 0; side_pcs[s][i] >= 0; i++) {
      int k = side_pcs[s][i];
      int sq = p[k];
      // the captured piece stood on sq
      if (is_pawn(m) && !MK_PAWN_RANK_OK(c, sq >> 3)) continue;
      bitboard bb = retro_capture(k, sq, occ);
      while (bb) {
        int o = FirstOne(bb);
        ClearFirst(bb);
        for (int l = 0; l < numpcs; l++) q[l] = p[l];
        q[k] = o;
        q[m] = sq;
        uint8_t *x = &tbl[encode(q)];
        if (r > 0) {
          atomic_min_u8(x, WIN(r));
          note_win(r);
        } else if (r == 0)
          atomic_min_u8(x, DRAW);
        else
          set_changed(x);
      }
    }
  }
}

// Promotions (with or without capture) lead to subtables; mark their
// results in the positions in which they can be played.
static void calc_promotions(struct thread_data *thread)
{
  int p[MAX_PIECES], q[MAX_PIECES];
  int pieces[MAX_PIECES];

  for (int s = 0; s < 2; s++) {
    int c = 1 - s;
    for (uint64_t idx = thread->begin; idx < thread->end; idx++) {
      uint8_t *x = &table[s][idx];
      if (*x == BROKEN || *x == ILLEGAL) continue;
      decode(idx, p);
      bitboard occ = occupancy(p, -1);
      for (int i = 0; side_pcs[s][i] >= 0; i++) {
        int k = side_pcs[s][i];
        if (!is_pawn(k) || (p[k] >> 3) + (s ? -1 : 1) != MK_PROMO_RANK(s))
          continue;
        bitboard bb = (range_of(k, p[k], occ) & occ) | quiet_moves(k, p, occ);
        while (bb) {
          int sq = FirstOne(bb);
          ClearFirst(bb);
          int m = -1;
          if (occ & bit[sq]) {
            for (m = 0; m < numpcs; m++)
              if (p[m] == sq) break;
            if (color_of(m) == s || m == king_slot(c)) continue;
          }
          for (int l = 0; l < numpcs; l++) {
            q[l] = p[l];
            pieces[l] = pt[l];
          }
          q[k] = sq;
          pieces[k] = QUEEN | (pt[k] & 0x08);
          if (m >= 0) pieces[m] = 0;
          bitboard occ2 = (occ & ~bit[p[k]]) | bit[sq];
          if (attacks(c, q[king_slot(s)], q, occ2, m)) continue; // illegal
          int r = cross_result(pieces, q, c, m, 1);
          if (r > 0) {
            atomic_min_u8(x, WIN(r));
            note_win(r);
          } else if (r == 0)
            atomic_min_u8(x, DRAW);
          else
            set_changed(x);
        }
      }
    }
  }
}

// does the side to move have a legal non-capture move?
static int has_move(int s, uint64_t idx, const int *p, bitboard occ)
{
  uint8_t *opp = table[1 - s];
  for (int i = 0; side_pcs[s][i] >= 0; i++) {
    int k = side_pcs[s][i];
    bitboard bb = quiet_moves(k, p, occ);
    while (bb) {
      int sq = FirstOne(bb);
      ClearFirst(bb);
      if (is_promotion(k, sq)) continue; // marked by calc_promotions()
      if (opp[move_idx(idx, p, k, sq)] != ILLEGAL) return 1;
    }
  }
  return 0;
}

static int check_loss(int s, uint64_t idx, int *p, bitboard occ, int j);

static void calc_mates(struct thread_data *thread)
{
  int p[MAX_PIECES];

  for (int s = 0; s < 2; s++)
    for (uint64_t idx = thread->begin; idx < thread->end; idx++) {
      int v = table[s][idx];
      if (v != UNKNOWN && v != CHANGED) continue;
      decode(idx, p);
      bitboard occ = occupancy(p, -1);
      if (has_move(s, idx, p, occ)) continue;
      if (v == CHANGED) {
        // Only captures or promotions, all losing. If they all lose at once
        // (D = 0, a fresh count or 50-move counter for the opponent), the
        // position is lost like a mate. It has to be found here, so that
        // the WIN(1) of its predecessors is dispatched by iterate().
        if (check_loss(s, idx, p, occ, 0) == 0)
          table[s][idx] = MATE;
        continue;
      }
      // no legal moves (legal captures and promotions would have been
      // marked)
      if (attacks(1 - s, p[king_slot(s)], p, occ, -1))
        table[s][idx] = MATE;
      else
        table[s][idx] = DRAW; // stalemate
    }
}

// Mark predecessors (side 1 - c to move) of the position idx (c to move).
// mode 0: UNKNOWN -> CHANGED, mode 1: win value v
static void retro(int c, uint64_t idx, const int *p, bitboard occ, int mode,
    int v)
{
  int s = 1 - c;
  uint8_t *tbl = table[s];
  for (int i = 0; side_pcs[s][i] >= 0; i++) {
    int k = side_pcs[s][i];
    bitboard bb = retro_quiet(k, p[k], occ);
    while (bb) {
      int o = FirstOne(bb);
      ClearFirst(bb);
      uint8_t *x = &tbl[move_idx(idx, p, k, o)];
      if (mode == 0)
        set_changed(x);
      else
        atomic_min_u8(x, v);
    }
  }
}

static int iter_j;
static int iter_found;

// Returns the loss value j >= 0 if all moves lose, -2 if the position is
// not (yet) known to be lost, and -1 if it is lost too slowly to matter
// (i.e. a draw).
static int check_loss(int s, uint64_t idx, int *p, bitboard occ, int j)
{
  uint8_t *opp = table[1 - s];
  int best = 0;

  for (int i = 0; side_pcs[s][i] >= 0; i++) {
    int k = side_pcs[s][i];
    bitboard bb = quiet_moves(k, p, occ);
    while (bb) {
      int sq = FirstOne(bb);
      ClearFirst(bb);
      if (is_promotion(k, sq)) continue; // handled below
      int v = opp[move_idx(idx, p, k, sq)];
      if (v == ILLEGAL) continue;
      if (!IS_WIN(v) || v > j) return -2;
      if (v > best) best = v;
    }
  }

  // all non-captures lose; captures and promotions are known to lose as
  // well (otherwise the position would have been marked as win or draw),
  // determine how fast
  int c = 1 - s;
  int pieces[MAX_PIECES], q[MAX_PIECES];
  for (int i = 0; side_pcs[s][i] >= 0; i++) {
    int k = side_pcs[s][i];
    bitboard bb = (range_of(k, p[k], occ) & occ) | quiet_moves(k, p, occ);
    while (bb) {
      int sq = FirstOne(bb);
      ClearFirst(bb);
      int m = -1;
      if (occ & bit[sq]) {
        for (m = 0; m < numpcs; m++)
          if (p[m] == sq) break;
        if (color_of(m) == s || m == king_slot(c)) continue;
      } else if (!is_promotion(k, sq))
        continue;
      for (int l = 0; l < numpcs; l++) {
        q[l] = p[l];
        pieces[l] = pt[l];
      }
      q[k] = sq;
      int promoted = is_promotion(k, sq);
      if (promoted) pieces[k] = QUEEN | (pt[k] & 0x08);
      if (m >= 0) pieces[m] = 0;
      bitboard occ2 = (occ & ~bit[p[k]]) | bit[sq];
      if (attacks(c, q[king_slot(s)], q, occ2, m)) continue; // illegal
      int r = cross_result(pieces, q, c, m, promoted);
      if (r >= 0) return -2; // should not happen
      if (-r - 1 > best) best = -r - 1;
    }
  }

  return best <= MAX_LOSS ? best : -1;
}

// phase 0: dispatch WIN(iter_j) of both sides;
// phase 1: check CHANGED positions of side iter_side.
// (Phase 1 handles one side at a time: losses found for one side are
// marked as wins of the other side, so the positions being written by
// the loop are never modified by other threads.)
static int iter_phase, iter_side;

static void iter(struct thread_data *thread)
{
  int p[MAX_PIECES];
  int found = 0;

  for (int s = 0; s < 2; s++) {
    if (iter_phase == 1 && s != iter_side) continue;
    uint8_t *tbl = table[s];
    for (uint64_t idx = thread->begin; idx < thread->end; idx++) {
      int v = tbl[idx];
      if (iter_phase == 0) {
        if (v != WIN(iter_j)) continue;
        decode(idx, p);
        retro(s, idx, p, occupancy(p, -1), 0, 0);
        found = 1;
      } else if (iter_phase == 1) {
        if (v != CHANGED) continue;
        found = 1;
        decode(idx, p);
        bitboard occ = occupancy(p, -1);
        int l = check_loss(s, idx, p, occ, iter_j);
        if (l == -2)
          tbl[idx] = UNKNOWN;
        else if (l == -1)
          tbl[idx] = DRAW;
        else {
          tbl[idx] = LOSS(l);
          if (l + 1 <= MAX_WIN) {
            retro(s, idx, p, occ, 1, WIN(l + 1));
            note_win(l + 1);
          }
        }
      } else { // iter_phase == 2: mates
        if (v != MATE) continue;
        decode(idx, p);
        retro(s, idx, p, occupancy(p, -1), 1, WIN(1));
        note_win(1);
      }
    }
  }

  if (found)
    __atomic_store_n(&iter_found, 1, __ATOMIC_RELAXED);
}

static void iterate(void)
{
  iter_phase = 2;
  run_threaded(iter, work_g, 0);

  for (iter_j = 1; iter_j <= MAX_WIN; iter_j++) {
    printf("Iteration %d ... ", iter_j);
    fflush(stdout);
    iter_found = 0;
    iter_phase = 0;
    run_threaded(iter, work_g, 0);
    iter_phase = 1;
    for (iter_side = 0; iter_side < 2; iter_side++)
      run_threaded(iter, work_g, 0);
    printf("done.\n");
    if (!iter_found && iter_j >= max_win_set) break;
  }
}

/* Statistics */

static uint64_t stats[2][256];
static LOCK_T stats_mutex;

static void collect(struct thread_data *thread)
{
  uint64_t st[2][256];
  memset(st, 0, sizeof(st));
  for (int s = 0; s < 2; s++)
    for (uint64_t idx = thread->begin; idx < thread->end; idx++)
      st[s][table[s][idx]]++;
  LOCK(stats_mutex);
  for (int s = 0; s < 2; s++)
    for (int i = 0; i < 256; i++)
      stats[s][i] += st[s][i];
  UNLOCK(stats_mutex);
}

static void print_stats(void)
{
  for (int s = 0; s < 2; s++) {
    uint64_t win = 0, fwin = 0, loss = 0, floss = 0, draw = 0;
    int longest_win = 0, longest_loss = -1;
    for (int j = 1; j <= MAX_WIN; j++)
      if (stats[s][WIN(j)]) {
        win += stats[s][WIN(j)];
        if (2 * j - 1 <= budget) fwin += stats[s][WIN(j)];
        longest_win = j;
      }
    for (int j = 0; j <= MAX_LOSS; j++)
      if (stats[s][LOSS(j)]) {
        loss += stats[s][LOSS(j)];
        if (2 * j <= budget) floss += stats[s][LOSS(j)];
        longest_loss = j;
      }
    draw = stats[s][DRAW] + stats[s][UNKNOWN] + stats[s][CHANGED];
    printf("%s to move: %"PRIu64" wins (%"PRIu64" with fresh count), "
           "%"PRIu64" draws, %"PRIu64" losses (%"PRIu64" with fresh count), "
           "%"PRIu64" mates\n", s ? "Black" : "White", win, fwin, draw, loss,
           floss, stats[s][MATE]);
    if (longest_win)
      printf("  longest win: %d plies\n", 2 * longest_win - 1);
    if (longest_loss > 0)
      printf("  longest loss: %d plies\n", 2 * longest_loss);
  }
  printf("Fresh counting budget: %d plies\n", budget);
}

/* Output */

static uint8_t wdl_v[256];
static uint8_t *map_src, *map_dst;
static int map_ord[MAX_PIECES];

// index of the generator table for an output index whose non-king pieces
// are stored in the slot order map_ord[]
static inline uint64_t src_index(uint64_t idx, const int *ord)
{
  uint64_t src = idx & (mask[0] | mask[1]);
  for (int i = 2; i < numpcs; i++)
    src |= ((idx >> shift[i]) & 0x3f) << shift[ord[i]];
  return src;
}

static uint8_t map_pc[MAX_PIECES];
static int map_sym8, map_dc;

static void set_map_order(const int *ord)
{
  memcpy(map_ord, ord, sizeof(map_ord));
  for (int k = 0; k < numpcs; k++)
    map_pc[k] = pt[ord[k]];
  map_sym8 = mk_sym8(map_pc, numpcs);
}

// value of an output index; non-canonical positions are don't cares
static inline uint8_t map_value(uint64_t idx, const uint8_t *src,
    const uint8_t *v)
{
  int q[MAX_PIECES];
  decode(idx, q);
  if (!mk_is_canonical(q, map_pc, numpcs, map_sym8))
    return map_dc;
  return v[src[src_index(idx, map_ord)]];
}

static void map_table(struct thread_data *thread)
{
  uint8_t *src = map_src;
  uint8_t *dst = map_dst;
  for (uint64_t idx = thread->begin; idx < thread->end; idx++)
    dst[idx] = map_value(idx, src, wdl_v);
}

static void run_map(uint8_t *dst, uint8_t *src, uint8_t *v, const int *ord)
{
  memcpy(wdl_v, v, 256);
  set_map_order(ord);
  map_src = src;
  map_dst = dst;
  run_threaded(map_table, work_g, 0);
}

// Choose the order of the groups of identical non-king pieces that
// compresses best, estimated on a random sample.
#define NUM_SAMPLES 256
#define SAMPLE_SIZE (16 * 1024)

static void choose_order(uint8_t *src, uint8_t *v, int wdl, int *best_ord)
{
  int groups[MAX_PIECES], glen[MAX_PIECES], ng = 0;
  for (int k = 2; k < numpcs; k++) {
    if (k > 2 && pt[k] == pt[k - 1]) {
      glen[ng - 1]++;
      continue;
    }
    groups[ng] = k;
    glen[ng++] = 1;
  }

  for (int k = 0; k < numpcs; k++)
    best_ord[k] = k;
  if (ng <= 1) return;

  uint64_t seg_size = SAMPLE_SIZE, num_segs = NUM_SAMPLES;
  if (seg_size * num_segs > size) {
    num_segs = 1;
    seg_size = size;
  }
  uint64_t dsize = seg_size * num_segs;
  uint8_t *dst = malloc(dsize);
  uint64_t segs[NUM_SAMPLES];
  srand(12345);
  for (uint64_t i = 0; i < num_segs; i++)
    segs[i] = num_segs == 1 ? 0 :
      (((uint64_t)rand() << 31) ^ rand()) % (size - seg_size);

  int perm[MAX_PIECES];
  for (int i = 0; i < ng; i++) perm[i] = i;
  uint64_t best = UINT64_MAX;
  while (1) {
    int ord[MAX_PIECES];
    ord[0] = 0;
    ord[1] = 1;
    for (int i = 0, k = 2; i < ng; i++)
      for (int j = 0; j < glen[perm[i]]; j++)
        ord[k++] = groups[perm[i]] + j;
    set_map_order(ord);
    for (uint64_t i = 0; i < num_segs; i++)
      for (uint64_t j = 0; j < seg_size; j++)
        dst[i * seg_size + j] = map_value(segs[i] + j, src, v);
    struct HuffCode *c = construct_pairs_u8(dst, dsize, 20, 100, wdl);
    uint64_t csize = calc_size(c);
    free(c);
    if (csize < best) {
      best = csize;
      memcpy(best_ord, ord, sizeof(ord));
    }
    // next permutation
    int i = ng - 2;
    while (i >= 0 && perm[i] > perm[i + 1]) i--;
    if (i < 0) break;
    int j = ng - 1;
    while (perm[j] < perm[i]) j--;
    int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    for (int a = i + 1, b = ng - 1; a < b; a++, b--) {
      t = perm[a]; perm[a] = perm[b]; perm[b] = t;
    }
  }
  free(dst);
  printf("piece order:");
  for (int k = 0; k < numpcs; k++)
    printf(" %d", pt[best_ord[k]]);
  printf("\n");
}

static void compress_side(struct tb_handle *G, uint8_t *buf, int s,
    uint8_t *v, int wdl)
{
  int ord[MAX_PIECES];
  uint8_t perm[TBPIECES];
  choose_order(table[s], v, wdl, ord);
  for (int k = 0; k < numpcs; k++)
    perm[k] = pt[ord[k]];
  run_map(buf, table[s], v, ord);
  printf("compressing data for %ctm/%s\n", s ? 'b' : 'w', wdl ? "wdl" : "dtz");
  compress_tb_u8(G, buf, size, perm, minfreq);
}

static void write_wdl(uint8_t *buf)
{
  int vals[5] = { 0, 0, 0, 0, 0 };
  uint8_t v[256];
  struct tb_handle *G = create_tb(tablename, 1, 6);

  compress_alloc_wdl();
  for (int s = 0; s < (split ? 2 : 1); s++) {
    for (int i = 0; i < 256; i++) v[i] = 2;
    v[ILLEGAL] = v[BROKEN] = 8;
    for (int j = 1; j <= MAX_WIN; j++)
      v[WIN(j)] = (2 * j - 1 <= budget) ? 4 : 2;
    for (int j = 0; j <= MAX_LOSS; j++)
      v[LOSS(j)] = (2 * j <= budget) ? 0 : 2;
    for (int i = 0; i < 5; i++) vals[i] = 0;
    for (int i = 0; i < 256; i++)
      if (stats[s][i] && v[i] < 5) vals[v[i]] = 1;
    compress_init_wdl(vals, 0);
    map_dc = 8;
    compress_side(G, buf, s, v, 1);
  }
  merge_tb(G);
}

static struct dtz_map dtz_maps[2];

static void sort_map(uint64_t *freq, uint16_t *map, uint16_t *inv_map,
    uint16_t *num)
{
  int n = 0;
  for (int i = 0; i < MAX_VALS; i++)
    map[i] = inv_map[i] = 0;
  for (int i = 0; i < 256; i++)
    if (freq[i]) map[n++] = i;
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (freq[map[i]] < freq[map[j]]) {
        uint16_t tmp = map[i];
        map[i] = map[j];
        map[j] = tmp;
      }
  for (int i = 0; i < n; i++)
    inv_map[map[i]] = i;
  *num = n;
}

// Only one side to move is stored in the DTZ file (as in Syzygy). Both
// sides are compressed and the smaller file is kept.
static void write_dtz(uint8_t *buf)
{
  uint8_t v[256];
  char name[2][64];
  uint64_t fsize[2] = { UINT64_MAX, UINT64_MAX };

  compress_alloc_dtz_u8();
  for (int s = 0; s < (split ? 2 : 1); s++) {
    sprintf(name[s], "%s%s", tablename, split ? (s ? "_b" : "_w") : "");
    struct tb_handle *G = create_tb(name[s], 0, 10);
    G->split = 0;
    struct dtz_map *map = &dtz_maps[s];
    uint64_t freq[4][256];
    memset(map, 0, sizeof(*map));
    memset(freq, 0, sizeof(freq));
    // wins store D - 1 = 2j - 2, losses store D = 2j (see MAKRUK.md)
    for (int j = 1; j <= MAX_WIN; j++)
      if (2 * j - 1 <= budget)
        freq[0][2 * j - 2] += stats[s][WIN(j)];
    for (int j = 0; j <= MAX_LOSS; j++)
      if (2 * j <= budget)
        freq[1][2 * j] += stats[s][LOSS(j)];
    for (int g = 0; g < 4; g++)
      sort_map(freq[g], map->map[g], map->inv_map[g], &map->num[g]);
    map->max_num = 1;
    for (int g = 0; g < 4; g++)
      if (map->num[g] > map->max_num) map->max_num = map->num[g];
    map->side = s;
    map->ply_accurate_win = map->ply_accurate_loss = 1;
    map->wide = 0;
    uint64_t tot = size / 3500;
    int i;
    for (i = 0; i < map->max_num; i++) {
      uint64_t f = 0;
      for (int g = 0; g < 2; g++)
        if (i < map->num[g]) f += freq[g][map->map[g][i]];
      if (f < tot) break;
    }
    map->high_freq_max = i;

    int dc = map->max_num;
    for (int i = 0; i < 256; i++) v[i] = dc;
    for (int j = 1; j <= MAX_WIN; j++)
      if (2 * j - 1 <= budget)
        v[WIN(j)] = map->inv_map[0][2 * j - 2];
    for (int j = 0; j <= MAX_LOSS; j++)
      if (2 * j <= budget)
        v[LOSS(j)] = map->inv_map[1][2 * j];

    compress_init_dtz_u8(map);
    map_dc = dc;
    compress_side(G, buf, s, v, 0);
    merge_tb(G);
    if (split) {
      char file[80];
      sprintf(file, "%s%s", name[s], DTZSUFFIX);
      FILE *F = fopen(file, "rb");
      fseek(F, 0, SEEK_END);
      fsize[s] = ftell(F);
      fclose(F);
    }
  }
  if (split) {
    int keep = fsize[1] < fsize[0];
    char from[80], to[80], other[80];
    sprintf(from, "%s%s", name[keep], DTZSUFFIX);
    sprintf(other, "%s%s", name[!keep], DTZSUFFIX);
    sprintf(to, "%s%s", tablename, DTZSUFFIX);
    printf("dtz: %ctm %"PRIu64" bytes, %ctm %"PRIu64" bytes, keeping %ctm\n",
        'w', fsize[0], 'b', fsize[1], keep ? 'b' : 'w');
    rename(from, to);
    remove(other);
  }
}

/* Main */


static struct option options[] = {
  { "threads", 1, NULL, 't' },
  { "wdl", 0, NULL, 'w' },
  { "dtz", 0, NULL, 'z' },
  { "affinity", 0, NULL, 'a' },
  { 0, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
  int val, longindex;
  int pcs[16];
  int generate_wdl = 1, generate_dtz = 1, only_generate = 0;

  numthreads = 1;
  while ((val = getopt_long(argc, argv, "at:gwz", options, &longindex)) != -1)
    switch (val) {
    case 'a':
      thread_affinity = 1;
      break;
    case 't':
      numthreads = atoi(optarg);
      break;
    case 'g':
      only_generate = 1;
      break;
    case 'w':
      generate_dtz = 0;
      break;
    case 'z':
      generate_wdl = 0;
      break;
    }

  if (optind >= argc) {
    fprintf(stderr, "No tablebase specified.\n");
    exit(1);
  }
  tablename = argv[optind];

  init_tablebases();

  for (int i = 0; i < 16; i++)
    pcs[i] = 0;
  int color = 0;
  numpcs = 0;
  for (int i = 0; i < (int)strlen(tablename); i++) {
    int t;
    switch (tablename[i]) {
    case 'K': t = KING; break;
    case 'R': t = ROOK; break;
    case 'S': t = BISHOP; break;
    case 'N': t = KNIGHT; break;
    case 'M': t = QUEEN; break;
    case 'v':
      if (color) exit(1);
      color = 0x08;
      continue;
    case 'P':
#ifdef CAMBODIAN
      t = PAWN;
      break;
#else
      fprintf(stderr, "Use mtbgenp for tables with pawns.\n");
      exit(1);
#endif
    default:
      fprintf(stderr, "Invalid table name.\n");
      exit(1);
    }
    pcs[t | color]++;
    numpcs++;
  }
  if (!color || pcs[WKING] != 1 || pcs[BKING] != 1) {
    fprintf(stderr, "Need one white king and one black king.\n");
    exit(1);
  }
  if (numpcs < 3 || numpcs > TBPIECES) {
    fprintf(stderr, "Need between 3 and %d pieces.\n", TBPIECES);
    exit(1);
  }

  // slots: white king, black king, then the other pieces grouped by type
  static const int order[] = { ROOK, BISHOP, KNIGHT, QUEEN, PAWN };
  int n = 0;
  pt[n++] = WKING;
  pt[n++] = BKING;
  for (int c = 0; c < 2; c++)
    for (int i = 0; i < 5; i++)
      for (int j = 0; j < pcs[order[i] | (c << 3)]; j++)
        pt[n++] = order[i] | (c << 3);
  for (int c = 0; c < 2; c++) {
    int j = 0;
    num_side[c] = 0;
    for (int k = 0; k < numpcs; k++)
      if (color_of(k) == c) {
        side_pcs[c][j++] = k;
        if (k > 1) num_side[c]++;
      }
    side_pcs[c][j] = -1;
  }

  symmetric = 1;
  for (int i = 0; i < 8; i++)
    if (pcs[i] != pcs[i + 8]) symmetric = 0;
  split = !symmetric;

  {
    int strong = num_side[0] ? 0 : 1;
    int rooks = 0, khons = 0, knights = 0;
    for (int i = 0; side_pcs[strong][i] >= 0; i++) {
      int t = pt[side_pcs[strong][i]] & 0x07;
      rooks += t == ROOK;
      khons += t == BISHOP;
      knights += t == KNIGHT;
    }
    num_pawns = pcs[WPAWN] + pcs[BPAWN];
    // pieces' honour needs a bare king and no pawns on the board
    budget = mk_fresh_budget(numpcs,
        !(num_side[0] && num_side[1]) && !num_pawns, rooks, khons, knights);
  }

  shift[0] = 6 * (numpcs - 1);
  mask[0] = 0x1fULL << shift[0];
  mirror_all = 0;
  for (int k = 1; k < numpcs; k++) {
    shift[k] = 6 * (numpcs - 1 - k);
    mask[k] = 0x3fULL << shift[k];
    mirror_all |= 0x07ULL << shift[k];
  }
  size = MK_RAW_SIZE(numpcs);

  if (numthreads < 1) numthreads = 1;
  printf("number of threads = %d\n", numthreads);
  total_work = (numthreads == 1) ? 1 : 100 + 10 * numthreads;
  work_g = create_work(total_work, size, 0x3f);

  table[0] = alloc_huge(2 * size);
  table[1] = table[0] + size;

  init_threads(0);
  set_up_tables();
  LOCK_INIT(stats_mutex);

  gettimeofday(&start_time, NULL);
  cur_time = start_time;

  printf("Initialising positions.\n");
  run_threaded(calc_init, work_g, 1);
  printf("Calculating captures.\n");
  for (capt_slot = 2; capt_slot < numpcs; capt_slot++)
    run_threaded(calc_captures, work_g, 1);
  if (num_pawns) {
    printf("Calculating promotions.\n");
    run_threaded(calc_promotions, work_g, 1);
  }
  printf("Calculating mate positions.\n");
  run_threaded(calc_mates, work_g, 1);

  iterate();

  run_threaded(collect, work_g, 0);
  printf("\n########## %s ##########\n\n", tablename);
  print_stats();

  if (only_generate)
    exit(0);

  uint8_t *buf = malloc(size);
  if (!buf) {
    fprintf(stderr, "Out of memory.\n");
    exit(1);
  }
  if (generate_wdl)
    write_wdl(buf);
  if (generate_dtz)
    write_dtz(buf);

  return 0;
}
