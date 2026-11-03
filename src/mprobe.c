/*
  Probe Makruk tablebases from the command line.

  This file is distributed under the terms of the GNU GPL, version 2.

  Reads one FEN per line from stdin (Makruk letters k, r, s, n, m, p;
  only the board and the side to move are used) and prints

    <wdl> <dtz>

  wdl is the WDL value for the side to move (pawnless tables: with a
  freshly started count). For pawnless positions (and for all Cambodian
  positions) dtz is the signed distance D in plies (see
  docs/variants/makruk.md), for pawnful positions it is 0.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"
#include "threads.h"
#include "util.h"

#define HAS_PAWNS
#include "board.h"

static int white_king = 0, black_king = 1;
int numpcs;
int numpawns;

#include "probe.c"
#include "board.c"

static int parse_fen(const char *fen, int *pieces, int *pos, int *wtm)
{
  int n = 2, sq = 56;
  pieces[0] = pieces[1] = 0;
  for (; *fen && *fen != ' '; fen++) {
    char ch = *fen;
    if (ch == '/') { sq -= 16; continue; }
    if (ch >= '1' && ch <= '8') { sq += ch - '0'; continue; }
    int color = (ch >= 'a') ? 8 : 0;
    int t;
    switch (ch | 0x20) {
    case 'k': t = KING; break;
    case 'r': t = ROOK; break;
    case 's': t = BISHOP; break;
    case 'n': t = KNIGHT; break;
    case 'm': t = QUEEN; break;
    case 'p': t = PAWN; break;
    default: return 0;
    }
    if (t == KING) {
      int k = color ? 1 : 0;
      if (pieces[k]) return 0;
      pieces[k] = t | color;
      pos[k] = sq;
    } else {
      if (n == TBPIECES) return 0;
      pieces[n] = t | color;
      pos[n++] = sq;
    }
    sq++;
  }
  if (!pieces[0] || !pieces[1]) return 0;
  while (*fen == ' ') fen++;
  *wtm = (*fen != 'b');
  return n;
}

int main(void)
{
  char line[256];
  int pieces[TBPIECES], pos[TBPIECES], wtm;

  init_tablebases();
  set_up_tables();
  fflush(stdout);

  while (fgets(line, sizeof(line), stdin)) {
    numpcs = parse_fen(line, pieces, pos, &wtm);
    if (!numpcs) {
      printf("error\n");
      fflush(stdout);
      continue;
    }
    numpawns = 0;
    bitboard occ = 0;
    for (int i = 0; i < numpcs; i++) {
      occ |= bit[pos[i]];
      if ((pieces[i] & 0x07) == PAWN) numpawns++;
    }
    int wdl, dtz = 0;
    if (numpcs == 2)
      wdl = 0;
#ifdef CAMBODIAN
    else if (1) { // all Cambodian tables store D
#else
    else if (!numpawns) {
#endif
      wdl = probe_table(pieces, pos, wtm);
      dtz = probe_dtz_makruk(pieces, pos, wtm);
    } else
      wdl = probe_tb(pieces, pos, wtm, occ, -2, 2);
    printf("%d %d\n", wdl, dtz);
    fflush(stdout);
  }

  return 0;
}
