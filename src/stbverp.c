/*
  Copyright (c) 2011-2013, 2018 Ronald de Man
  Copyright (c) 2026 Fabian Fichter

  This file is distributed under the terms of the GNU GPL, version 2.
*/

// Verifier for suicide and giveaway tables with pawns (included by
// tbverp.c). See stbver.c for the treatment of captures and threats.
// In addition to that:
//
// - Pawn moves are zeroing moves. As in rtbverp.c, they are evaluated with
//   the (loaded) WDL values of the positions they lead to or by probing
//   the subtables (promotions, also to a king), and folded into the WDL
//   class of the position before the DTZ values are checked.
// - After a double pawn push, an en passant capture is compulsory if it
//   is the only capture.
// - A player without moves is stalemated: under FICS rules (suicide) the
//   player with fewer pieces wins, under international rules (giveaway)
//   the stalemated player wins. Stalemate positions have DTZ 0.

int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta);

// Values after calculating captures and threats. Higher is better for the
// side to move.
#define A_NONE 0
#define A_CAPT_LOSS 1
#define A_CAPT_CLOSS 2
#define A_CAPT_DRAW 3
#define A_CAPT_CWIN 4
#define A_CAPT_WIN 5
#define A_THR_LOSS 6
#define A_THR_CLOSS 7
#define A_THR_DRAW 8
#define A_THR_CWIN 9
#define A_THR_WIN 10
#define A_BROKEN WDL_BROKEN

// WDL classes. The first eight classes require a DTZ value.
#define WDL_LOSS 0
#define WDL_CLOSS 1
#define WDL_CWIN 2
#define WDL_WIN 3
#define WDL_PLOSS 4     // loss, a pawn move loses as well
#define WDL_PCLOSS 5    // cursed loss, a pawn move leads to a cursed loss
#define WDL_SWIN 6      // won by stalemate
#define WDL_SLOSS 7     // lost by stalemate
#define WDL_ERROR 8
#define WDL_DRAW 9
#define WDL_THR_CWIN 10
#define WDL_THR_WIN 11
#define WDL_THR_WIN1 12 // threat win and a move stalemating the opponent
#define WDL_PDRAW 13    // draw by a pawn move
#define WDL_PCWIN 14    // cursed win by a pawn move
#define WDL_PWIN 15     // win by a pawn move
#define WDL_CAPT_LOSS 16
#define WDL_CAPT_CLOSS 17
#define WDL_CAPT_DRAW 18
#define WDL_CAPT_CWIN 19
#define WDL_CAPT_WIN 20
#define WDL_BROKEN 21
#define WDL_NUM 22

#define IS_CAPT(v) ((v) >= A_CAPT_LOSS && (v) <= A_CAPT_WIN)

static uint8_t capt_threat[256];
static uint8_t wdl_matrix[A_THR_WIN + 1][5];

// WDL value of each class, -3 for ERROR and BROKEN
static const int8_t cls_val[WDL_NUM] = {
  -2, -1, 1, 2, -2, -1, 2, -2, -3, 0, 1, 2, 2, 0, 1, 2, -2, -1, 0, 1, 2, -3
};
#define CLS_IS_CAPT(c) ((c) >= WDL_CAPT_LOSS && (c) <= WDL_CAPT_WIN)

// index for the DTZ map: 0 = win, 1 = loss, 2 = cursed win, 3 = cursed loss
static int wdl_tbl_to_wdl[] = { 1, 3, 2, 0, 1, 3, 0, 1 };

// DTZ values. For the side to move, lower values are better, except that
// DTZ_SWIN is better than DTZ_CAPT_WIN (see dtz_key).
#define DTZ_NONE 0 // no move
#define DTZ_CAPT_WIN 1
#define DTZ_SWIN 2
#define DTZ_BASE_WIN 2
#define DTZ_CAPT_LOSS 252
#define DTZ_BASE_LOSS 252
#define DTZ_SLOSS 253
#define DTZ_ERROR 254
#define DTZ_BROKEN 255
static int dtz_capt_cwin, dtz_base_cwin, dtz_draw, dtz_capt_draw;
static int dtz_capt_closs, dtz_base_closs, max_dtz_cursed;
static int opp_capt_cwin, opp_base_cwin, opp_draw, opp_capt_draw;
static int opp_capt_closs, opp_base_closs;

static uint8_t wdl_to_dtz_c[WDL_NUM - WDL_ERROR];
static uint8_t opp_c[WDL_NUM - WDL_ERROR];
static uint8_t opp_compute[WDL_NUM];
static uint8_t wdl_to_dtz[8][256];
static uint8_t dtz_to_opp[WDL_NUM][256];
static uint8_t dtz_matrix[256][256];
static int dtz_key[256];

static void set_max(uint8_t *x, uint8_t v)
{
  uint8_t cur = __atomic_load_n(x, __ATOMIC_RELAXED);
  while (cur < v && !__atomic_compare_exchange_n(x, &cur, v, 1,
                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

// threats only apply to positions without captures
static void set_threat(uint8_t *x, uint8_t v)
{
  uint8_t cur = __atomic_load_n(x, __ATOMIC_RELAXED);
  while (cur < v && !IS_CAPT(cur)
         && !__atomic_compare_exchange_n(x, &cur, v, 1,
                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

void init_wdl_matrix(void)
{
}

void init_w_wdl_matrix(void)
{
}

void init_capt_threat(void)
{
  int i, j;

  for (i = 0; i < 256; i++)
    capt_threat[i] = 0;
  capt_threat[A_CAPT_WIN] = A_THR_LOSS;
  capt_threat[A_CAPT_CWIN] = A_THR_CLOSS;
  capt_threat[A_CAPT_DRAW] = A_THR_DRAW;
  capt_threat[A_CAPT_CLOSS] = A_THR_CWIN;
  capt_threat[A_CAPT_LOSS] = A_THR_WIN;

  // In tables with 6 or more pieces, the prober searches threats and
  // returns the maximum of the threat value and the stored value.
  int threat_dc = numpcs >= 6;

  static const uint8_t normal[5] = {
    WDL_LOSS, WDL_CLOSS, WDL_DRAW, WDL_CWIN, WDL_WIN
  };
  static const uint8_t threat[5] = {
    WDL_LOSS, WDL_CLOSS, WDL_DRAW, WDL_THR_CWIN, WDL_THR_WIN
  };

  for (i = 0; i <= A_THR_WIN; i++)
    for (j = 0; j < 5; j++) {
      if (IS_CAPT(i)) {
        wdl_matrix[i][j] = WDL_ERROR; // not used
        continue;
      }
      int t = i == A_NONE ? -1 : i - A_THR_LOSS;
      int v = j;
      if (threat_dc) {
        if (t > v) v = t;
      } else if (t > v) {
        wdl_matrix[i][j] = WDL_ERROR;
        continue;
      }
      wdl_matrix[i][j] = t == v ? threat[v] : normal[v];
    }
}

void init_wdl_dtz(void)
{
  int i, j;
  int win_num, loss_num;

  win_num = ply_accurate_win ? DRAW_RULE : (DRAW_RULE / 2);
  loss_num = ply_accurate_loss ? DRAW_RULE : (DRAW_RULE / 2);
  dtz_capt_cwin = DTZ_BASE_WIN + 1 + win_num;
  dtz_capt_closs = DTZ_BASE_LOSS - 1 - loss_num;
  opp_capt_cwin = DTZ_BASE_WIN + 1 + loss_num;
  opp_capt_closs = DTZ_BASE_LOSS - 1 - win_num;
  dtz_base_cwin = dtz_capt_cwin + 1;
  dtz_base_closs = dtz_capt_closs - 1;
  opp_base_cwin = opp_capt_cwin + 1;
  opp_base_closs = opp_capt_closs - 1;
  max_dtz_cursed = (dtz_base_closs - dtz_base_cwin - 1) / 2 - 1;
  dtz_draw = dtz_base_cwin + max_dtz_cursed + 1;
  dtz_capt_draw = dtz_draw + 1;
  opp_draw = opp_base_cwin + max_dtz_cursed + 1;
  opp_capt_draw = opp_draw + 1;

  // threat win: 2 plies
  int thr_win = DTZ_BASE_WIN + 1 + (ply_accurate_win ? 1 : 0);

  for (i = 0; i < 256; i++)
    dtz_key[i] = 4 * i;
  dtz_key[DTZ_SWIN] = 2; // between DTZ_NONE and DTZ_CAPT_WIN

  for (i = 0; i < WDL_NUM - WDL_ERROR; i++)
    wdl_to_dtz_c[i] = opp_c[i] = DTZ_ERROR;
  wdl_to_dtz_c[WDL_DRAW - WDL_ERROR] = dtz_draw;
  wdl_to_dtz_c[WDL_THR_CWIN - WDL_ERROR] = dtz_base_cwin;
  wdl_to_dtz_c[WDL_THR_WIN - WDL_ERROR] = thr_win;
  wdl_to_dtz_c[WDL_THR_WIN1 - WDL_ERROR] = DTZ_CAPT_WIN;
  wdl_to_dtz_c[WDL_PDRAW - WDL_ERROR] = dtz_capt_draw;
  wdl_to_dtz_c[WDL_PCWIN - WDL_ERROR] = dtz_capt_cwin;
  wdl_to_dtz_c[WDL_PWIN - WDL_ERROR] = DTZ_CAPT_WIN;
  wdl_to_dtz_c[WDL_CAPT_LOSS - WDL_ERROR] = DTZ_CAPT_LOSS;
  wdl_to_dtz_c[WDL_CAPT_CLOSS - WDL_ERROR] = dtz_capt_closs;
  wdl_to_dtz_c[WDL_CAPT_DRAW - WDL_ERROR] = dtz_capt_draw;
  wdl_to_dtz_c[WDL_CAPT_CWIN - WDL_ERROR] = dtz_capt_cwin;
  wdl_to_dtz_c[WDL_CAPT_WIN - WDL_ERROR] = DTZ_CAPT_WIN;
  wdl_to_dtz_c[WDL_BROKEN - WDL_ERROR] = DTZ_BROKEN;

  opp_c[WDL_THR_WIN1 - WDL_ERROR] = DTZ_CAPT_WIN;
  opp_c[WDL_CAPT_LOSS - WDL_ERROR] = DTZ_CAPT_LOSS;
  opp_c[WDL_CAPT_CLOSS - WDL_ERROR] = opp_capt_closs;
  opp_c[WDL_CAPT_DRAW - WDL_ERROR] = opp_capt_draw;
  opp_c[WDL_CAPT_CWIN - WDL_ERROR] = opp_capt_cwin;
  opp_c[WDL_CAPT_WIN - WDL_ERROR] = DTZ_CAPT_WIN;
  opp_c[WDL_BROKEN - WDL_ERROR] = DTZ_BROKEN;

  // opponent classes whose value is computed from the moves into the
  // DTZ table (stalemates are handled separately)
  for (i = 0; i < WDL_NUM; i++)
    opp_compute[i] = 0;
  opp_compute[WDL_LOSS] = opp_compute[WDL_CLOSS] = 1;
  opp_compute[WDL_CWIN] = opp_compute[WDL_WIN] = 1;
  opp_compute[WDL_PLOSS] = opp_compute[WDL_PCLOSS] = 1;
  opp_compute[WDL_DRAW] = opp_compute[WDL_THR_CWIN] = 1;
  opp_compute[WDL_THR_WIN] = opp_compute[WDL_PDRAW] = 1;
  opp_compute[WDL_PCWIN] = opp_compute[WDL_PWIN] = 1;

  for (i = 0; i < 8; i++)
    for (j = 0; j < 256; j++)
      wdl_to_dtz[i][j] = DTZ_ERROR;

  for (i = 0; i < win_num; i++)
    wdl_to_dtz[WDL_WIN][i] = DTZ_BASE_WIN + 1 + i;
  for (i = 0; i < loss_num; i++)
    wdl_to_dtz[WDL_LOSS][i] = DTZ_BASE_LOSS - 1 - i;
  wdl_to_dtz[WDL_PLOSS][0] = DTZ_CAPT_LOSS;
  for (i = 1; i < loss_num; i++)
    wdl_to_dtz[WDL_PLOSS][i] = DTZ_BASE_LOSS - 1 - i;
  for (i = 0; i < 256; i++) {
    int c = i < max_dtz_cursed ? i : max_dtz_cursed;
    wdl_to_dtz[WDL_CWIN][i] = dtz_base_cwin + c;
    wdl_to_dtz[WDL_CLOSS][i] = dtz_base_closs - c;
    wdl_to_dtz[WDL_PCLOSS][i] = dtz_base_closs - c;
  }
  wdl_to_dtz[WDL_PCLOSS][0] = dtz_capt_closs;
  wdl_to_dtz[WDL_SWIN][0] = DTZ_SWIN;
  wdl_to_dtz[WDL_SLOSS][0] = DTZ_SLOSS;

  // Opponent values, derived from the best move into the DTZ table.
  for (i = 0; i < WDL_NUM; i++)
    for (j = 0; j < 256; j++)
      dtz_to_opp[i][j] = DTZ_ERROR;

  // WDL_DRAW (including stalemates)
  dtz_to_opp[WDL_DRAW][DTZ_NONE] = opp_draw;
  dtz_to_opp[WDL_DRAW][dtz_draw] = opp_draw;
  dtz_to_opp[WDL_DRAW][dtz_capt_draw] = opp_draw;

  // WDL_PDRAW
  dtz_to_opp[WDL_PDRAW][DTZ_NONE] = opp_capt_draw;
  for (i = DTZ_CAPT_WIN; i <= dtz_capt_draw; i++)
    dtz_to_opp[WDL_PDRAW][i] = opp_capt_draw;

  // WDL_WIN
  dtz_to_opp[WDL_WIN][DTZ_SLOSS] = DTZ_CAPT_WIN;
  if (ply_accurate_loss) {
    dtz_to_opp[WDL_WIN][DTZ_CAPT_LOSS] = DTZ_BASE_WIN + 2;
    for (i = 0; i < loss_num - 1; i++)
      dtz_to_opp[WDL_WIN][DTZ_BASE_LOSS - i - 1] = DTZ_BASE_WIN + i + 2;
  } else {
    dtz_to_opp[WDL_WIN][DTZ_CAPT_LOSS] = DTZ_BASE_WIN + 1;
    for (i = 0; i < loss_num; i++)
      dtz_to_opp[WDL_WIN][DTZ_BASE_LOSS - i - 1] = DTZ_BASE_WIN + i + 1;
  }

  // WDL_PWIN
  for (i = DTZ_NONE; i <= DTZ_SLOSS; i++)
    dtz_to_opp[WDL_PWIN][i] = DTZ_CAPT_WIN;

  // WDL_CWIN
  if (ply_accurate_loss)
    dtz_to_opp[WDL_CWIN][DTZ_BASE_LOSS - loss_num] = opp_base_cwin;
  dtz_to_opp[WDL_CWIN][dtz_capt_closs] = opp_base_cwin;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_CWIN][dtz_base_closs - i] = opp_base_cwin + i;

  // WDL_PCWIN
  dtz_to_opp[WDL_PCWIN][DTZ_NONE] = opp_capt_cwin;
  if (ply_accurate_loss)
    for (i = DTZ_CAPT_WIN; i <= DTZ_BASE_LOSS - loss_num; i++)
      dtz_to_opp[WDL_PCWIN][i] = opp_capt_cwin;
  else
    for (i = DTZ_CAPT_WIN; i <= dtz_capt_closs; i++)
      dtz_to_opp[WDL_PCWIN][i] = opp_capt_cwin;

  // WDL_LOSS and WDL_PLOSS
  dtz_to_opp[WDL_LOSS][DTZ_SWIN] = DTZ_CAPT_LOSS;
  if (ply_accurate_win) {
    dtz_to_opp[WDL_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 2;
    for (i = 0; i < win_num - 1; i++)
      dtz_to_opp[WDL_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 2;
  } else {
    dtz_to_opp[WDL_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 1;
    for (i = 0; i < win_num; i++)
      dtz_to_opp[WDL_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 1;
  }
  for (j = 0; j < 256; j++)
    dtz_to_opp[WDL_PLOSS][j] = dtz_to_opp[WDL_LOSS][j];
  dtz_to_opp[WDL_PLOSS][DTZ_NONE] = DTZ_CAPT_LOSS;

  // WDL_CLOSS
  if (ply_accurate_win)
    dtz_to_opp[WDL_CLOSS][DTZ_BASE_WIN + win_num] = opp_base_closs;
  dtz_to_opp[WDL_CLOSS][dtz_capt_cwin] = opp_base_closs;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_CLOSS][dtz_base_cwin + i] = opp_base_closs - i;

  // WDL_PCLOSS
  dtz_to_opp[WDL_PCLOSS][DTZ_NONE] = opp_capt_closs;
  for (i = DTZ_CAPT_WIN; i < dtz_capt_cwin; i++)
    dtz_to_opp[WDL_PCLOSS][i] = opp_capt_closs;
  dtz_to_opp[WDL_PCLOSS][dtz_capt_cwin] = opp_base_closs;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_PCLOSS][dtz_base_cwin + i] = opp_base_closs - i;

  for (j = 0; j < 256; j++) {
    dtz_to_opp[WDL_THR_WIN][j] = dtz_to_opp[WDL_WIN][j];
    dtz_to_opp[WDL_THR_CWIN][j] = dtz_to_opp[WDL_CWIN][j];
  }

  // Allowed combinations of the DTZ value of a position and the best
  // value of the opponent after a piece move.
  for (i = 0; i < 256; i++)
    for (j = 0; j < 256; j++)
      dtz_matrix[i][j] = 0;

  // DRAW (including threat draws and stalemates)
  dtz_matrix[dtz_draw][DTZ_NONE] = 1;
  dtz_matrix[dtz_draw][opp_draw] = 1;
  dtz_matrix[dtz_draw][opp_capt_draw] = 1;

  // PDRAW
  dtz_matrix[dtz_capt_draw][DTZ_NONE] = 1;
  for (i = DTZ_CAPT_WIN; i <= opp_capt_draw; i++)
    dtz_matrix[dtz_capt_draw][i] = 1;

  // PWIN, THR_WIN1
  for (i = DTZ_NONE; i <= DTZ_SLOSS; i++)
    dtz_matrix[DTZ_CAPT_WIN][i] = 1;

  // BASE_WIN (including threat wins)
  dtz_matrix[DTZ_BASE_WIN + 1][DTZ_SLOSS] = 1;
  if (ply_accurate_win)
    dtz_matrix[DTZ_BASE_WIN + 2][DTZ_CAPT_LOSS] = 1;
  else
    dtz_matrix[DTZ_BASE_WIN + 1][DTZ_CAPT_LOSS] = 1;
  for (i = 1; i < win_num; i++)
    dtz_matrix[DTZ_BASE_WIN + i + 1][DTZ_BASE_LOSS - i] = 1;

  // PLOSS with DTZ 0
  dtz_matrix[DTZ_CAPT_LOSS][DTZ_NONE] = 1;
  dtz_matrix[DTZ_CAPT_LOSS][DTZ_SWIN] = 1;
  if (!ply_accurate_loss)
    dtz_matrix[DTZ_CAPT_LOSS][DTZ_CAPT_WIN] = 1;

  // BASE_LOSS
  dtz_matrix[DTZ_BASE_LOSS - 1][DTZ_SWIN] = 1;
  if (ply_accurate_loss) {
    dtz_matrix[DTZ_BASE_LOSS - 2][DTZ_CAPT_WIN] = 1;
    for (i = 2; i < loss_num; i++)
      dtz_matrix[DTZ_BASE_LOSS - 1 - i][DTZ_BASE_WIN + i] = 1;
  } else {
    dtz_matrix[DTZ_BASE_LOSS - 1][DTZ_CAPT_WIN] = 1;
    for (i = 1; i < loss_num; i++)
      dtz_matrix[DTZ_BASE_LOSS - 1 - i][DTZ_BASE_WIN + i] = 1;
  }

  // PCWIN
  dtz_matrix[dtz_capt_cwin][DTZ_NONE] = 1;
  if (ply_accurate_win)
    for (i = DTZ_CAPT_WIN; i <= DTZ_BASE_LOSS - win_num; i++)
      dtz_matrix[dtz_capt_cwin][i] = 1;
  else
    for (i = DTZ_CAPT_WIN; i <= opp_capt_closs; i++)
      dtz_matrix[dtz_capt_cwin][i] = 1;

  // BASE_CWIN (including threat cursed wins)
  if (ply_accurate_win) {
    dtz_matrix[dtz_base_cwin][DTZ_BASE_LOSS - win_num] = 1;
    dtz_matrix[dtz_base_cwin][opp_capt_closs] = 1;
    dtz_matrix[dtz_base_cwin][opp_base_closs] = 1;
  } else
    dtz_matrix[dtz_base_cwin][opp_capt_closs] = 1;
  for (i = 1; i <= max_dtz_cursed; i++)
    dtz_matrix[dtz_base_cwin + i][opp_base_closs - (i - 1)] = 1;
  dtz_matrix[dtz_base_cwin + max_dtz_cursed][opp_base_closs - max_dtz_cursed] = 1;

  // PCLOSS with DTZ 0
  dtz_matrix[dtz_capt_closs][DTZ_NONE] = 1;
  for (i = DTZ_CAPT_WIN; i <= opp_capt_cwin; i++)
    dtz_matrix[dtz_capt_closs][i] = 1;

  // BASE_CLOSS
  if (ply_accurate_loss) {
    dtz_matrix[dtz_base_closs][DTZ_BASE_WIN + loss_num] = 1;
    dtz_matrix[dtz_base_closs][opp_capt_cwin] = 1;
    dtz_matrix[dtz_base_closs][opp_base_cwin] = 1;
  } else
    dtz_matrix[dtz_base_closs][opp_capt_cwin] = 1;
  for (i = 1; i <= max_dtz_cursed; i++)
    dtz_matrix[dtz_base_closs - i][opp_base_cwin + (i - 1)] = 1;
  dtz_matrix[dtz_base_closs - max_dtz_cursed][opp_base_cwin + max_dtz_cursed] = 1;
}

void init_pawn_dtz(int opp)
{
}

void init_wdl(void)
{
}

void calc_broken(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ, bb;
  uint64_t end = thread->end;

  for (idx = thread->begin; idx < end; idx += 64) {
    FILL_OCC64_cheap {
      if (n == numpawns) {
        for (i = 0; i < 64; i++)
          table_w[idx + i] = table_b[idx + i] =
                (i < 8 || i >= 56 || (occ & bit[i ^ pw[n - 1]]))
                ? A_BROKEN : A_NONE;
      } else {
        for (i = 0, bb = 1ULL; i < 64; i++, bb <<= 1)
          table_w[idx + i] = table_b[idx + i] = (occ & bb) ? A_BROKEN : A_NONE;
      }
    } else {
      // two or more on one square or a pawn on rank 1 or 8
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = A_BROKEN;
    }
  }
}

// Also marks positions as broken in which pawns of the type of the pivot
// pawn are not in canonical order (these are not stored in the table).
void calc_broken_pp(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ, bb;
  uint64_t end = thread->end;

  for (idx = thread->begin; idx < end; idx += 64) {
    int c = 0;
    bb = 0;
    for (i = pp_num, idx2 = (idx ^ pw_mask) >> pp_shift; i > 0; i--, idx2 >>= 6)
      bit_set_test(bb, idx2 & 0x3f, c);
    bit_set_test(bb, piv_sq[idx2], c);
    if ((bb & pp_mask) || c) {
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = A_BROKEN;
      continue;
    }
    occ = 0;
    for (i = n - 2, idx2 = (idx ^ pw_mask) >> 6; i >= numpawns; i--, idx2 >>= 6)
      bit_set_test(occ, idx2 & 0x3f, c);
    for (; i > pp_num; i--, idx2 >>= 6)
      bit_set_test(bb, idx2 & 0x3f, c);
    if (!c && !(bb & (occ | PAWN_MASK))) {
      occ |= bb;
      if (n == numpawns) {
        for (i = 0; i < 64; i++)
          table_w[idx + i] = table_b[idx + i] =
                (i < 8 || i >= 56 || (occ & bit[i ^ pw[n - 1]]))
                ? A_BROKEN : A_NONE;
      } else {
        for (i = 0, bb = 1ULL; i < 64; i++, bb <<= 1)
          table_w[idx + i] = table_b[idx + i] = (occ & bb) ? A_BROKEN : A_NONE;
      }
    } else {
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = A_BROKEN;
    }
  }
}

MARK(mark_capt_value, uint8_t v)
{
  MARK_BEGIN;
  set_max(&table[idx2], v);
  MARK_END;
}

static int captured_piece;

static const uint8_t capt_val[5] = {
  A_CAPT_WIN, A_CAPT_CWIN, A_CAPT_DRAW, A_CAPT_CLOSS, A_CAPT_LOSS
};

// capturing the last piece of the opponent loses
static void probe_last_capture_w(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_WHITE_PIECES(mark_capt_value, A_CAPT_LOSS);
    }
  }
}

static void probe_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      CHECK_WHITE_PIECES;
      int v = probe_tb(pt2, p, 0, occ, -2, 2);
      MAKE_IDX2;
      LOOP_WHITE_PIECES(mark_capt_value, capt_val[v + 2]);
    }
  }
}

static void probe_last_capture_b(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_BLACK_PIECES(mark_capt_value, A_CAPT_LOSS);
    }
  }
}

static void probe_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      CHECK_BLACK_PIECES;
      int v = probe_tb(pt2, p, 1, occ, -2, 2);
      MAKE_IDX2;
      LOOP_BLACK_PIECES(mark_capt_value, capt_val[v + 2]);
    }
  }
}

static void probe_last_pivot_capture(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      LOOP_PIECES_PIVOT(mark_capt_value, A_CAPT_LOSS);
    }
  }
}

static void probe_pivot_captures(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      CHECK_PIECES_PIVOT;
      int v = probe_tb(pt2, p, wtm, occ, -2, 2);
      MAKE_IDX2_PIVOT;
      LOOP_PIECES_PIVOT(mark_capt_value, capt_val[v + 2]);
    }
  }
}

void calc_captures_w(void)
{
  int i;
  int n = numpcs;

  if (last_b >= 0) {
    if (last_b) {
      captured_piece = last_b;
      run_threaded(probe_last_capture_w, work_g, 1);
    } else
      run_threaded(probe_last_pivot_capture, work_piv, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over black pieces
      if (!(pt[i] & 0x08)) continue;
      if (i) {
        captured_piece = i;
        run_threaded(probe_captures_w, work_g, 1);
      } else
        run_threaded(probe_pivot_captures, work_piv, 1);
    }
  }
}

void calc_captures_b(void)
{
  int i;
  int n = numpcs;

  if (last_w >= 0) {
    if (last_w) {
      captured_piece = last_w;
      run_threaded(probe_last_capture_b, work_g, 1);
    } else
      run_threaded(probe_last_pivot_capture, work_piv, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over white pieces
      if (pt[i] & 0x08) continue;
      if (i) {
        captured_piece = i;
        run_threaded(probe_captures_b, work_g, 1);
      } else
        run_threaded(probe_pivot_captures, work_piv, 1);
    }
  }
}

void calc_last_pawn_capture_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_w[idx] == A_BROKEN || table_w[idx] == A_CAPT_LOSS) continue;
    FILL_OCC;
    bitboard bits = bit[p[last_b]];
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      if (sides_mask[p[i] + 0x08] & bits) break;
    }
    if (i < numpawns)
      table_w[idx] = A_CAPT_LOSS;
  }
}

void calc_last_pawn_capture_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_b[idx] == A_BROKEN || table_b[idx] == A_CAPT_LOSS) continue;
    FILL_OCC;
    bitboard bits = bit[p[last_w]];
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      if (sides_mask[p[i] - 0x08] & bits) break;
    }
    if (i < numpawns)
      table_b[idx] = A_CAPT_LOSS;
  }
}

// value for the capturing side of the best capture by pawn k to one of
// the squares next to sq, or -4 if there is none
static int probe_pawn_capt(int k, int sq, int clr, int wtm, bitboard occ,
    int *p)
{
  int i, m;
  int v;
  int best = -4;
  int pt2[MAX_PIECES];
  int pos[MAX_PIECES];
  bitboard bits;

  for (bits = sides_mask[sq] & occ; bits; ClearFirst(bits)) {
    int sq2 = FirstOne(bits);
    int t;
    for (t = 0; p[t] != sq2; t++);
    if ((pt[t] & 0x08) == clr) continue;
    for (i = 0; i < numpcs; i++) {
      pos[i] = p[i];
      pt2[i] = pt[i];
    }
    pt2[t] = 0;
    pos[k] = sq2;
    if (sq >= 0x08 && sq < 0x38) {
      v = -probe_tb(pt2, pos, wtm, occ, -2, 2);
      if (v > best) best = v;
    } else { // promotion
      for (m = KING; m >= KNIGHT; m--) {
        pt2[k] = m | (pt[k] & 0x08);
        v = -probe_tb(pt2, pos, wtm, occ, -2, 2);
        if (v > best) best = v;
      }
    }
    if (best == 2) break;
  }

  return best;
}

void calc_pawn_captures_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_w[idx] == A_BROKEN || table_w[idx] == A_CAPT_WIN) continue;
    FILL_OCC;
    int v = -4;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      int val = probe_pawn_capt(i, p[i] + 0x08, 0, 0, occ & ~bit[p[i]], p);
      if (val > v) v = val;
    }
    if (v > -4)
      set_max(&table_w[idx], capt_val[2 - v]);
  }
}

void calc_pawn_captures_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_b[idx] == A_BROKEN || table_b[idx] == A_CAPT_WIN) continue;
    FILL_OCC;
    int v = -4;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      int val = probe_pawn_capt(i, p[i] - 0x08, 8, 1, occ & ~bit[p[i]], p);
      if (val > v) v = val;
    }
    if (v > -4)
      set_max(&table_b[idx], capt_val[2 - v]);
  }
}

MARK(mark_threat, uint8_t v)
{
  MARK_BEGIN;
  set_threat(&table[idx2], v);
  MARK_END;
}

uint8_t *iter_table, *iter_table_opp;
int *iter_pcs_opp;

// For each position in iter_table in which the side to move has to
// capture, mark the positions in iter_table_opp from which the opponent
// can move into it with a piece move.
void calc_threats(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *table = iter_table;
  uint8_t *table_opp = iter_table_opp;
  int *pcs_opp = iter_pcs_opp;

  LOOP_ITER_ALL {
    int v = capt_threat[table[idx]];
    if (!v) continue;
    FILL_OCC;
    RETRO(mark_threat, v);
  }
}

static int has_moves_pieces(int *pcs, bitboard occ, int *p)
{
  for (; *pcs >= 0; pcs++)
    if (PieceMoves1(p[*pcs], pt[*pcs], occ))
      return 1;
  return 0;
}

static int has_pawn_moves(int clr, bitboard occ, int *p)
{
  int i;

  for (i = 0; i < numpawns; i++) {
    if (clr ? pw[i] : !pw[i]) continue;
    int sq = clr ? p[i] - 0x08 : p[i] + 0x08;
    if (!(bit[sq] & occ)) return 1;
  }
  return 0;
}

static const uint8_t capt_class[A_CAPT_WIN + 1] = {
  0, WDL_CAPT_LOSS, WDL_CAPT_CLOSS, WDL_CAPT_DRAW, WDL_CAPT_CWIN, WDL_CAPT_WIN
};

void load_wdl(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int p[MAX_PIECES];
  int pos[MAX_PIECES];
  bitboard occ;
  uint8_t *norm = load_entry->file[file].norm[load_bside];
  uint64_t *factor = load_entry->file[file].factor[load_bside];
  struct TBEntry_pawn *entry = load_entry;
  int clr = load_bside;
  int *pcs = clr ? black_pcs : white_pcs;
  int stalemate = clr ? stalemate_b : stalemate_w;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 == A_BROKEN) continue;
    if (IS_CAPT(v1)) {
      table[idx] = capt_class[v1];
      continue;
    }
    FILL_OCC;
    for (i = 0; i < n; i++)
      pos[perm[i]] = p[i];
    idx2 = encode_pawn_ver(entry, norm, pos, factor);
    v2 = src[idx2];
    if (v2 > 4) {
      table[idx] = WDL_ERROR;
    } else if (!has_moves_pieces(pcs, occ, p) && !has_pawn_moves(clr, occ, p)) {
      // stalemate
      if (v2 != stalemate + 2)
        table[idx] = WDL_ERROR;
      else
        table[idx] = stalemate > 0 ? WDL_SWIN
                     : stalemate < 0 ? WDL_SLOSS : WDL_DRAW;
    } else
      table[idx] = wdl_matrix[v1][v2];
    if (unlikely(table[idx] == WDL_ERROR))
      error("WDL_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d\n", idx, v1, v2);
  }
}

static void load_dtz_common(struct thread_data *thread, int mapping)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->file[file].norm[load_bside];
  uint64_t *factor = load_entry->file[file].factor[load_bside];
  struct TBEntry_pawn *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = piv_sq[idx2];
    idx2 = encode_pawn_ver(entry, norm, pos, factor);
    v2 = src[idx2];
    if (mapping == 1)
      v2 = load_map[wdl_tbl_to_wdl[v1]][v2];
    else if (mapping == 2) {
      v2 = load_map16[wdl_tbl_to_wdl[v1]][v2];
      if (v2 > 255) v2 = 255;
    }
    table[idx] = wdl_to_dtz[v1][v2];
    if (unlikely(table[idx] == DTZ_ERROR))
      error("DTZ_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx, v1, v2, idx2);
  }
}

void load_dtz(struct thread_data *thread)
{
  load_dtz_common(thread, 0);
}

void load_dtz_mapped(struct thread_data *thread)
{
  load_dtz_common(thread, 1);
}

void load_dtz_mapped16(struct thread_data *thread)
{
  load_dtz_common(thread, 2);
}

// value for the side to move after a double pawn push and an en passant
// capture of pawn k by pawn l
static int eval_ep(int k, int l, int sq, int ep, int wtm, bitboard occ, int *p)
{
  int i, v;
  int pt2[MAX_PIECES];

  occ ^= bit[sq] | bit[ep] | bit[p[k]];
  p[l] = ep;

  for (i = 0; i < numpcs; i++)
    pt2[i] = pt[i];
  pt2[k] = 0;
  v = probe_tb(pt2, p, wtm, occ, -2, 2);
  p[l] = sq;

  return v;
}

// Best value of the pawn moves of the side to move (-3 if there are none).
static int pawn_moves_value(uint64_t idx, int clr, uint8_t *opp_table,
    bitboard occ, int *p)
{
  int i, k;
  int best = -3;
  int pos[MAX_PIECES];
  int pt2[MAX_PIECES];
  int n = numpcs;
  int dir = clr ? -0x08 : 0x08;
  int opp_pawn = clr ? WPAWN : BPAWN;

  for (i = 0; i < numpawns; i++) {
    if (clr ? !pw[i] : pw[i]) {
      int sq = p[i];
      int sq1 = sq + dir;
      if (bit[sq1] & occ) continue;
      if (sq1 < 0x08 || sq1 >= 0x38) { // promotion
        bitboard bb = occ ^ bit[sq] ^ bit[sq1];
        for (k = 0; k < n; k++) {
          pos[k] = p[k];
          pt2[k] = pt[k];
        }
        pos[i] = sq1;
        for (k = KING; k >= KNIGHT; k--) {
          pt2[i] = k | (clr << 3);
          int v = -probe_tb(pt2, pos, clr, bb, -2, 2); // opponent to move
          if (v > best) best = v;
        }
        continue;
      }
      uint64_t idx0 = idx & ~mask[i];
      uint64_t idx2;
      if (i) idx2 = idx0 | ((uint64_t)(sq1 ^ pw[i]) << shift[i]);
      else idx2 = idx0 | piv_idx[sq1];
      int c = opp_table[idx2];
      if (cls_val[c] > -3 && -cls_val[c] > best) best = -cls_val[c];
      // double push
      int sq2 = sq1 + dir;
      if ((clr ? sq >= 0x30 : sq < 0x10) && !(bit[sq2] & occ)) {
        if (i) idx2 = idx0 | ((uint64_t)(sq2 ^ pw[i]) << shift[i]);
        else idx2 = idx0 | piv_idx[sq2];
        c = opp_table[idx2];
        int v1 = 3;
        bitboard bits = sides_mask[sq2] & occ;
        while (bits) {
          int sq3 = FirstOne(bits);
          for (k = 0; p[k] != sq3; k++);
          if (pt[k] == opp_pawn) {
            int v0 = eval_ep(i, k, sq3, sq1, !clr, occ, p); // we are to move
            if (v0 < v1) v1 = v0;
          }
          ClearFirst(bits);
        }
        int v = cls_val[c] > -3 ? -cls_val[c] : -3;
        if (v1 < 3) {
          // en passant is compulsory if it is the only capture
          if (v1 < v || !CLS_IS_CAPT(c))
            v = v1;
        }
        if (v > best) best = v;
      }
    }
  }

  return best;
}

static int moves_to_slosses(int *pcs, uint64_t idx0, uint8_t *opp_table,
    bitboard occ, int *p)
{
  int k, sq;
  bitboard bb;

  while ((k = *pcs++) >= 0) {
    bb = PieceMoves1(p[k], pt[k], occ);
    uint64_t idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      if (opp_table[MakeMove(idx, k, sq)] == WDL_SLOSS) return 1;
      ClearFirst(bb);
    }
  }
  return 0;
}

static const uint8_t pawn_class[5] = {
  WDL_PLOSS, WDL_PCLOSS, WDL_PDRAW, WDL_PCWIN, WDL_PWIN
};

static void calc_pawn_moves(struct thread_data *thread, int clr)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ, bb = thread->occ;
  int *p = thread->p;
  uint64_t end = begin + thread->end;
  uint8_t *table = clr ? table_b : table_w;
  uint8_t *opp_table = clr ? table_w : table_b;
  int *pcs = clr ? black_pcs : white_pcs;

  for (idx = begin + thread->begin; idx < end; idx++) {
    int c = table[idx];
    if (c > WDL_THR_WIN || c == WDL_ERROR || c == WDL_SWIN || c == WDL_SLOSS
        || c == WDL_PLOSS || c == WDL_PCLOSS)
      continue;
    FILL_OCC_PIECES;
    int piece_moves = has_moves_pieces(pcs, occ, p);
    int bp = pawn_moves_value(idx, clr, opp_table, occ, p);
    if (bp == -3 && !piece_moves) continue; // stalemate
    int val = cls_val[c];
    if (unlikely(bp > val || (!piece_moves && bp != val))) {
      table[idx] = WDL_ERROR;
      error("ERROR: pawn moves, idx = %"PRIu64", class = %d, value = %d\n", idx, c, bp);
      continue;
    }
    if (bp == val)
      table[idx] = pawn_class[val + 2];
    else if (c == WDL_THR_WIN && moves_to_slosses(pcs, idx, opp_table, occ, p))
      table[idx] = WDL_THR_WIN1;
  }
}

void calc_pawn_moves_w(struct thread_data *thread)
{
  calc_pawn_moves(thread, 0);
}

void calc_pawn_moves_b(struct thread_data *thread)
{
  calc_pawn_moves(thread, 1);
}

// Code of the best value (with the highest key) over all piece moves.
static int compute_pieces(int *pcs, uint64_t idx0, uint8_t *table,
    bitboard occ, int *p)
{
  int sq, k;
  uint64_t idx, idx2;
  bitboard bb;
  int best = DTZ_NONE;

  while ((k = *pcs++) >= 0) {
    bb = PieceMoves1(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove(idx, k, sq);
      int v = table[idx2];
      if (dtz_key[v] > dtz_key[best]) best = v;
      ClearFirst(bb);
    }
  }

  return best;
}

static int has_capture(int *all, int *all_opp, bitboard occ, int *p)
{
  bitboard opp_occ = 0;
  int k;

  for (; *all_opp >= 0; all_opp++)
    opp_occ |= bit[p[*all_opp]];
  for (; (k = *all) >= 0; all++)
    if (PieceRange(p[k], pt[k], occ) & opp_occ)
      return 1;
  return 0;
}

void verify_opp(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *opp_table = load_opp_table;
  uint8_t *dtz_table = load_table;
  int *opp_pieces = load_opp_pieces;

  LOOP_ITER_ALL {
    int v = opp_table[idx];
    if (v == WDL_SWIN || v == WDL_SLOSS) {
      opp_table[idx] = v == WDL_SWIN ? DTZ_SWIN : DTZ_SLOSS;
      continue;
    }
    if (!opp_compute[v]) {
      opp_table[idx] = opp_c[v - WDL_ERROR];
      if (unlikely(v == WDL_ERROR))
        error("ERROR: opp table, idx = %"PRIu64", v = WDL_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    int w = compute_pieces(opp_pieces, idx, dtz_table, occ, p);
    int z = dtz_to_opp[v][w];
    opp_table[idx] = z;
    if (unlikely(z == DTZ_ERROR))
      error("ERROR: opp table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void verify_dtz(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *dtz_table = load_table;
  uint8_t *opp_table = load_opp_table;
  int *dtz_pieces = load_pieces;
  int *all = load_pieces == white_pcs ? white_all : black_all;
  int *all_opp = load_pieces == white_pcs ? black_all : white_all;

  LOOP_ITER_ALL {
    int v = dtz_table[idx];
    if (v == DTZ_BROKEN || v == DTZ_SWIN || v == DTZ_SLOSS) continue;
    if (unlikely(v == DTZ_ERROR)) {
      error("ERROR: dtz table, idx = %"PRIu64", v = DTZ_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    if (has_capture(all, all_opp, occ, p)) continue;
    int w = compute_pieces(dtz_pieces, idx, opp_table, occ, p);
    if (unlikely(!dtz_matrix[v][w]))
      error("ERROR: dtz table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

// WDL-only verification is not supported (and disabled in tbverp.c).
void wdl_load_wdl(struct thread_data *thread)
{
}

void verify_wdl_w(struct thread_data *thread)
{
}

void verify_wdl_b(struct thread_data *thread)
{
}
