/*
  Copyright (c) 2011-2016, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef PROBE_H
#define PROBE_H

#include "defs.h"
#include "util.h"
#include "types.h"

#define TBPIECES 7

#if defined(SUICIDE)
#if !defined(GIVEAWAY)
#define WDLSUFFIX ".stbw"
#define DTZSUFFIX ".stbz"
#define TBPATH "STBPATH"
#define STATSDIR "STBSTATSDIR"
#define LOGFILE "stblog.txt"
#else
#define WDLSUFFIX ".gtbw"
#define DTZSUFFIX ".gtbz"
#define TBPATH "GTBPATH"
#define STATSDIR "GTBSTATSDIR"
#define LOGFILE "gtblog.txt"
#endif
#elif defined(LOSER)
#define WDLSUFFIX ".ltbw"
#define DTZSUFFIX ".ltbz"
#define TBPATH "LTBPATH"
#define STATSDIR "LTBSTATSDIR"
#define LOGFILE "ltblog.txt"
#elif defined(GIVEAWAY)
#define WDLSUFFIX ".gtbw"
#define DTZSUFFIX ".gtbz"
#define TBPATH "GTBPATH"
#define STATSDIR "GTBSTATSDIR"
#define LOGFILE "gtblog.txt"
#elif defined(ATOMIC)
#define WDLSUFFIX ".atbw"
#define DTZSUFFIX ".atbz"
#define TBPATH "ATBPATH"
#define STATSDIR "ATBSTATSDIR"
#define LOGFILE "atblog.txt"
#elif defined(ASEAN)
#define WDLSUFFIX ".asw"
#define DTZSUFFIX ".asz"
#define TBPATH "ASTBPATH"
#define STATSDIR "ASTBSTATSDIR"
#define LOGFILE "astblog.txt"
#elif defined(CAMBODIAN)
#define WDLSUFFIX ".cmw"
#define DTZSUFFIX ".cmz"
#define TBPATH "CMTBPATH"
#define STATSDIR "CMTBSTATSDIR"
#define LOGFILE "cmtblog.txt"
#elif defined(MAKRUK)
#define WDLSUFFIX ".mkw"
#define DTZSUFFIX ".mkz"
#define TBPATH "MKTBPATH"
#define STATSDIR "MKTBSTATSDIR"
#define LOGFILE "mktblog.txt"
#elif defined(SHATRANJ)
#define WDLSUFFIX ".jtbw"
#define DTZSUFFIX ".jtbz"
#define TBPATH "JTBPATH"
#define STATSDIR "JTBSTATSDIR"
#define LOGFILE "jtblog.txt"
#else
#define WDLSUFFIX ".rtbw"
#define DTZSUFFIX ".rtbz"
#define TBPATH "RTBPATH"
#define STATSDIR "RTBSTATSDIR"
#define LOGFILE "rtblog.txt"
#endif

#define MAX_TBPIECES 8

#if defined(ASEAN)
#define WDL_MAGIC 0x6f1d2a93
#define DTZ_MAGIC 0x2c8e47b5
#elif defined(CAMBODIAN)
#define WDL_MAGIC 0x91b3c6d2
#define DTZ_MAGIC 0x4e7a0f18
#elif defined(REGULAR)
#define WDL_MAGIC 0x5d23e871
#define DTZ_MAGIC 0xa50c66d7
#elif defined(ATOMIC)
#define WDL_MAGIC 0x49a48d55
#define DTZ_MAGIC 0xeb5ea991
#elif defined(MAKRUK)
#define WDL_MAGIC 0x3ac9d5e1
#define DTZ_MAGIC 0x7b51e2c4
#elif defined(SHATRANJ)
#define WDL_MAGIC 0xb4e9b3b7
#define DTZ_MAGIC 0x87c126fc
#elif defined(SUICIDE) && !defined(GIVEAWAY)
#define WDL_MAGIC 0x1593f67b
#define DTZ_MAGIC 0x23e7cfe4
#define OTHER_MAGIC 0x21bc55bc
#elif defined(GIVEAWAY)
#define WDL_MAGIC 0x21bc55bc
#define DTZ_MAGIC 0x501bf5d6
#define OTHER_MAGIC 0x1593f67b
#endif

#ifdef SUICIDE
#define TBHASHBITS 12
#else
#define TBHASHBITS 10
#endif

struct TBHashEntry;

struct PairsData {
  char *indextable;
  uint16_t *sizetable;
  uint8_t *data;
  uint16_t *offset;
  uint8_t *symlen;
  uint8_t *sympat;
#ifdef LOOKUP
  uint16_t *lookup_len;
  uint8_t *lookup_bits;
#endif
  int blocksize;
  int idxbits;
  int min_len;
  int max_len; // to allow checking max_len with rtbver/rtbverp
  uint64_t base[];
};

struct TBEntry {
  uint8_t *data;
  uint32_t key;
  uint8_t ready;
  uint8_t num;
  uint8_t symmetric;
  uint8_t has_pawns;
} __attribute__((__may_alias__));

struct TBEntry_piece {
  uint8_t *data;
  uint32_t key;
  uint8_t ready;
  uint8_t num;
  uint8_t symmetric;
  uint8_t has_pawns;
  uint8_t enc_type;
  struct PairsData *precomp[2];
  uint64_t factor[2][TBPIECES];
  uint8_t pieces[2][TBPIECES];
  uint8_t norm[2][TBPIECES];
  uint8_t order[2];
#ifdef MAKRUK
  char name[2 * TBPIECES + 4];
  uint8_t dtz_ready;
  uint8_t dtz_flags[2];
  uint8_t dtz_side;         // side to move stored in the DTZ file
  uint8_t dtz_pieces[2][TBPIECES];
  struct PairsData *dtz_precomp[2];
  uint8_t *dtz_map[2][2];
#endif
};

struct TBEntry_pawn {
  uint8_t *data;
  uint32_t key;
  uint8_t ready;
  uint8_t num;
  uint8_t symmetric;
  uint8_t has_pawns;
  uint8_t pawns[2];
  struct {
    struct PairsData *precomp[2];
    uint64_t factor[2][TBPIECES];
    uint8_t pieces[2][TBPIECES];
    uint8_t norm[2][TBPIECES];
    uint8_t order[2];
    uint8_t order2[2];
  } file[4];
};

struct TBHashEntry {
  uint64_t key;
  struct TBEntry *ptr;
};

int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta);

#ifdef MAKRUK
// Raw index of Makruk pawnless tables: white king mirrored to files a-d,
// 11 bits for both kings, 6 bits for each other piece.
#define MK_RAW_SIZE(n) (2048ULL << (6 * ((n) - 2)))

static inline uint64_t mk_raw_index(int *pos, int n, int flip)
{
  int mirror = ((pos[0] ^ flip) & 0x04) ? 0x07 : 0;
  int wk = pos[0] ^ flip ^ mirror;
  uint64_t idx = ((wk >> 3) << 2) | (wk & 0x03);
  for (int i = 1; i < n; i++)
    idx = (idx << 6) | (pos[i] ^ flip ^ mirror);
  return idx;
}

// Tables without khon (piece type 3) and pawns also use the symmetries that flip
// the board vertically and along the a1-h8 diagonal. Only canonical
// positions are stored (the others are don't cares):
// - all tables: identical pieces have ascending squares;
// - tables without khon: the white king is in the triangle a1-d1-d4, and
//   if it is on the diagonal, the position is not larger (as a sequence of
//   squares in index order) than its reflection in the diagonal.
#define MK_KHON 3
#define MK_TRANSPOSE(sq) ((((sq) & 0x07) << 3) | ((sq) >> 3))

static inline int mk_sym8(const uint8_t *pc, int n)
{
  for (int i = 0; i < n; i++)
    if ((pc[i] & 0x07) == MK_KHON || (pc[i] & 0x07) == 1) return 0; // pawn
  return 1;
}

static inline void mk_sort_groups(int *q, const uint8_t *pc, int n)
{
  for (int i = 3; i < n; i++)
    for (int j = i; j > 2 && pc[j - 1] == pc[j] && q[j - 1] > q[j]; j--) {
      int t = q[j]; q[j] = q[j - 1]; q[j - 1] = t;
    }
}

static inline int mk_less(const int *a, const int *b, int n)
{
  for (int i = 1; i < n; i++)
    if (a[i] != b[i]) return a[i] < b[i];
  return 0;
}

// is the position q (squares in index order, white king on files a-d)
// canonical?
static inline int mk_is_canonical(const int *q, const uint8_t *pc, int n,
    int sym8)
{
  for (int i = 3; i < n; i++)
    if (pc[i - 1] == pc[i] && q[i - 1] > q[i]) return 0;
  if (!sym8) return 1;
  int f = q[0] & 0x07, r = q[0] >> 3;
  if (r > f) return 0;
  if (r < f) return 1;
  int t[TBPIECES];
  for (int i = 0; i < n; i++)
    t[i] = MK_TRANSPOSE(q[i]);
  mk_sort_groups(t, pc, n);
  return !mk_less(t, q, n);
}

// index of a position for probing: canonicalize, then compute the raw index
static inline uint64_t mk_probe_index(const int *pos, const uint8_t *pc,
    int n, int flip)
{
  int q[TBPIECES];
  int sym8 = mk_sym8(pc, n);
  int m = ((pos[0] ^ flip) & 0x04) ? 0x07 : 0;
  if (sym8 && ((pos[0] ^ flip) & 0x20)) m |= 0x38;
  for (int i = 0; i < n; i++)
    q[i] = pos[i] ^ flip ^ m;
  if (sym8 && (q[0] >> 3) > (q[0] & 0x07))
    for (int i = 0; i < n; i++)
      q[i] = MK_TRANSPOSE(q[i]);
  mk_sort_groups(q, pc, n);
  if (sym8 && (q[0] >> 3) == (q[0] & 0x07)) {
    int t[TBPIECES];
    for (int i = 0; i < n; i++)
      t[i] = MK_TRANSPOSE(q[i]);
    mk_sort_groups(t, pc, n);
    if (mk_less(t, q, n))
      for (int i = 0; i < n; i++) q[i] = t[i];
  }
  return mk_raw_index(q, n, 0);
}

int probe_dtz_makruk(int *pieces, int *gpos, int wtm);
#endif

uint64_t encode_piece(struct TBEntry_piece *ptr, uint8_t *norm, int *pos,
    uint64_t *factor);
void decode_piece(struct TBEntry_piece *ptr, uint8_t *norm, int *pos,
    uint64_t *factor, int *order, uint64_t idx);
uint64_t encode_pawn(struct TBEntry_pawn *ptr, uint8_t *norm, int *pos,
    uint64_t *factor);
void decode_pawn(struct TBEntry_pawn *ptr, uint8_t *norm, int *pos,
    uint64_t *factor, int *order, uint64_t idx, int file);

void set_norm_piece(struct TBEntry_piece *ptr, uint8_t *norm, uint8_t *pieces,
    int order);
void set_norm_pawn(struct TBEntry_pawn *ptr, uint8_t *norm, uint8_t *pieces,
    int order, int order2);
uint64_t calc_factors_piece(uint64_t *factor, int num, int order,
    uint8_t *norm, uint8_t enc_type);
uint64_t calc_factors_pawn(uint64_t *factor, int num, int order, int order2,
    uint8_t *norm, int file);

void calc_order_piece(int num, int ord, int *order, uint8_t *norm);
void calc_order_pawn(int num, int ord, int ord2, int *order, uint8_t *norm);

#endif
