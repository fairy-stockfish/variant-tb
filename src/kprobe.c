/*
  Simple probing tool for Racing Kings tablebases (.ktbw/.ktbz).

  Usage: kprobe [-m] [FEN ...]
  Without FEN arguments, FENs are read from standard input (one per line).
  For each position the WDL value (-2..2, from the point of view of the
  side to move) and the DTZ value are printed. With -m, the WDL and DTZ
  values after each legal move are printed as well.

  Tables are looked up in the directories listed in $KTBPATH.

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "decompress.h"
#include "defs.h"
#include "threads.h"
#include "util.h"

static int white_king, black_king;

#include "probe.c"
#include "board.c"

int numpcs;
int numpawns = 0;

struct pos {
  int pieces[TBPIECES];
  int sq[TBPIECES];
  int num;
  int wtm;
};

static int parse_fen(const char *fen, struct pos *P)
{
  int rank = 7, file = 0;
  const char *s = fen;

  P->num = 0;
  for (; *s && *s != ' '; s++) {
    char c = *s;
    if (c == '/') {
      rank--;
      file = 0;
      continue;
    }
    if (c >= '1' && c <= '8') {
      file += c - '0';
      continue;
    }
    int t;
    switch (c | 0x20) {
    case 'k': t = KING; break;
    case 'q': t = QUEEN; break;
    case 'r': t = ROOK; break;
    case 'b': t = BISHOP; break;
    case 'n': t = KNIGHT; break;
    default: return 0;
    }
    if (!(c & 0x20)) t |= 0; else t |= 0x08;
    if (P->num == TBPIECES || rank < 0 || file > 7) return 0;
    P->pieces[P->num] = t;
    P->sq[P->num] = 8 * rank + file;
    P->num++;
    file++;
  }
  while (*s == ' ') s++;
  P->wtm = (*s != 'b');
  return 1;
}

static void setup_globals(struct pos *P, int *pieces, int *gpos, bitboard *occ)
{
  numpcs = P->num;
  *occ = 0;
  for (int i = 0; i < P->num; i++) {
    pieces[i] = P->pieces[i];
    gpos[i] = P->sq[i];
    *occ |= bit[P->sq[i]];
    if (pieces[i] == WKING) white_king = i;
    if (pieces[i] == BKING) black_king = i;
  }
}

static int count_kings(struct pos *P)
{
  int w = 0, b = 0;
  for (int i = 0; i < P->num; i++) {
    if (P->pieces[i] == WKING) w++;
    if (P->pieces[i] == BKING) b++;
  }
  return w == 1 && b == 1;
}

static int is_attacked_by(struct pos *P, int sq, int color)
{
  bitboard occ = 0;
  for (int i = 0; i < P->num; i++)
    occ |= bit[P->sq[i]];
  for (int i = 0; i < P->num; i++) {
    if ((P->pieces[i] & 0x08) != color) continue;
    if (PieceRange(P->sq[i], P->pieces[i], occ) & bit[sq]) return 1;
  }
  return 0;
}

static int king_sq(struct pos *P, int color)
{
  for (int i = 0; i < P->num; i++)
    if (P->pieces[i] == (KING | color)) return P->sq[i];
  return -1;
}

static int legal_position(struct pos *P)
{
  return !is_attacked_by(P, king_sq(P, 0), 8)
         && !is_attacked_by(P, king_sq(P, 8), 0);
}

// generate all legal moves; returns the number of moves
static int gen_moves(struct pos *P, struct pos *children, int *capture)
{
  int n = 0;
  int color = P->wtm ? 0 : 8;
  bitboard occ = 0, own = 0;

  for (int i = 0; i < P->num; i++) {
    occ |= bit[P->sq[i]];
    if ((P->pieces[i] & 0x08) == color) own |= bit[P->sq[i]];
  }

  for (int i = 0; i < P->num; i++) {
    if ((P->pieces[i] & 0x08) != color) continue;
    bitboard bb = PieceRange(P->sq[i], P->pieces[i], occ) & ~own;
    for (; bb; ClearFirst(bb)) {
      int to = FirstOne(bb);
      struct pos C = *P;
      int capt = 0;
      C.sq[i] = to;
      for (int j = 0; j < C.num; j++)
        if (j != i && C.sq[j] == to) {
          if ((C.pieces[j] & 7) == KING) goto skip;
          C.pieces[j] = C.pieces[C.num - 1];
          C.sq[j] = C.sq[C.num - 1];
          C.num--;
          capt = 1;
          break;
        }
      C.wtm = !P->wtm;
      if (!legal_position(&C)) continue;
      capture[n] = capt;
      children[n++] = C;
skip:;
    }
  }

  return n;
}

static int is_terminal(struct pos *P, int *v)
{
  int pieces[TBPIECES], gpos[TBPIECES];
  bitboard occ;

  setup_globals(P, pieces, gpos, &occ);
  return rk_terminal(pieces, gpos, P->wtm, occ, v);
}

static int probe_wdl(struct pos *P)
{
  int pieces[TBPIECES], gpos[TBPIECES];
  bitboard occ;

  setup_globals(P, pieces, gpos, &occ);
  return probe_tb(pieces, gpos, P->wtm, occ, -2, 2);
}

static void table_name(struct pos *P, char *str)
{
  static const char pc[] = "KQRBN";
  static const int ord[] = { KING, QUEEN, ROOK, BISHOP, KNIGHT };
  char *s = str;

  for (int c = 0; c < 2; c++) {
    if (c) *s++ = 'v';
    for (int k = 0; k < 5; k++)
      for (int i = 0; i < P->num; i++)
        if (P->pieces[i] == (ord[k] | (c ? 8 : 0)))
          *s++ = pc[k];
  }
  *s = 0;
}

struct dtz_table {
  char name[16];
  struct PairsData *d;
  struct TBEntry_piece entry;
  int side;
  int flags;
  uint8_t (*map)[256];
  uint16_t (*map16)[MAX_VALS];
  struct dtz_table *next;
};

static struct dtz_table *dtz_tables = NULL;

// Decode a single value of a compressed DTZ table. Same as decompress_pairs()
// in probe.c, but without the lookup tables, which decomp_setup_pairs() does
// not build.
static uint8_t dtz_decompress(struct PairsData *d, uint64_t idx)
{
  if (!d->idxbits)
    return d->min_len;

  uint32_t mainidx = idx >> d->idxbits;
  int litidx = (idx & ((1 << d->idxbits) - 1)) - (1 << (d->idxbits - 1));
  uint32_t block;
  memcpy(&block, d->indextable + 6 * mainidx, sizeof(block));
  litidx += *(uint16_t *)(d->indextable + 6 * mainidx + 4);
  if (litidx < 0) {
    do {
      litidx += d->sizetable[--block] + 1;
    } while (litidx < 0);
  } else {
    while (litidx > d->sizetable[block])
      litidx -= d->sizetable[block++] + 1;
  }

  uint32_t *ptr = (uint32_t *)(d->data + ((uint64_t)block << d->blocksize));

  int m = d->min_len;
  uint16_t *offset = d->offset;
  uint64_t *base = d->base - m;
  uint8_t *symlen = d->symlen;
  int sym, bitcnt, l;

  uint64_t code = __builtin_bswap64(*((uint64_t *)ptr));
  ptr += 2;
  bitcnt = 0; // number of "empty bits" in code
  for (;;) {
    l = m;
    while (code < base[l]) l++;
    sym = offset[l] + ((code - base[l]) >> (64 - l));
    if (litidx < (int)symlen[sym] + 1) break;
    litidx -= (int)symlen[sym] + 1;
    code <<= l;
    bitcnt += l;
    if (bitcnt >= 32) {
      bitcnt -= 32;
      code |= (uint64_t)(__builtin_bswap32(*ptr++)) << bitcnt;
    }
  }

  uint8_t *sympat = d->sympat;
  while (symlen[sym] != 0) {
    uint8_t *w = sympat + 3 * sym;
    int s1 = ((w[1] & 0xf) << 8) | w[0];
    if (litidx < (int)symlen[s1] + 1)
      sym = s1;
    else {
      litidx -= (int)symlen[s1] + 1;
      sym = (w[2] << 4) | (w[1] >> 4);
    }
  }

  return *(sympat + 3 * sym);
}

static struct dtz_table *get_dtz_table(struct pos *P)
{
  char name[16];
  struct dtz_table *T;

  table_name(P, name);
  for (T = dtz_tables; T; T = T->next)
    if (strcmp(T->name, name) == 0)
      return T;

  char path[512];
  const char *dir = getenv(TBPATH);
  if (!dir || !*dir) dir = ".";
  int found = 0;
  while (*dir && !found) {
    const char *end = strchr(dir, SEP_CHAR);
    int len = end ? end - dir : (int)strlen(dir);
    snprintf(path, sizeof(path), "%.*s/%s%s", len, dir, name, DTZSUFFIX);
    FILE *F = fopen(path, "rb");
    if (F) {
      fclose(F);
      snprintf(path, sizeof(path), "%.*s/%s", len, dir, name);
      found = 1;
    }
    dir = end ? end + 1 : dir + len;
  }
  if (!found) {
    fprintf(stderr, "Missing table %s%s\n", name, DTZSUFFIX);
    exit(EXIT_FAILURE);
  }

  int pcs[16];
  for (int i = 0; i < 16; i++)
    pcs[i] = 0;
  for (int i = 0; i < P->num; i++)
    pcs[P->pieces[i]]++;

  numpcs = P->num;
  decomp_init_piece(pcs);
  struct tb_handle *H = open_tb_handle(path, 0);
  decomp_init_table(H);
  fclose(H->F);

  // The table stays compressed in the memory-mapped file; values are
  // decoded on demand, so probing many tables does not need much RAM.
  T = malloc(sizeof(*T));
  strcpy(T->name, name);
  T->d = H->entry_piece.precomp[0];
  T->d->data = H->data + H->file[0].idx[0];
  T->entry = H->entry_piece;
  T->side = get_dtz_side(H, 0);
  T->flags = H->dtz_flags[0];
  T->map = get_dtz_map(H, 0);
  T->map16 = get_dtz_map16(H, 0);
  T->next = dtz_tables;
  dtz_tables = T;

  return T;
}

static int dtz_before_zeroing(int wdl)
{
  return wdl == 2 ? 1 : wdl == 1 ? 101 : wdl == -1 ? -101 : wdl == -2 ? -1 : 0;
}

// Returns the DTZ value of a position in which the game is not over.
// Same conventions as the usual Syzygy probing code.
static int probe_dtz(struct pos *P)
{
  int v;

  if (is_terminal(P, &v))
    return 0;

  int wdl = probe_wdl(P);
  if (wdl == 0)
    return 0;

  // is a capture the best move?
  struct pos children[256];
  int capture[256];
  int num = gen_moves(P, children, capture);
  int best = -3, num_capt = 0;
  for (int i = 0; i < num; i++)
    if (capture[i]) {
      num_capt++;
      v = -probe_wdl(&children[i]);
      if (v > best) best = v;
    }
  if (best >= wdl && (best > 0 || num_capt == num))
    return dtz_before_zeroing(wdl);

  if (P->num == 2) {
    int pieces[TBPIECES], gpos[TBPIECES];
    bitboard occ;
    setup_globals(P, pieces, gpos, &occ);
    return probe_kvk_dtz(pieces, gpos, P->wtm);
  }

  struct dtz_table *T = get_dtz_table(P);
  if (T->side == (P->wtm ? 0 : 1)) {
    int pos[TBPIECES];
    uint8_t *pc = T->entry.pieces[0];
    for (int i = 0, j = 0; i < P->num; i++) {
      for (; P->pieces[j] != pc[i]; j++);
      pos[i] = P->sq[j++];
      if (i + 1 < P->num && pc[i] != pc[i + 1]) j = 0;
    }
    uint64_t idx = encode_piece(&T->entry, T->entry.norm[0], pos,
                                T->entry.factor[0]);
    int value = dtz_decompress(T->d, idx);
    static const int wdl_map[] = { 1, 3, 0, 2, 0 };
    if (T->flags & 2) {
      if (T->map)
        value = T->map[wdl_map[wdl + 2]][value];
      else
        value = T->map16[wdl_map[wdl + 2]][value];
    }
    if ((wdl == 2 && !(T->flags & 4)) || (wdl == -2 && !(T->flags & 8))
        || wdl == 1 || wdl == -1)
      value *= 2;
    value++;
    if (wdl == 1 || wdl == -1)
      value += 100;
    return wdl > 0 ? value : -value;
  }

  // the table stores the other side: one-ply search
  int min_dtz = 0xffff;
  for (int i = 0; i < num; i++) {
    int dtz;
    if (capture[i])
      dtz = -dtz_before_zeroing(probe_wdl(&children[i]));
    else if (is_terminal(&children[i], &v)) {
      // the race is decided: a win counts like a mate
      if (v < 0) {
        min_dtz = 1;
        continue;
      }
      dtz = 0;
    } else {
      dtz = -probe_dtz(&children[i]);
      if (dtz) dtz += dtz > 0 ? 1 : -1;
    }
    if (dtz < min_dtz && dtz != 0 && (dtz > 0) == (wdl > 0))
      min_dtz = dtz;
  }

  return min_dtz == 0xffff ? -1 : min_dtz;
}

static void move_str(struct pos *P, struct pos *C, char *str)
{
  int from = -1, to = -1;
  for (int i = 0; i < P->num; i++) {
    int j;
    for (j = 0; j < C->num; j++)
      if (C->pieces[j] == P->pieces[i] && C->sq[j] == P->sq[i]) break;
    if (j == C->num && (P->pieces[i] & 0x08) == (P->wtm ? 0 : 8))
      from = P->sq[i];
  }
  for (int j = 0; j < C->num; j++) {
    int i;
    for (i = 0; i < P->num; i++)
      if (C->pieces[j] == P->pieces[i] && C->sq[j] == P->sq[i]) break;
    if (i == P->num)
      to = C->sq[j];
  }
  sprintf(str, "%c%c%c%c", 'a' + (from & 7), '1' + (from >> 3),
          'a' + (to & 7), '1' + (to >> 3));
}

static void probe_fen(const char *fen, int moves)
{
  struct pos P;

  if (!parse_fen(fen, &P) || !count_kings(&P) || !legal_position(&P)) {
    printf("%s: illegal\n", fen);
    return;
  }

  int v;
  if (is_terminal(&P, &v)) {
    printf("%s: wdl %d dtz 0 (game over)\n", fen, v);
    return;
  }

  printf("%s: wdl %d dtz %d\n", fen, probe_wdl(&P), probe_dtz(&P));

  if (moves) {
    struct pos children[256];
    int capture[256];
    int num = gen_moves(&P, children, capture);
    for (int i = 0; i < num; i++) {
      char str[8];
      move_str(&P, &children[i], str);
      if (is_terminal(&children[i], &v))
        printf("  %s: wdl %d dtz 0 (game over)\n", str, v);
      else
        printf("  %s: wdl %d dtz %d\n", str, probe_wdl(&children[i]),
               probe_dtz(&children[i]));
    }
  }
}

int main(int argc, char **argv)
{
  int moves = 0;
  int val;

  while ((val = getopt(argc, argv, "m")) != -1)
    if (val == 'm')
      moves = 1;

  numthreads = 1;
  total_work = 1;
  init_threads(0);
  set_up_tables();
  init_tablebases();

  if (optind < argc) {
    for (; optind < argc; optind++)
      probe_fen(argv[optind], moves);
  } else {
    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
      line[strcspn(line, "\r\n")] = 0;
      if (*line)
        probe_fen(line, moves);
      fflush(stdout);
    }
  }

  return 0;
}
