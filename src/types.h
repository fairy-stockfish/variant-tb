#ifndef TYPES_H
#define TYPES_H

#include <inttypes.h>
#include "defs.h"

typedef uint64_t bitboard;
typedef uint16_t Move;

typedef uint8_t u8;
typedef uint16_t u16;

// A piece code is a piece type ORed with BLACK_BIT for black pieces.
// Piece code 0 means "no piece".
#ifndef SCHESS
enum { PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };

#define TYPE_MASK 0x07
#define BLACK_BIT 0x08
#else
// Seirawan chess adds the hawk (bishop + knight) and the elephant
// (rook + knight), so piece types need 4 bits.
enum { PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING, HAWK, ELEPHANT };

#define TYPE_MASK 0x0f
#define BLACK_BIT 0x10
#endif

// number of piece codes (size of arrays indexed by piece code)
#define NUM_PC (2 * BLACK_BIT)

enum {
  WPAWN = PAWN, WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING,
  BPAWN = PAWN | BLACK_BIT, BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING,
};

#ifdef SCHESS
enum { WHAWK = HAWK, WELEPHANT, BHAWK = HAWK | BLACK_BIT, BELEPHANT };
#endif

// Table files store each piece code in a nibble: the piece type in the low
// 3 bits and the colour in bit 3. In Seirawan chess the elephant (type 8)
// is stored with type bits 0.
#ifndef SCHESS
#define PC_TO_NIBBLE(pc) (pc)
#define NIBBLE_TO_PC(n) (n)
#else
#define PC_TO_NIBBLE(pc) (((pc) & 0x07) | (((pc) & BLACK_BIT) ? 0x08 : 0x00))
#define NIBBLE_TO_PC(n) \
  ((((n) & 0x07) ? ((n) & 0x07) : ELEPHANT) | (((n) & 0x08) ? BLACK_BIT : 0))
#endif

// Loop over the piece types a pawn of colour c (0 or BLACK_BIT) can
// promote to, from strong to weak.
#ifndef SCHESS
#define LAST_PROMO QUEEN
#define PREV_PROMO(m) ((m) - 1)
#else
#define LAST_PROMO ELEPHANT
#define PREV_PROMO(m) (((m) & TYPE_MASK) == HAWK ? (m) - 2 : (m) - 1)
#endif
#define PROMO_LOOP(m, c) \
  for (m = LAST_PROMO | (c); ((m) & TYPE_MASK) >= KNIGHT; m = PREV_PROMO(m))

// Sort order of the pieces in the generator (kings first, then pawns and
// pieces from weak to strong, white before black).
#ifndef SCHESS
#define PIECE_ORDER_INIT { \
    0, 0, 3, 5, 7, 9, 1, 0, \
    0, 0, 4, 6, 8, 10, 2, 0 \
  }
#else
#define PIECE_ORDER_INIT { \
    0, 0, 3, 5, 7, 9, 1, 11, 13, 0, 0, 0, 0, 0, 0, 0, \
    0, 0, 4, 6, 8, 10, 2, 12, 14, 0, 0, 0, 0, 0, 0, 0 \
  }
#endif

struct dtz_map {
  uint16_t map[4][MAX_VALS];
  uint16_t inv_map[4][MAX_VALS];
  uint16_t num[4];
  uint16_t max_num;
  uint8_t side;
  uint8_t ply_accurate_win;
  uint8_t ply_accurate_loss;
  uint8_t wide;
  uint8_t high_freq_max;
};

#endif
