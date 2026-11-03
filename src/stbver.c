/*
  Copyright (c) 2011-2013, 2018 Ronald de Man
  Copyright (c) 2026 Fabian Fichter

  This file is distributed under the terms of the GNU GPL, version 2.
*/

// Verifier for pawnless suicide and giveaway tables (included by tbver.c).
//
// It follows the structure of rtbver.c, adapted to the rules of losing
// chess and to the way stbgen.c stores the tables:
//
// - Captures are compulsory. The value of a position in which the side to
//   move can capture is determined by the captures alone (probed in the
//   subtables), and its WDL and DTZ values are "don't care" in the table.
// - A non-capture move that forces the opponent to capture is a "threat".
//   Positions whose value is determined by such a threat (threat win,
//   threat cursed win, threat draw) have "don't care" DTZ values. In
//   tables with 6 or more pieces their WDL values are only upper bounded,
//   because the probing code (sprobe.c) searches threats itself and takes
//   the maximum of the threat value and the stored value.
//
// The verifier checks that the probed WDL value of every position is
// correct and that the stored DTZ values are consistent with the WDL
// values and with each other.

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

// WDL classes after loading the WDL table. Only the first four classes
// require a DTZ value.
#define WDL_LOSS 0
#define WDL_CLOSS 1
#define WDL_CWIN 2
#define WDL_WIN 3
#define WDL_ERROR 4
#define WDL_DRAW 5
#define WDL_THR_CWIN 6
#define WDL_THR_WIN 7
#define WDL_CAPT_LOSS 8
#define WDL_CAPT_CLOSS 9
#define WDL_CAPT_DRAW 10
#define WDL_CAPT_CWIN 11
#define WDL_CAPT_WIN 12
#define WDL_BROKEN 13

#define IS_CAPT(v) ((v) >= A_CAPT_LOSS && (v) <= A_CAPT_WIN)

static uint8_t capt_threat[256];
static uint8_t wdl_matrix[A_THR_WIN + 1][5];

// index for the DTZ map: 0 = win, 1 = loss, 2 = cursed win, 3 = cursed loss
static int wdl_tbl_to_wdl[] = { 1, 3, 2, 0 };

// DTZ values. For the side to move, lower values are better.
#define DTZ_NONE 0 // no legal move (cannot happen without pawns)
#define DTZ_CAPT_WIN 1
#define DTZ_BASE_WIN 2
#define DTZ_CAPT_LOSS 252
#define DTZ_BASE_LOSS 252
#define DTZ_ERROR 254
#define DTZ_BROKEN 255
int dtz_capt_cwin;
int dtz_base_cwin;
int dtz_draw;
int dtz_capt_draw;
int dtz_capt_closs;
int dtz_base_closs;
int max_dtz_cursed;
int opp_capt_cwin, opp_base_cwin, opp_draw, opp_capt_draw;
int opp_capt_closs, opp_base_closs;

// DTZ values for the classes not requiring a DTZ value (WDL_ERROR and up)
static uint8_t wdl_to_dtz_c[WDL_BROKEN - WDL_ERROR + 1];
static uint8_t opp_c[WDL_BROKEN - WDL_ERROR + 1];

// mapping for WDL_LOSS - WDL_WIN
static uint8_t wdl_to_dtz[4][256];
static uint8_t dtz_to_opp[WDL_THR_WIN + 1][256];
static uint8_t dtz_matrix[256][256];
static uint8_t dtz_skip[256];
static uint8_t opp_compute[WDL_BROKEN + 1];

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

void init_capt_threat(void)
{
  int i, j;

  for (i = 0; i < 256; i++)
    capt_threat[i] = 0;
  // a move into a position where the opponent must capture
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

static void init_wdl_dtz(void)
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

  // smallest stored DTZ values of non-capture wins (3 plies, as a win in
  // 2 plies is a threat) and losses (2 plies)
  int min_win = ply_accurate_win ? 2 : 1;
  int min_loss = ply_accurate_loss ? 1 : 0;
  int thr_win = DTZ_BASE_WIN + 1 + (ply_accurate_win ? 1 : 0);

  wdl_to_dtz_c[WDL_ERROR - WDL_ERROR] = DTZ_ERROR;
  wdl_to_dtz_c[WDL_DRAW - WDL_ERROR] = dtz_draw;
  wdl_to_dtz_c[WDL_THR_CWIN - WDL_ERROR] = dtz_base_cwin;
  wdl_to_dtz_c[WDL_THR_WIN - WDL_ERROR] = thr_win;
  wdl_to_dtz_c[WDL_CAPT_LOSS - WDL_ERROR] = DTZ_CAPT_LOSS;
  wdl_to_dtz_c[WDL_CAPT_CLOSS - WDL_ERROR] = dtz_capt_closs;
  wdl_to_dtz_c[WDL_CAPT_DRAW - WDL_ERROR] = dtz_capt_draw;
  wdl_to_dtz_c[WDL_CAPT_CWIN - WDL_ERROR] = dtz_capt_cwin;
  wdl_to_dtz_c[WDL_CAPT_WIN - WDL_ERROR] = DTZ_CAPT_WIN;
  wdl_to_dtz_c[WDL_BROKEN - WDL_ERROR] = DTZ_BROKEN;

  for (i = 0; i <= WDL_BROKEN - WDL_ERROR; i++)
    opp_c[i] = DTZ_ERROR;
  opp_c[WDL_CAPT_LOSS - WDL_ERROR] = DTZ_CAPT_LOSS;
  opp_c[WDL_CAPT_CLOSS - WDL_ERROR] = opp_capt_closs;
  opp_c[WDL_CAPT_DRAW - WDL_ERROR] = opp_capt_draw;
  opp_c[WDL_CAPT_CWIN - WDL_ERROR] = opp_capt_cwin;
  opp_c[WDL_CAPT_WIN - WDL_ERROR] = DTZ_CAPT_WIN;
  opp_c[WDL_BROKEN - WDL_ERROR] = DTZ_BROKEN;

  for (i = 0; i <= WDL_BROKEN; i++)
    opp_compute[i] = 0;
  opp_compute[WDL_LOSS] = opp_compute[WDL_CLOSS] = 1;
  opp_compute[WDL_CWIN] = opp_compute[WDL_WIN] = 1;
  opp_compute[WDL_DRAW] = 1;
  opp_compute[WDL_THR_CWIN] = opp_compute[WDL_THR_WIN] = 1;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 256; j++)
      wdl_to_dtz[i][j] = DTZ_ERROR;

  for (i = min_win; i < win_num; i++)
    wdl_to_dtz[WDL_WIN][i] = DTZ_BASE_WIN + 1 + i;

  for (i = 0; i < 256; i++)
    wdl_to_dtz[WDL_CWIN][i] = dtz_base_cwin
                              + (i < max_dtz_cursed ? i : max_dtz_cursed);

  for (i = min_loss; i < loss_num; i++)
    wdl_to_dtz[WDL_LOSS][i] = DTZ_BASE_LOSS - 1 - i;

  for (i = 0; i < 256; i++)
    wdl_to_dtz[WDL_CLOSS][i] = dtz_base_closs
                               - (i < max_dtz_cursed ? i : max_dtz_cursed);

  // Opponent values, derived from the best move into the DTZ table.
  for (i = 0; i <= WDL_THR_WIN; i++)
    for (j = 0; j < 256; j++)
      dtz_to_opp[i][j] = DTZ_ERROR;

  // WDL_DRAW
  dtz_to_opp[WDL_DRAW][dtz_draw] = opp_draw;
  dtz_to_opp[WDL_DRAW][dtz_capt_draw] = opp_draw;

  // WDL_WIN
  if (ply_accurate_loss) {
    dtz_to_opp[WDL_WIN][DTZ_CAPT_LOSS] = DTZ_BASE_WIN + 2;
    for (i = 0; i < loss_num - 1; i++)
      dtz_to_opp[WDL_WIN][DTZ_BASE_LOSS - i - 1] = DTZ_BASE_WIN + i + 2;
  } else {
    dtz_to_opp[WDL_WIN][DTZ_CAPT_LOSS] = DTZ_BASE_WIN + 1;
    for (i = 0; i < loss_num; i++)
      dtz_to_opp[WDL_WIN][DTZ_BASE_LOSS - i - 1] = DTZ_BASE_WIN + i + 1;
  }

  // WDL_CWIN
  if (ply_accurate_loss)
    dtz_to_opp[WDL_CWIN][DTZ_BASE_LOSS - loss_num] = opp_base_cwin;
  dtz_to_opp[WDL_CWIN][dtz_capt_closs] = opp_base_cwin;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_CWIN][dtz_base_closs - i] = opp_base_cwin + i;

  // WDL_LOSS
  if (ply_accurate_win) {
    dtz_to_opp[WDL_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 2;
    for (i = 0; i < win_num - 1; i++)
      dtz_to_opp[WDL_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 2;
  } else {
    dtz_to_opp[WDL_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 1;
    for (i = 0; i < win_num; i++)
      dtz_to_opp[WDL_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 1;
  }

  // WDL_CLOSS
  if (ply_accurate_win)
    dtz_to_opp[WDL_CLOSS][DTZ_BASE_WIN + win_num] = opp_base_closs;
  dtz_to_opp[WDL_CLOSS][dtz_capt_cwin] = opp_base_closs;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_CLOSS][dtz_base_cwin + i] = opp_base_closs - i;

  // Threat positions have the same value as the corresponding normal
  // positions (the best move is the threat).
  for (j = 0; j < 256; j++) {
    dtz_to_opp[WDL_THR_WIN][j] = dtz_to_opp[WDL_WIN][j];
    dtz_to_opp[WDL_THR_CWIN][j] = dtz_to_opp[WDL_CWIN][j];
  }

  // Positions in the DTZ table that are not checked: captures (their value
  // is determined by the subtables) and broken positions.
  for (i = 0; i < 256; i++)
    dtz_skip[i] = 0;
  dtz_skip[DTZ_CAPT_WIN] = dtz_skip[DTZ_CAPT_LOSS] = 1;
  dtz_skip[dtz_capt_cwin] = dtz_skip[dtz_capt_closs] = 1;
  dtz_skip[dtz_capt_draw] = 1;
  dtz_skip[DTZ_ERROR] = dtz_skip[DTZ_BROKEN] = 1;

  // Allowed combinations of the DTZ value of a position and the best
  // value of the opponent after a move.
  for (i = 0; i < 256; i++)
    for (j = 0; j < 256; j++)
      dtz_matrix[i][j] = 0;

  // DRAW (including threat draws)
  dtz_matrix[dtz_draw][opp_draw] = 1;
  dtz_matrix[dtz_draw][opp_capt_draw] = 1;

  // BASE_WIN (including threat wins)
  if (ply_accurate_win)
    dtz_matrix[DTZ_BASE_WIN + 2][DTZ_CAPT_LOSS] = 1;
  else
    dtz_matrix[DTZ_BASE_WIN + 1][DTZ_CAPT_LOSS] = 1;
  for (i = 1; i < win_num; i++)
    dtz_matrix[DTZ_BASE_WIN + i + 1][DTZ_BASE_LOSS - i] = 1;

  // BASE_LOSS
  if (ply_accurate_loss) {
    dtz_matrix[DTZ_BASE_LOSS - 2][DTZ_CAPT_WIN] = 1;
    for (i = 2; i < loss_num; i++)
      dtz_matrix[DTZ_BASE_LOSS - 1 - i][DTZ_BASE_WIN + i] = 1;
  } else {
    dtz_matrix[DTZ_BASE_LOSS - 1][DTZ_CAPT_WIN] = 1;
    for (i = 1; i < loss_num; i++)
      dtz_matrix[DTZ_BASE_LOSS - 1 - i][DTZ_BASE_WIN + i] = 1;
  }

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

void calc_broken(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ, bb;
  uint64_t end = thread->end;

  for (idx = thread->begin; idx < end; idx += 64) {
    FILL_OCC64_cheap {
      for (i = 0, bb = 1; i < 64; i++, bb <<= 1)
        table_w[idx + i] = table_b[idx + i] = (occ & bb) ? A_BROKEN : A_NONE;
    } else {
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = A_BROKEN;
    }
  }
}

MARK_PIVOT(mark_capt_value, uint8_t v)
{
  MARK_BEGIN_PIVOT;
  set_max(&table[idx2], v);
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    set_max(&table[idx3], v);
  }
  MARK_END;
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
      int v = probe_tb(pt2, p, 0, occ, -2, 2);
      MAKE_IDX2;
      LOOP_WHITE_PIECES(mark_capt_value, capt_val[v + 2]);
    }
  }
}

static void probe_last_pivot_capture_b(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      LOOP_BLACK_PIECES_PIVOT(mark_capt_value, A_CAPT_LOSS);
    }
  }
}

static void probe_pivot_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      CHECK_BLACK_PIECES_PIVOT;
      int v = probe_tb(pt2, p, 1, occ, -2, 2);
      MAKE_IDX2_PIVOT;
      LOOP_BLACK_PIECES_PIVOT(mark_capt_value, capt_val[v + 2]);
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
      int v = probe_tb(pt2, p, 1, occ, -2, 2);
      MAKE_IDX2;
      LOOP_BLACK_PIECES(mark_capt_value, capt_val[v + 2]);
    }
  }
}

void calc_captures_w(void)
{
  int i, k;
  int n = numpcs;

  k = 0;
  for (i = 0; i < n; i++)
    if (pt[i] & 0x08) k++;

  if (k == 1) {
    for (i = 0; i < n; i++)
      if (pt[i] & 0x08) break;
    captured_piece = i;
    run_threaded(probe_last_capture_w, work_g, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over black pieces
      if (!(pt[i] & 0x08)) continue;
      captured_piece = i;
      run_threaded(probe_captures_w, work_g, 1);
    }
  }
}

void calc_captures_b(void)
{
  int i, k;
  int n = numpcs;

  k = 0;
  for (i = 0; i < n; i++)
    if (!(pt[i] & 0x08)) k++;

  if (k == 1) {
    for (i = 0; i < n; i++)
      if (!(pt[i] & 0x08)) break;
    captured_piece = i;
    if (i == 0)
      run_threaded(probe_last_pivot_capture_b, work_piv, 1);
    else
      run_threaded(probe_last_capture_b, work_g, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over white pieces
      if (pt[i] & 0x08) continue;
      captured_piece = i;
      if (i == 0)
        run_threaded(probe_pivot_captures_b, work_piv, 1);
      else
        run_threaded(probe_captures_b, work_g, 1);
    }
  }
}

MARK_PIVOT(mark_threat, uint8_t v)
{
  MARK_BEGIN_PIVOT;
  set_threat(&table[idx2], v);
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    set_threat(&table[idx3], v);
  }
  MARK_END;
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
// can move into it.
void calc_threats(struct thread_data *thread)
{
  BEGIN_ITER;
  uint8_t *table = iter_table;
  uint8_t *table_opp = iter_table_opp;
  int *pcs_opp = iter_pcs_opp;

  LOOP_ITER {
    int v = capt_threat[table[idx]];
    if (!v) continue;
    FILL_OCC;
    RETRO(mark_threat, v);
  }
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
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->norm[load_bside];
  uint64_t *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 == A_BROKEN) continue;
    if (IS_CAPT(v1)) {
      table[idx] = capt_class[v1];
      continue;
    }
    for (i = n - 1, idx2 = idx; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = inv_tri0x40[idx2];
    idx2 = encode_piece(entry, norm, pos, factor);
    v2 = src[idx2];
    table[idx] = v2 > 4 ? WDL_ERROR : wdl_matrix[v1][v2];
    if (unlikely(table[idx] == WDL_ERROR))
      error("WDL_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d\n", idx, v1, v2);
  }
}

void load_dtz(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->norm[load_bside];
  uint64_t *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    for (i = n - 1, idx2 = idx; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = inv_tri0x40[idx2];
    idx2 = encode_piece(entry, norm, pos, factor);
    v2 = src[idx2];
    table[idx] = wdl_to_dtz[v1][v2];
    if (unlikely(table[idx] == DTZ_ERROR))
      error("DTZ_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx, v1, v2, idx2);
  }
}

void load_dtz_mapped(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->norm[load_bside];
  uint64_t *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;
  uint8_t (*map)[256] = load_map;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    int wdl = wdl_tbl_to_wdl[v1];
    for (i = n - 1, idx2 = idx; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = inv_tri0x40[idx2];
    idx2 = encode_piece(entry, norm, pos, factor);
    v2 = map[wdl][src[idx2]];
    table[idx] = wdl_to_dtz[v1][v2];
    if (unlikely(table[idx] == DTZ_ERROR))
      error("DTZ_ERROR: idx = %"PRIu64", wdl = %d, v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx, wdl, v1, v2, idx2);
  }
}

void load_dtz_mapped16(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->norm[load_bside];
  uint64_t *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;
  uint16_t (*map16)[MAX_VALS] = load_map16;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    int wdl = wdl_tbl_to_wdl[v1];
    for (i = n - 1, idx2 = idx; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = inv_tri0x40[idx2];
    idx2 = encode_piece(entry, norm, pos, factor);
    v2 = map16[wdl][src[idx2]];
    v2 = v2 > 255 ? 255 : v2;
    table[idx] = wdl_to_dtz[v1][v2];
    if (unlikely(table[idx] == DTZ_ERROR))
      error("DTZ_ERROR: idx = %"PRIu64", wdl = %d, v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx, wdl, v1, v2, idx2);
  }
}

// Best value (highest, i.e. worst for the opponent) over all non-capture
// moves. Only called for positions without captures.
static int compute(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ, int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;
  int best = DTZ_NONE;
  int k;

  while ((k = *pcs++) >= 0) {
    bb = PieceMoves(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove(idx, k, sq);
      int v = table[idx2];
      if (v > best) best = v;
      ClearFirst(bb);
    }
  }

  return best;
}

void verify_opp(struct thread_data *thread)
{
  BEGIN_ITER;
  uint8_t *opp_table = load_opp_table;
  uint8_t *dtz_table = load_table;
  int *opp_pieces = load_opp_pieces;

  LOOP_ITER {
    int v = opp_table[idx];
    if (!opp_compute[v]) {
      opp_table[idx] = opp_c[v - WDL_ERROR];
      if (unlikely(v == WDL_ERROR))
        error("ERROR: opp table, idx = %"PRIu64", v = WDL_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    int w = compute(opp_pieces, idx, dtz_table, occ, p);
    int z = dtz_to_opp[v][w];
    opp_table[idx] = z;
    if (unlikely(z == DTZ_ERROR))
      error("ERROR: opp table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void verify_dtz(struct thread_data *thread)
{
  BEGIN_ITER;
  uint8_t *dtz_table = load_table;
  uint8_t *opp_table = load_opp_table;
  int *dtz_pieces = load_pieces;

  LOOP_ITER {
    int v = dtz_table[idx];
    if (dtz_skip[v]) {
      if (unlikely(v == DTZ_ERROR))
        error("ERROR: dtz table, idx = %"PRIu64", v = DTZ_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    int w = compute(dtz_pieces, idx, opp_table, occ, p);
    if (unlikely(!dtz_matrix[v][w]))
      error("ERROR: dtz table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

// WDL-only verification is not supported (and disabled in tbver.c).
static void init_wdl(void)
{
}

void wdl_load_wdl(struct thread_data *thread)
{
}

void verify_wdl(struct thread_data *thread)
{
}
