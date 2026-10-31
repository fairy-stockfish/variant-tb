/*
  Makruk pawnless tablebase verifier.

  This file is distributed under the terms of the GNU GPL, version 2.

  Checks every position of a pawnless table against all its successors.
  The rules are implemented independently of the generator (mtbgen.c);
  shared are only the probing code for the subtables and the index
  definition of the file format (probe.h).

  1. The WDL file (both sides) and the DTZ file (one side to move, S) are
     decompressed into arrays in a mirror-only raw layout.
  2. For every position of the other side O the expected WDL value and D
     are computed from the successors (positions of S and subtables) and
     compared with the stored WDL value. The computed D is kept.
  3. For every position of S the expected WDL value and D are computed
     from the successors (the values of step 2 and subtables) and compared
     with the stored ones.
  As win and loss distances must lead to checkmate or a bare-king event,
  local consistency of all positions implies that the table is correct,
  provided the subtables are.
*/

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "board.h"
#include "decompress.h"
#include "defs.h"
#include "threads.h"
#include "util.h"

static int white_king = 0, black_king = 1;

#include "probe.c"
#include "board.c"

#define MAX_PIECES 8

#define CAP 129            // largest possible counting budget in plies
#define INVALID 0xff       // broken or illegal
#define V_DRAW 0
#define V_WIN(j) (j)       // win in 2j - 1 plies
#define V_LOSS(j) (0x80 + (j)) // loss in 2j plies
#define V_WIN_UNK 0x7f     // WDL win, D not known yet
#define V_LOSS_UNK 0xfe    // WDL loss, D not known yet

int numpcs;
int numpawns = 0;
char *tablename;

static int pt[MAX_PIECES];
static int num_side[2];
static int symmetric;
static int budget;
static uint64_t size;
static uint8_t *val[2];
static uint64_t *work;
static int stored_side;

static uint64_t errors;
static LOCK_T err_mutex;

static inline int col(int k) { return (pt[k] >> 3) & 1; }

static inline void decode(uint64_t idx, int *p)
{
  for (int k = numpcs - 1; k >= 1; k--) {
    p[k] = idx & 0x3f;
    idx >>= 6;
  }
  p[0] = ((idx >> 2) << 3) | (idx & 0x03);
}

// index of a position in the verifier's own layout (white king on a-d)
static inline uint64_t encode(const int *p)
{
  int m = (p[0] & 0x04) ? 0x07 : 0;
  int wk = p[0] ^ m;
  uint64_t idx = ((wk >> 3) << 2) | (wk & 0x03);
  for (int k = 1; k < numpcs; k++)
    idx = (idx << 6) | (p[k] ^ m);
  return idx;
}

static inline int attacked_by(int c, int sq, const int *p, bitboard occ,
    int skip)
{
  for (int k = 0; k < numpcs; k++)
    if (k != skip && col(k) == c && (PieceRange(p[k], pt[k], occ) & bit[sq]))
      return 1;
  return 0;
}

/* Step 1: decompress */

static uint8_t *dbuf;      // decompressed sub-table
static uint8_t dpc[TBPIECES]; // its piece order
static uint8_t (*dmap)[256];
static int dflags, fill_wdl;

// value of the decompressed sub-table for position p with side s to move
static inline int lookup(const int *p, int s)
{
  int pos[TBPIECES];
  int cm = 0, flip = 0;
  if (symmetric && s) {
    cm = 8;
    flip = 0x38;
  }
  int used = 0;
  for (int i = 0; i < numpcs; i++) {
    int k;
    for (k = 0; k < numpcs; k++)
      if (!(used & (1 << k)) && pt[k] == (dpc[i] ^ cm)) break;
    used |= 1 << k;
    pos[i] = p[k];
  }
  return dbuf[mk_probe_index(pos, dpc, numpcs, flip)];
}

static void fill(struct thread_data *thread)
{
  int p[MAX_PIECES];
  for (int s = 0; s < 2; s++) {
    if (!fill_wdl && s != stored_side && !symmetric) continue;
    for (uint64_t idx = thread->begin; idx < thread->end; idx++) {
      decode(idx, p);
      bitboard occ = 0;
      for (int k = 0; k < numpcs; k++) occ |= bit[p[k]];
      if (__builtin_popcountll(occ) != numpcs
          || attacked_by(s, p[1 - s], p, occ, -1)) {
        val[s][idx] = INVALID;
        continue;
      }
      if (fill_wdl) {
        if (fill_wdl == 2 && s == 0 && !symmetric) continue;
        if (fill_wdl == 1 && s == 1 && !symmetric) continue;
        int w = lookup(p, s) - 2;
        val[s][idx] = w > 0 ? V_WIN_UNK : w < 0 ? V_LOSS_UNK : V_DRAW;
      } else {
        int v = val[s][idx];
        if (v == V_DRAW) continue;
        int x = lookup(p, s);
        if (dflags & 2) x = dmap[v == V_WIN_UNK ? 0 : 1][x];
        // wins are stored as D - 1, losses as D
        if (v == V_WIN_UNK)
          val[s][idx] = V_WIN((x + 2) / 2);
        else
          val[s][idx] = V_LOSS(x / 2);
      }
    }
  }
}

/* Steps 2 and 3: check */

// Expected (wdl, D) of position p with side s to move. Returns D with the
// sign of the result (0 for a draw); *mate is set for checkmate.
static int expected(int s, const int *p, bitboard occ, int *mate)
{
  int pieces[MAX_PIECES], q[MAX_PIECES];
  int best_win = 0, worst_loss = 0, draw = 0, moves = 0;
  int king = s; // slot of the king of side s

  *mate = 0;
  for (int k = 0; k < numpcs; k++) {
    if (col(k) != s) continue;
    bitboard bb = PieceRange(p[k], pt[k], occ);
    while (bb) {
      int to = FirstOne(bb);
      ClearFirst(bb);
      int m = -1;
      if (occ & bit[to]) {
        for (m = 0; m < numpcs; m++)
          if (p[m] == to) break;
        if (col(m) == s || m == 1 - s) continue; // own piece or king
      }
      memcpy(q, p, sizeof(q));
      q[k] = to;
      bitboard occ2 = (occ & ~bit[p[k]]) | bit[to];
      if (attacked_by(1 - s, q[king], q, occ2, m)) continue;
      moves++;
      int r; // > 0: we win in r plies, < 0: we lose in -r plies, 0: draw
      if (m < 0) {
        int v = val[1 - s][encode(q)];
        if (v == V_DRAW) r = 0;
        else if (v >= V_LOSS(0) && v <= V_LOSS(64)) r = 2 * (v - V_LOSS(0)) + 1;
        else if (v >= 1 && v <= 65) r = -(2 * v);
        else {
          fprintf(stderr, "unexpected successor value %d\n", v);
          exit(1);
        }
      } else {
        for (int i = 0; i < numpcs; i++) pieces[i] = pt[i];
        pieces[m] = 0;
        q[m] = 0;
        int opp_left = num_side[1 - s] - 1;
        if (opp_left == 0) {
          // bare king: a fresh pieces' honour count starts
          r = num_side[s] && probe_table(pieces, q, s == 1) < 0 ? 1 : 0;
        } else {
          int w = probe_table(pieces, q, s == 1);
          int d = w ? probe_dtz_makruk(pieces, q, s == 1) : 0;
          r = w < 0 ? -d + 1 : w > 0 ? -(d + 1) : 0;
        }
      }
      if (r > 0) {
        if (!best_win || r < best_win) best_win = r;
      } else if (r < 0) {
        if (-r > worst_loss) worst_loss = -r;
      } else
        draw = 1;
    }
  }

  if (!moves) {
    if (attacked_by(1 - s, p[king], p, occ, -1)) {
      *mate = 1;
      return 0;
    }
    return 0; // stalemate
  }
  if (best_win && best_win <= CAP)
    return best_win <= budget ? best_win : 0;
  if (draw || best_win || worst_loss > CAP - 1)
    return 0;
  return worst_loss <= budget ? -worst_loss : 0;
}

static int check_side;

static void report(const int *p, int s, const char *what, int got, int exp)
{
  LOCK(err_mutex);
  if (errors < 20) {
    printf("ERROR (%s): %ctm, stored %d, expected %d, pieces:", what,
        s ? 'b' : 'w', got, exp);
    for (int k = 0; k < numpcs; k++)
      printf(" %c%c%c", "?PNSRMK?"[pt[k] & 7], 'a' + (p[k] & 7),
          '1' + (p[k] >> 3));
    printf("\n");
  }
  errors++;
  UNLOCK(err_mutex);
}

static void check(struct thread_data *thread)
{
  int p[MAX_PIECES];
  int s = check_side;
  int derive = !symmetric && s != stored_side;

  for (uint64_t idx = thread->begin; idx < thread->end; idx++) {
    int v = val[s][idx];
    if (v == INVALID) continue;
    decode(idx, p);
    bitboard occ = 0;
    for (int k = 0; k < numpcs; k++) occ |= bit[p[k]];
    int mate;
    int e = expected(s, p, occ, &mate);
    int ew = mate ? -1 : (e > 0) - (e < 0);
    int sw = v == V_DRAW ? 0 : (v <= 0x7f ? 1 : -1);
    if (sw != ew) {
      report(p, s, "wdl", sw, ew);
      continue;
    }
    if (derive) {
      if (mate) val[s][idx] = V_LOSS(0);
      else if (e > 0) val[s][idx] = V_WIN((e + 1) / 2);
      else if (e < 0) val[s][idx] = V_LOSS(-e / 2);
    } else if (sw) {
      int d = v <= 0x7f ? 2 * v - 1 : 2 * (v - V_LOSS(0));
      int ed = mate ? 0 : (e < 0 ? -e : e);
      if (d != ed)
        report(p, s, "dtz", d, ed);
    }
  }
}

static int count_limit(int c)
{
  int rooks = 0, khons = 0, knights = 0;
  for (int k = 0; k < numpcs; k++) {
    if (col(k) != c) continue;
    if ((pt[k] & 7) == ROOK) rooks++;
    if ((pt[k] & 7) == BISHOP) khons++;
    if ((pt[k] & 7) == KNIGHT) knights++;
  }
  if (rooks > 1) return 8;
  if (rooks == 1) return 16;
  if (khons > 1) return 22;
  if (knights > 1) return 32;
  if (khons == 1) return 44;
  return 64;
}

int main(int argc, char **argv)
{
  int val_opt;
  int pcs[16];

  numthreads = 1;
  while ((val_opt = getopt(argc, argv, "t:")) != -1)
    if (val_opt == 't') numthreads = atoi(optarg);
  if (optind >= argc) {
    fprintf(stderr, "No tablebase specified.\n");
    exit(1);
  }
  tablename = argv[optind];

  memset(pcs, 0, sizeof(pcs));
  int c = 0;
  numpcs = 0;
  pt[numpcs++] = WKING;
  pt[numpcs++] = BKING;
  for (char *s = tablename; *s; s++) {
    int t;
    switch (*s) {
    case 'K': continue;
    case 'v': c = 8; continue;
    case 'R': t = ROOK; break;
    case 'S': t = BISHOP; break;
    case 'N': t = KNIGHT; break;
    case 'M': t = QUEEN; break;
    default:
      fprintf(stderr, "Invalid pawnless table name.\n");
      exit(1);
    }
    pt[numpcs++] = t | c;
  }
  for (int k = 0; k < numpcs; k++) pcs[pt[k]]++;
  num_side[0] = num_side[1] = 0;
  for (int k = 2; k < numpcs; k++) num_side[col(k)]++;
  symmetric = 1;
  for (int i = 0; i < 8; i++)
    if (pcs[i] != pcs[i + 8]) symmetric = 0;
  if (num_side[0] && num_side[1])
    budget = CAP;
  else
    budget = 2 * count_limit(num_side[0] ? 0 : 1) - 2 * numpcs + 1;

  init_tablebases();
  set_up_tables();
  total_work = (numthreads == 1) ? 1 : 100 + 10 * numthreads;
  init_threads(0);
  LOCK_INIT(err_mutex);
  size = MK_RAW_SIZE(numpcs);
  work = create_work(total_work, size, 0);
  val[0] = alloc_huge(2 * size);
  val[1] = val[0] + size;

  decomp_init_piece(pcs);
  struct tb_handle *H = open_tb_handle(tablename, 1);
  decomp_init_table(H);
  struct TBEntry_piece *e = &H->entry_piece;
  for (int side = 0; side < (symmetric ? 1 : 2); side++) {
    printf("Decompressing %ctm/wdl.\n", side ? 'b' : 'w');
    dbuf = decompress_table(H, side, 0);
    memcpy(dpc, e->pieces[side], TBPIECES);
    fill_wdl = side ? 2 : 1;
    if (symmetric) fill_wdl = 3;
    run_threaded(fill, work, 0);
  }
  H = open_tb_handle(tablename, 0);
  decomp_init_table(H);
  e = &H->entry_piece;
  stored_side = get_dtz_side(H, 0);
  printf("Decompressing %ctm/dtz.\n", stored_side ? 'b' : 'w');
  dbuf = decompress_table(H, 0, 0);
  memcpy(dpc, e->pieces[0], TBPIECES);
  dflags = H->dtz_flags[0];
  dmap = get_dtz_map(H, 0);
  fill_wdl = 0;
  run_threaded(fill, work, 0);

  if (!symmetric) {
    check_side = 1 - stored_side;
    printf("Checking %ctm.\n", check_side ? 'b' : 'w');
    run_threaded(check, work, 0);
  }
  check_side = stored_side;
  printf("Checking %ctm.\n", check_side ? 'b' : 'w');
  run_threaded(check, work, 0);
  if (symmetric) {
    check_side = 1;
    run_threaded(check, work, 0);
  }

  printf("%s: %s (%"PRIu64" errors)\n", tablename, errors ? "FAILED" : "OK",
      errors);
  return errors ? 1 : 0;
}
