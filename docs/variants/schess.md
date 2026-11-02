# Seirawan chess (S-Chess)

## Rules

Seirawan chess adds two pieces to chess: the hawk (`H`, moves as a bishop
or a knight) and the elephant (`E`, moves as a rook or a knight). In the
game both start in hand and are gated in when a piece leaves its initial
square on the first rank.

The tablebases cover positions **without pieces in hand**. Gating is then
impossible and the rules are those of chess with the two additional
pieces: checkmate wins, stalemate is a draw, the 50-move rule applies, and
pawns may promote to a knight, bishop, rook, queen, hawk or elephant.
Castling rights are not represented, as in chess tables.

## Building and usage

`make -C src schess` builds `etbgen`, `etbgenp`, `etbver`, `etbverp` and
`tbcheck`. Files have the suffixes `.etbw`/`.etbz`; subtables are looked up
in `$ETBPATH`, statistics are written to `$ETBSTATSDIR`, verification logs
to `etblog.txt`. Usage and options are the same as for the chess generator.

Table names use `H` for the hawk and `E` for the elephant. The pieces of a
side are ordered `K Q E H R B N P`, the side with more pieces comes first
and, for equal numbers, the side that is smaller in this order, e.g.
`KEHvK`, `KQvKE`, `KHPvKR`.

`tests/variant_test.py schess` generates and verifies the tables (see the
main README); `--max 4` covers all 63 tables with up to 4 pieces.

## File format

Same layout as the chess tables, with WDL magic `0x7e19c2a5` and DTZ magic
`0xd3b8475e`. Pieces are stored in nibbles as in chess (type in bits 0-2,
bit 3 set for black): pawn 1, knight 2, bishop 3, rook 4, queen 5, king 6
and hawk 7. The elephant is stored with type bits 0, i.e. nibble `0x0` for
a white and `0x8` for a black elephant.

Internally the generator uses 4 bits for the piece type in S-Chess
(`TYPE_MASK`/`BLACK_BIT` in `src/types.h`); for the other variants the
encoding is unchanged.

## Resources

| Pieces | Pawnless tables | Pawnful tables |
|--------|-----------------|----------------|
| 3      | 6               | 1              |
| 4      | 42              | 14             |
| 5      | 182             | 98             |
| 6      | 693             | 511            |
| 7      | 2184            | 2100           |

Each table has the same size and needs the same memory and time to
generate as a chess table with the same number of pieces and pawns.

## Limitations

- Positions with pieces in hand are not covered. A probing engine must
  only probe when neither side has a piece in hand.
- As for chess, pawnful tables with symmetric material (e.g. `KPvKP`)
  cannot be verified.
- The 7-piece probing limits (`TBMAX_PIECE`, `TBMAX_PAWN`, `HSHMAX`) are
  sized for the complete 7-piece set, but tables with more than 4 pieces
  have not been generated yet.

## Tables and probing

No tables have been published yet. Fairy-Stockfish support for probing
these tables is planned.
