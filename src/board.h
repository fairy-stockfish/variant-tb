/*
  Copyright (c) 2011-2013, 2024 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef BOARD_H
#define BOARD_H

#include "defs.h"
#include "types.h"

extern bitboard bit[64];

static __inline__ int FirstOne(bitboard x)
{
  return __builtin_ctzll(x);
}

#ifdef USE_POPCNT
static __inline__ int PopCount(bitboard x)
{
  return __builtin_popcountll(x);
}
#endif

#define ClearFirst(x) ((x)&=(x)-1)

#include "magic.h"
#include "hyper.h"
#include "bmi2.h"

extern bitboard knight_range[64], king_range[64];
#ifdef SHATRANJ
extern bitboard queen_range[64], bishop_range[64];
#endif
#ifdef HAS_PAWNS
extern bitboard pawn_range[2][64];
#define white_pawn_range pawn_range[0]
#define black_pawn_range pawn_range[1]
extern bitboard sides_mask[64];
extern bitboard double_push_mask[16];
#endif

#ifdef ATOMIC
extern bitboard atom_mask[64];
#endif

#define KnightRange(x) knight_range[x]
#define KingRange(x) king_range[x]
#ifdef SHATRANJ
#undef QueenRange
#define QueenRange(x,occ) queen_range[x]
#undef BishopRange
#define BishopRange(x,occ) bishop_range[x]
#endif

// The piece types and codes are defined in types.h.

#ifdef SCHESS
#define HawkRange(x,occ) (BishopRange(x,occ) | KnightRange(x))
#define ElephantRange(x,occ) (RookRange(x,occ) | KnightRange(x))
#define SCHESS_CASES \
  case HAWK: \
    return HawkRange(sq, occ); \
  case ELEPHANT: \
    return ElephantRange(sq, occ);
#else
#define SCHESS_CASES
#endif

static __inline__ bitboard PieceRange(int sq, int type, bitboard occ)
{
  switch (type & TYPE_MASK) {
#ifdef HAS_PAWNS
  case PAWN:
    return pawn_range[!!(type & BLACK_BIT)][sq];
#endif
  case KNIGHT:
    return KnightRange(sq);
  case BISHOP:
    return BishopRange(sq, occ);
  case ROOK:
    return RookRange(sq, occ);
  case QUEEN:
    return QueenRange(sq, occ);
  SCHESS_CASES
  default:
    return KingRange(sq);
  }
}

static __inline__ bitboard PieceMoves(int sq, int type, bitboard occ)
{
  return PieceRange(sq, type, occ) & ~occ;
}

// only used in rtbgenp / rtbverp
static __inline__ bitboard PieceRange1(int sq, int type, bitboard occ)
{
  switch (type & TYPE_MASK) {
  case KING:
    return KingRange(sq);
  case KNIGHT:
    return KnightRange(sq);
  case BISHOP:
    return BishopRange(sq, occ);
  case ROOK:
    return RookRange(sq, occ);
  case QUEEN:
    return QueenRange(sq, occ);
  SCHESS_CASES
  default:
    assume(0);
  }
}

static __inline__ bitboard PieceMoves1(int sq, int type, bitboard occ)
{
  return PieceRange1(sq, type, occ) & ~occ;
}

// only used in rtbgen / rtbver
static __inline__ bitboard PieceRange2(int sq, int type, bitboard occ)
{
  switch (type & TYPE_MASK) {
  case KNIGHT:
    return KnightRange(sq);
  case BISHOP:
    return BishopRange(sq, occ);
  case ROOK:
    return RookRange(sq, occ);
  case QUEEN:
    return QueenRange(sq, occ);
  SCHESS_CASES
  default:
    assume(0);
  }
}

static __inline__ bitboard PieceMoves2(int sq, int type, bitboard occ)
{
  return PieceRange2(sq, type, occ) & ~occ;
}

#endif

