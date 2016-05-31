/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016 Yosuke Demura
 * except where otherwise indicated.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "movegen.h"

#include <algorithm>
#include "position.h"

namespace {

/**
 * 指し手生成を担当するクラスです.
 * C++では、クラスでしかテンプレートの部分特殊化を行うことができないので、
 * 指し手生成関数は、特殊化されたクラスのstaticメンバ関数として実装しています。
 */
template<GeneratorType, Color> struct Generator {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};

// 以下、特殊化されたクラス
template<Color kColor> struct Generator<kRecaptures, kColor> {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};
template<Color kColor> struct Generator<kCaptures, kColor> {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};
template<Color kColor> struct Generator<kQuiets, kColor> {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};
template<Color kColor> struct Generator<kEvasions, kColor> {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};
template<Color kColor> struct Generator<kChecks, kColor> {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};
template<Color kColor> struct Generator<kQuietChecks, kColor> {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};
template<Color kColor> struct Generator<kAdjacentChecks, kColor> {
  static ExtMove* GenerateMoves(const Position&, ExtMove*);
};

/**
 * 移動手のうち、成る手を生成します.
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenPromotions(Square from, Bitboard to_bb, ExtMove* stack) {
  assert(stack != nullptr);
  to_bb.ForEach([&](Square to) {
    (stack++)->move = Move(Piece(kColor, kPt), from, to, true);
  });
  return stack;
}

/**
 * 移動手のうち、成らない手を生成します。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenNonPromotions(Square from, Bitboard to_bb, ExtMove* stack) {
  assert(stack != nullptr);
  to_bb.ForEach([&](Square to) {
    (stack++)->move = Move(Piece(kColor, kPt), from, to);
  });
  return stack;
}

template<Color kColor, PieceType kPt>
inline ExtMove* GenNonPromotions(const Position& pos, Bitboard target,
                                 ExtMove* stack) {
  assert(stack != nullptr);
  pos.pieces(kColor, kPt).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<kColor, kPt>(from) & target;
    stack = GenNonPromotions<kColor, kPt>(from, to_bb, stack);
  });
  return stack;
}

/**
 * 指定された手番側の指し手を生成します.
 * この関数は、「打つ手」および「玉を動かす手」を生成しないことに注意してください。
 */
template<Color C>
ExtMove* GenMoves(const Position& pos, const Bitboard target, ExtMove* stack) {
  assert(stack != nullptr);

  const Bitboard rank1_3 = rank_bb<C, 1, 3>();
  const Bitboard rank3_5 = rank_bb<C, 3, 5>();
  const Bitboard rank3_8 = rank_bb<C, 3, 8>();
  const Bitboard rank4   = rank_bb<C, 4, 4>();
  const Bitboard rank5_9 = rank_bb<C, 5, 9>();

  // 1. 歩
  {
    Bitboard to_bb = pawns_attacks_bb(pos.pieces(C, kPawn), C) & target;
    // 成る手
    (to_bb & rank1_3).ForEach([&](Square to) {
      Square from = to + (C == kBlack ? kDeltaS : kDeltaN);
      (stack++)->move = Move(C, kPawn, from, to, true);
    });
    // 成らない手
    to_bb.andnot(rank1_3).Serialize([&](Square to) {
      Square from = to + (C == kBlack ? kDeltaS : kDeltaN);
      (stack++)->move = Move(C, kPawn, from, to);
    });
  }

  // 2. 香車
  pos.pieces(C, kLance).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kLance>(from) & target;
    stack = GenPromotions<C, kLance>(from, to_bb & rank1_3, stack);
    stack = GenNonPromotions<C, kLance>(from, to_bb & rank3_8, stack);
  });

  // 3. 桂馬
  Bitboard knights = pos.pieces(C, kKnight);
  (knights & rank3_5).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kKnight>(from) & target;
    stack = GenPromotions<C, kKnight>(from, to_bb, stack);
  });
  (knights & rank5_9).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kKnight>(from) & target;
    stack = GenNonPromotions<C, kKnight>(from, to_bb, stack);
  });

  // 4. 銀
  Bitboard silvers = pos.pieces(C, kSilver);
  (silvers & rank1_3).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kSilver>(from) & target;
    to_bb.Serialize([&](Square to) {
      (stack++)->move = Move(C, kSilver, from, to, true);
      (stack++)->move = Move(C, kSilver, from, to);
    });
  });
  (silvers & rank4).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kSilver>(from) & target;
    (to_bb & rank1_3).ForEach([&](Square to) {
      (stack++)->move = Move(C, kSilver, from, to, true);
      (stack++)->move = Move(C, kSilver, from, to);
    });
    stack = GenNonPromotions<C, kSilver>(from, to_bb.andnot(rank1_3), stack);
  });
  (silvers & rank5_9).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kSilver>(from) & target;
    stack = GenNonPromotions<C, kSilver>(from, to_bb, stack);
  });

  // 5. 金
  stack = GenNonPromotions<C, kGold>(pos, target, stack);

  // 6. 角
  Bitboard bishops = pos.pieces(C, kBishop);
  (bishops & rank1_3).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kBishop>(from) & target;
    stack = GenPromotions<C, kBishop>(from, to_bb, stack);
  });
  bishops.andnot(rank1_3).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kBishop>(from) & target;
    stack = GenPromotions<C, kBishop>(from, to_bb & rank1_3, stack);
    stack = GenNonPromotions<C, kBishop>(from, to_bb.andnot(rank1_3), stack);
  });

  // 7. 飛車
  Bitboard rooks = pos.pieces(C, kRook);
  (rooks & rank1_3).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kRook>(from) & target;
    stack = GenPromotions<C, kRook>(from, to_bb, stack);
  });
  rooks.andnot(rank1_3).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<C, kRook>(from) & target;
    stack = GenPromotions<C, kRook>(from, to_bb & rank1_3, stack);
    stack = GenNonPromotions<C, kRook>(from, to_bb.andnot(rank1_3), stack);
  });

  // 8. すでに成っている駒
  stack = GenNonPromotions<C, kPPawn  >(pos, target, stack);
  stack = GenNonPromotions<C, kPLance >(pos, target, stack);
  stack = GenNonPromotions<C, kPKnight>(pos, target, stack);
  stack = GenNonPromotions<C, kPSilver>(pos, target, stack);
  stack = GenNonPromotions<C, kHorse  >(pos, target, stack);
  stack = GenNonPromotions<C, kDragon >(pos, target, stack);

  return stack;
}

/**
 * 指定された移動先のマスに、駒を動かす手を生成します（取り返しの手を生成するのに便利です）.
 * この関数は、玉を動かす手を生成しないことに注意してください。
 */
template<Color kColor>
ExtMove* GenMovesTo(const Position& pos, const Square to, ExtMove* stack) {
  assert(stack != nullptr);
  assert(pos.is_empty(to) || pos.piece_on(to).color() != pos.side_to_move());

  // 移動先のマスに利きをつけている手番側の駒を求める（玉を除く）
  Bitboard attackers = pos.AttackersTo<kColor>(to, pos.pieces())
                          .andnot(pos.pieces(kKing));

  // 利きをつけている駒がなければ、終了する
  if (attackers.none()) {
    return stack;
  }

  const Piece captured = pos.piece_on(to);

  if (to.is_promotion_zone_of(kColor)) {
    // 成ることができる駒に限定する
    Bitboard promotables = pos.pieces(kPawn, kLance, kKnight, kSilver, kBishop, kRook);
    Bitboard promotions = attackers & promotables;

    // 成る手を生成する
    promotions.ForEach([&](Square from) {
      (stack++)->move = Move(pos.piece_on(from), from, to, true, captured);
    });

    // 劣等手または非合法手を除外する
    Bitboard exclusions = pos.pieces(kPawn, kBishop, kRook);
    if (relative_rank(kColor, to.rank()) <= kRank2) {
      exclusions |= pos.pieces(kLance, kKnight);
    }
    Bitboard non_promotions = attackers.andnot(exclusions);

    // 成らない手を生成する
    non_promotions.ForEach([&](Square from) {
      (stack++)->move = Move(pos.piece_on(from), from, to, false, captured);
    });
  } else {
    const Bitboard rank1_3 = rank_bb<kColor, 1, 3>();

    // 移動先のマスは敵陣３段目以内でなければならない
    Bitboard promotion_candidates = attackers & rank1_3;

    // 行きどころのない駒になっていないか検証する
    assert((promotion_candidates & pos.pieces(kPawn, kLance, kKnight)).none());

    // 成ることができない駒を除外する
    Bitboard promotables = pos.pieces(kSilver, kBishop, kRook);
    Bitboard promotions = promotion_candidates & promotables;

    // 成る手を生成する
    promotions.ForEach([&](Square from) {
      (stack++)->move = Move(pos.piece_on(from), from, to, true, captured);
    });

    // 劣等手を除外する
    Bitboard non_promotions = attackers.andnot(pos.pieces(kBishop, kRook) & rank1_3);

    // 成らない手を生成する
    non_promotions.ForEach([&](Square from) {
      (stack++)->move = Move(pos.piece_on(from), from, to, false, captured);
    });
  }

  return stack;
}

/**
 * 複数の駒打をいっぺんに生成します.
 * GenDrops()関数の実装で使用されている関数です。
 */
template<int kNumPieces, typename T>
inline ExtMove* GenManyDrops(const T& pieces, Bitboard to_bb, ExtMove* stack) {
  static_assert(0 < kNumPieces && kNumPieces <= 6, "");
  assert(stack != nullptr);
  assert(!to_bb.HasExcessBits());

  to_bb.Serialize([&](Square to) {
    // 最大６手をいっぺんに生成する
    if (kNumPieces >= 1) (stack++)->move = Move(pieces[0], to);
    if (kNumPieces >= 2) (stack++)->move = Move(pieces[1], to);
    if (kNumPieces >= 3) (stack++)->move = Move(pieces[2], to);
    if (kNumPieces >= 4) (stack++)->move = Move(pieces[3], to);
    if (kNumPieces >= 5) (stack++)->move = Move(pieces[4], to);
    if (kNumPieces >= 6) (stack++)->move = Move(pieces[5], to);
  });

  return stack;
}

/**
 * 駒打を生成します.
 */
template<Color kColor>
ExtMove* GenDrops(const Position& pos, Bitboard target, ExtMove* stack) {
  assert(stack != nullptr);
  assert(!target.HasExcessBits());

  const Hand hand = pos.hand(kColor);
  target = target.andnot(pos.pieces()) & rank_bb<1, 9>();

  // Step 1. 歩を打つ手を生成する
  if (hand.has(kPawn)) {
    const Bitboard rank2_9 = rank_bb<kColor, 2, 9>();
    Bitboard pawn_files = Bitboard::FileFill(pos.pieces(kColor, kPawn));
    Bitboard to_bb = (target & rank2_9).andnot(pawn_files);
    to_bb.Serialize([&](Square to) {
      (stack++)->move = Move(kColor, kPawn, to);
    });
  }

  // 歩以外の持ち駒がない場合は、終了する
  if (!hand.has_any_piece_except(kPawn)) {
    return stack;
  }

  Array<Piece, 6> pieces_in_hand;
  int numHands = 0, numKnight = 0, numLanceKnight = 0;

  // Step 2. 持ち駒にある駒のリストを作る
  if (hand.has(kRook  )) pieces_in_hand[numHands++] = Piece(kColor, kRook  );
  if (hand.has(kBishop)) pieces_in_hand[numHands++] = Piece(kColor, kBishop);
  if (hand.has(kGold  )) pieces_in_hand[numHands++] = Piece(kColor, kGold  );
  if (hand.has(kSilver)) pieces_in_hand[numHands++] = Piece(kColor, kSilver);
  if (hand.has(kLance)) {
    numLanceKnight++;
    pieces_in_hand[numHands++] = Piece(kColor, kLance);
  }
  if (hand.has(kKnight)) {
    numLanceKnight++, numKnight++;
    pieces_in_hand[numHands++] = Piece(kColor, kKnight);
  }

  // Step 3. １段目への駒打を生成する（飛・角・金・銀）
  const Bitboard rank1 = target & rank_bb<kColor, 1, 1>();
  switch (numHands - numLanceKnight) {
    case 0: break;
    case 1: stack = GenManyDrops<1>(pieces_in_hand, rank1, stack); break;
    case 2: stack = GenManyDrops<2>(pieces_in_hand, rank1, stack); break;
    case 3: stack = GenManyDrops<3>(pieces_in_hand, rank1, stack); break;
    case 4: stack = GenManyDrops<4>(pieces_in_hand, rank1, stack); break;
    default:
      assert(0); break;
  }

  // Step 4. ２段目への駒打を生成する（飛・角・金・銀・香）
  const Bitboard rank2 = target & rank_bb<kColor, 2, 2>();
  switch (numHands - numKnight) {
    case 0: break;
    case 1: stack = GenManyDrops<1>(pieces_in_hand, rank2, stack); break;
    case 2: stack = GenManyDrops<2>(pieces_in_hand, rank2, stack); break;
    case 3: stack = GenManyDrops<3>(pieces_in_hand, rank2, stack); break;
    case 4: stack = GenManyDrops<4>(pieces_in_hand, rank2, stack); break;
    case 5: stack = GenManyDrops<5>(pieces_in_hand, rank2, stack); break;
    default:
      assert(0); break;
  }

  // Step 5. ３段目から９段目への駒打を生成する（飛・角・金・銀・桂・香）
  const Bitboard rank3_9 = target & rank_bb<kColor, 3, 9>();
  switch (numHands) {
    case 0: break;
    case 1: stack = GenManyDrops<1>(pieces_in_hand, rank3_9, stack); break;
    case 2: stack = GenManyDrops<2>(pieces_in_hand, rank3_9, stack); break;
    case 3: stack = GenManyDrops<3>(pieces_in_hand, rank3_9, stack); break;
    case 4: stack = GenManyDrops<4>(pieces_in_hand, rank3_9, stack); break;
    case 5: stack = GenManyDrops<5>(pieces_in_hand, rank3_9, stack); break;
    case 6: stack = GenManyDrops<6>(pieces_in_hand, rank3_9, stack); break;
    default:
      assert(0); break;
  }

  return stack;
}

/**
 * 動かす手の王手を生成します.
 * なお、駒打王手についてはGenDropChecks()を、開き王手についてはGenDiscoveredChecks()を
 * 参照してください。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenDirectChecks(const Position& pos, const Square king_square,
                                ExtMove* stack) {
  static_assert(kPawn <= kPt && kPt <= kGold, "");

  constexpr Piece kPiece(kColor, kPt);
  Bitboard pieces = kPt == kGold ? pos.golds(kColor) : pos.pieces(kColor, kPt);
  Bitboard candidates = checker_candidates_bb(kPiece, king_square);

  // 王手可能な駒の候補がなければ、終了する
  if (!pieces.test(candidates)) {
    return stack;
  }

  Bitboard checker_candidates = pieces & candidates;
  constexpr PieceType kPromoted = GetPromotedType(kPt);
  const Bitboard occ = pos.pieces();
  const Bitboard own_pieces = pos.pieces(kColor);
  Bitboard promotion_target = attackers_to<kColor, kPromoted>(king_square, occ)
                              .andnot(own_pieces);
  Bitboard non_promotion_target = attackers_to<kColor, kPt>(king_square, occ)
                                  .andnot(own_pieces);

  // 非合法手または劣等手を除外する
  if (kPt == kPawn) {
    non_promotion_target &= rank_bb<kColor, 4, 9>();
  } else if (kPt == kLance || kPt == kKnight) {
    non_promotion_target &= rank_bb<kColor, 3, 9>();
  }

  // 後退できない駒が成る手の場合は、移動先が敵陣３段目以内でなければならない
  if (kPt != kSilver) {
    promotion_target &= rank_bb<kColor, 1, 3>();
  }

  do {
    const Square from = checker_candidates.pop_first_one();
    const Piece piece = kPt == kGold ? pos.piece_on(from) : kPiece;
    const Bitboard attacks = pos.AttacksFrom<kColor, kPt>(from);

    // 成らない手
    Bitboard non_promotions = attacks & non_promotion_target;
    non_promotions.ForEach([&](Square to) {
      (stack++)->move = Move(piece, from, to);
    });

    // 成る手
    if (kPiece.can_promote()) {
      Bitboard promotions = attacks & promotion_target;
      if (kPt == kSilver && relative_rank(kColor, from.rank()) > kRank3) {
        promotions &= rank_bb<kColor, 1, 3>();
      }
      promotions.ForEach([&](Square to) {
        (stack++)->move = Move(piece, from, to, true);
      });
    }
  } while (checker_candidates.any());

  return stack;
}

/**
 * 駒打王手を生成します.
 * なお、動かす手の王手についてはGenDirectChecks()を、開き王手についてはGenDiscoveredChecks()を
 * 参照してください。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenDropChecks(const Position& pos, const Square king_square,
                              ExtMove* stack) {
  static_assert(IsDroppablePieceType(kPt), "");

  // 持ち駒がない場合は、終了する
  if (!pos.hand(kColor).has(kPt)) {
    return stack;
  }

  Bitboard occ = pos.pieces();
  Bitboard target = attackers_to<kColor, kPt>(king_square, occ).andnot(pos.pieces());

  // 二歩を除外する
  if (kPt == kPawn) {
    Bitboard pawn_files = Bitboard::FileFill(pos.pieces(kColor, kPawn));
    target = target.andnot(pawn_files);
  }

  target.ForEach([&](Square to) {
    (stack++)->move = Move(kColor, kPt, to);
  });

  return stack;
}

/**
 * 開き王手を生成します.
 * なお、開き王手以外の王手については、GenDirectChecks()およびGenDropChecks()を
 * 参照してください。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenDiscoveredChecks(const Position& pos,
                                    const Bitboard dc_candidates,
                                    ExtMove* stack) {
  constexpr Piece kPiece(kColor, kPt);
  Bitboard pieces = kPt == kGold ? pos.golds(kColor) : pos.pieces(kColor, kPt);

  // 開き王手可能な駒の候補がなければ、終了する
  if (!pieces.test(dc_candidates)) {
    return stack;
  }

  Bitboard checker_candidates = pieces & dc_candidates;
  Bitboard target = Bitboard::board_bb().andnot(pos.pieces(kColor));
  const Square ksq = pos.king_square(~kColor);
  const Bitboard occ = pos.pieces();

  do {
    const Square from = checker_candidates.pop_first_one();
    const Piece piece = kPt == kGold ? pos.piece_on(from) : kPiece;
    Bitboard attacks = pos.AttacksFrom<kColor, kPt>(from);
    Bitboard discovered_checks = (attacks & target).andnot(line_bb(ksq, from));

    // 成らない手（ただし、直接王手を除く）
    Bitboard direct_checks_np = attackers_to<kColor, kPt>(ksq, occ);
    Bitboard non_promotions = discovered_checks.andnot(direct_checks_np);
    if (kPt == kBishop || kPt == kRook) {
      if (!from.is_promotion_zone_of(kColor)) {
        non_promotions &= rank_bb<kColor, 4, 9>();
        stack = GenNonPromotions<kColor, kPt>(from, non_promotions, stack);
      }
    } else {
      if (kPt == kPawn) {
         non_promotions &= rank_bb<kColor, 4, 9>();
       } else if (kPt == kLance || kPt == kKnight) {
         non_promotions &= rank_bb<kColor, 3, 9>();
       }
       non_promotions.ForEach([&](Square to) {
         (stack++)->move = Move(piece, from, to);
       });
    }

    // 成る手（ただし、直接王手を除く）
    if (kPiece.can_promote()) {
      constexpr PieceType kPromoted = GetPromotedType(kPt);
      Bitboard direct_checks_p = attackers_to<kColor, kPromoted>(ksq, occ);
      Bitboard promotions = discovered_checks.andnot(direct_checks_p);
      if (kPt == kSilver || kPt == kBishop || kPt == kRook) {
        // 銀・角・飛は後退できるので、移動先または移動元のいずれかが敵陣３段目以内であれば足りる
        if (!from.is_promotion_zone_of(kColor)) {
          promotions &= rank_bb<kColor, 1, 3>();
        }
      } else {
        assert(kPt == kPawn || kPt == kLance || kPt == kKnight);
        // 歩・香・桂は後退できないので、移動先が敵陣３段目以内でなければならない
        promotions &= rank_bb<kColor, 1, 3>();
      }
      promotions.ForEach([&](Square to) {
        (stack++)->move = Move(piece, from, to, true);
      });
    }
  } while (checker_candidates.any());

  return stack;
}

/**
 * 動かす手の王手のうち、取る手と成る手を除いた手を生成します.
 * すでに取る手や成る手の王手を生成済みの場合に、この関数を利用すると便利です。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenQuietDirectChecks(const Position& pos,
                                     const Square king_square, ExtMove* stack) {
  static_assert(kPawn <= kPt && kPt <= kGold, "");

  constexpr Piece kPiece(kColor, kPt);
  Bitboard pieces = kPt == kGold ? pos.golds(kColor) : pos.pieces(kColor, kPt);
  Bitboard candidates = checker_candidates_bb(kPiece, king_square);

  // 王手可能な駒の候補がなければ、終了する
  if (!pieces.test(candidates)) {
    return stack;
  }

  Bitboard checker_candidates = pieces & candidates;
  const Bitboard occ = pos.pieces();
  Bitboard non_promotion_target = attackers_to<kColor, kPt>(king_square, occ)
                                  .andnot(occ);

  // 非合法手や劣等手を除外する
  if (kPt == kPawn) {
    non_promotion_target &= rank_bb<kColor, 4, 9>();
  } else if (kPt == kLance || kPt == kKnight) {
    non_promotion_target &= rank_bb<kColor, 3, 9>();
  }

  do {
    const Square from = checker_candidates.pop_first_one();
    const Piece piece = kPt == kGold ? pos.piece_on(from) : kPiece;
    const Bitboard attacks = pos.AttacksFrom<kColor, kPt>(from);

    // 成らない手
    Bitboard non_promotions = attacks & non_promotion_target;
    non_promotions.ForEach([&](Square to) {
      (stack++)->move = Move(piece, from, to);
    });

    // 銀が成る手
    //（銀は、成る前後で駒の価値があまり変動しないので、例外的に、銀が成る手は「静かな手」として扱う）
    //（idea from Bonanza）
    if (kPt == kSilver) {
      Bitboard promotion_target = attackers_to<kColor, kPSilver>(king_square, occ)
                                  .andnot(occ);
      Bitboard promotions = attacks & promotion_target;
      if (!from.is_promotion_zone_of(kColor)) {
        promotions &= rank_bb<kColor, 1, 3>();
      }
      promotions.ForEach([&](Square to) {
        (stack++)->move = Move(kColor, kSilver, from, to, true);
      });
    }
  } while (checker_candidates.any());

  return stack;
}

/**
 * 開き王手のうち、取る手と成る手を除いた手を生成します.
 * すでに取る手や成る手の王手を生成済みの場合に、この関数を利用すると便利です。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenQuietDiscoveredChecks(const Position& pos,
                                         const Bitboard dc_candidates,
                                         ExtMove* stack) {
  constexpr Piece kPiece(kColor, kPt);
  Bitboard pieces = kPt == kGold ? pos.golds(kColor) : pos.pieces(kColor, kPt);

  // 開き王手可能な駒の候補がなければ、終了する
  if (!pieces.test(dc_candidates)) {
    return stack;
  }

  const Square ksq = pos.king_square(~kColor);
  const Bitboard occ = pos.pieces();
  Bitboard checker_candidates = pieces & dc_candidates;
  Bitboard target = Bitboard::board_bb().andnot(occ);

  do {
    const Square from = checker_candidates.pop_first_one();
    const Piece piece = kPt == kGold ? pos.piece_on(from) : kPiece;
    Bitboard attacks = pos.AttacksFrom<kColor, kPt>(from);
    Bitboard discovered_checks = (attacks & target).andnot(line_bb(ksq, from));

    // 成らない手（ただし、直接王手を除く）
    Bitboard direct_checks_np = attackers_to<kColor, kPt>(ksq, occ);
    Bitboard non_promotions = discovered_checks.andnot(direct_checks_np);
    if (kPt == kBishop || kPt == kRook) {
      if (!from.is_promotion_zone_of(kColor)) {
        non_promotions &= rank_bb<kColor, 4, 9>();
        stack = GenNonPromotions<kColor, kPt>(from, non_promotions, stack);
      }
    } else {
      if (kPt == kPawn) {
         non_promotions &= rank_bb<kColor, 4, 9>();
       } else if (kPt == kLance || kPt == kKnight) {
         non_promotions &= rank_bb<kColor, 3, 9>();
       }
       non_promotions.ForEach([&](Square to) {
         (stack++)->move = Move(piece, from, to);
       });
    }

    // 銀が成る手（ただし、直接王手を除く）
    if (kPt == kSilver) {
      Bitboard direct_checks_p = attackers_to<kColor, kPSilver>(ksq, occ);
      Bitboard promotions = discovered_checks.andnot(direct_checks_p);
      // 銀は後退できるので、移動先または移動元のいずれかが敵陣３段目以内であれば足りる
      if (!from.is_promotion_zone_of(kColor)) {
        promotions &= rank_bb<kColor, 1, 3>();
      }
      promotions.ForEach([&](Square to) {
        (stack++)->move = Move(kColor, kSilver, from, to, true);
      });
    }
  } while (checker_candidates.any());

  return stack;
}

/**
 * 動かす手の近接王手を生成します.
 * ここで近接王手とは、受け方の合駒が不可能な位置への王手のことです。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenAdjacentDirectChecks(const Position& pos,
                                        const Square king_square,
                                        ExtMove* stack) {
  static_assert(kPt != kKing, "");

  constexpr Piece kPiece(kColor, kPt);
  Bitboard pieces = kPt == kGold ? pos.golds(kColor) : pos.pieces(kColor, kPt);
  Bitboard candidates = adjacent_check_candidates_bb(kPiece, king_square);

  // 王手可能な駒の候補がなければ、終了する
  if (!pieces.test(candidates)) {
    return stack;
  }

  Bitboard checker_candidates = pieces & candidates;
  constexpr PieceType kPromoted = GetPromotedType(kPt);
  const Bitboard own_pieces = pos.pieces(kColor);
  Bitboard promotion_target = min_attacks_bb(Piece(~kColor, kPromoted), king_square)
                              .andnot(own_pieces);
  Bitboard non_promotion_target = min_attacks_bb(Piece(~kColor, kPt), king_square)
                                  .andnot(own_pieces);

  // 非合法手および劣等手を除外する
  if (kPt == kPawn || kPt == kBishop || kPt == kRook) {
    non_promotion_target &= rank_bb<kColor, 4, 9>();
  } else if (kPt == kLance || kPt == kKnight) {
    non_promotion_target &= rank_bb<kColor, 3, 9>();
  }

  // 後退できない駒が成る手の場合は、移動先が敵陣３段目以内でなければならない
  if (kPt == kPawn || kPt == kLance || kPt == kKnight) {
    promotion_target &= rank_bb<kColor, 1, 3>();
  }

  do {
    const Square from = checker_candidates.pop_first_one();
    const Piece piece = kPt == kGold ? pos.piece_on(from) : kPiece;
    const Bitboard attacks = pos.AttacksFrom<kColor, kPt>(from);

    // 成らない手の王手
    Bitboard non_promotions = attacks & non_promotion_target;
    if (kPt == kBishop || kPt == kRook) {
      // 劣等手を除外する
      if (!from.is_promotion_zone_of(kColor)) {
        non_promotions.ForEach([&](Square to) {
          (stack++)->move = Move(piece, from, to);
        });
      }
    } else {
      non_promotions.ForEach([&](Square to) {
        (stack++)->move = Move(piece, from, to);
      });
    }

    // 成る手の王手
    if (kPiece.can_promote()) {
      Bitboard promotions = attacks & promotion_target;
      if (   (kPt == kSilver || kPt == kBishop || kPt == kRook)
          && relative_rank(kColor, from.rank()) > kRank3) {
        promotions &= rank_bb<kColor, 1, 3>();
      }
      promotions.ForEach([&](Square to) {
        (stack++)->move = Move(piece, from, to, true);
      });
    }
  } while (checker_candidates.any());

  return stack;
}

/**
 * 駒打の近接王手を生成します.
 * ここで近接王手とは、受け方の合駒が不可能な位置への王手のことです。
 */
template<Color kColor, PieceType kPt>
inline ExtMove* GenAdjacentDropChecks(const Position& pos,
                                      const Square king_square,
                                      ExtMove* stack) {
  static_assert(IsDroppablePieceType(kPt), "");

  // 持ち駒がない場合は、終了する
  if (!pos.hand(kColor).has(kPt)) {
    return stack;
  }

  Bitboard occ = pos.pieces();
  Bitboard target = min_attacks_bb(Piece(~kColor, kPt), king_square).andnot(occ);

  if (kPt == kPawn) {
    if (target.any()) {
      // 二歩を除外する
      Bitboard own_pawns = pos.pieces(kColor, kPawn);
      Bitboard king_file = file_bb(king_square.file());
      if (!own_pawns.test(king_file)) {
        assert(target.count() == 1);
        Square to = target.pop_first_one();
        (stack++)->move = Move(kColor, kPt, to);
      }
    }
  } else {
    target.ForEach([&](Square to) {
      (stack++)->move = Move(kColor, kPt, to);
    });
  }

  return stack;
}

/**
 * 取り返しの手を生成します.
 */
template<Color kColor>
ExtMove* Generator<kRecaptures, kColor>::GenerateMoves(const Position& pos,
                                                       ExtMove* stack) {
  assert(!pos.in_check());
  assert(pos.last_move().is_real_move());
  assert(stack != nullptr);

  const Square to = pos.last_move().to();

  // 移動先のマスの上には、相手の駒があること
  assert(!pos.is_empty(to) && pos.piece_on(to).color() != pos.side_to_move());

  // 1. 玉以外で取り返す手を生成する
  stack = GenMovesTo<kColor>(pos, to, stack);

  // 2. 玉で取り返す手を生成する
  Bitboard king_bb = pos.AttackersTo<kColor, kKing>(to);
  if (king_bb.any()) {
    Square from = king_bb.first_one();
    Piece captured = pos.piece_on(to);
    (stack++)->move = Move(kColor, kKing, from, to, false, captured);
  }

  return stack;
}

/**
 * 取る手と成る手を生成します.
 * この関数では、「銀が駒を取らずに成る手」は生成しないことに注意してください。
 * これは、銀は、成る前後で駒の価値があまり変動しないので、例外扱いしているためです。
 */
template<Color kColor>
ExtMove* Generator<kCaptures, kColor>::GenerateMoves(const Position& pos,
                                                     ExtMove* stack) {
  assert(!pos.in_check());
  assert(stack != nullptr);

  const Bitboard rank1_3 = rank_bb<kColor, 1, 3>();
  const Bitboard rank3_5 = rank_bb<kColor, 3, 5>();
  const Bitboard rank3_9 = rank_bb<kColor, 3, 9>();
  const Bitboard rank4   = rank_bb<kColor, 4, 4>();
  const Bitboard rank5_9 = rank_bb<kColor, 5, 9>();

  ExtMove* const stack_begin = stack;
  const Bitboard target = pos.pieces(~kColor);
  const Bitboard movable = rank_bb<1, 9>().andnot(pos.pieces(kColor));

  // 1. 歩
  Bitboard pawns = pos.pieces(kColor, kPawn);
  {
    Bitboard attacks    = pawns_attacks_bb(pawns, kColor);
    Bitboard promotions = attacks & rank1_3 & movable;
    Bitboard captures   = attacks.andnot(rank1_3) & target;
    promotions.ForEach([&](Square to) {
      Square from = to + (kColor == kBlack ? kDeltaS : kDeltaN);
      (stack++)->move = Move(kColor, kPawn, from, to, true);
    });
    captures.ForEach([&](Square to) {
      Square from = to + (kColor == kBlack ? kDeltaS : kDeltaN);
      (stack++)->move = Move(kColor, kPawn, from, to);
    });
  }

  // 2. 香車
  pos.pieces(kColor, kLance).ForEach([&](Square from) {
    Bitboard attacks    = pos.AttacksFrom<kColor, kLance>(from);
    Bitboard promotions = attacks & rank1_3 & movable;
    Bitboard captures   = attacks & rank3_9 & target;
    stack = GenPromotions<kColor, kLance>(from, promotions, stack);
    stack = GenNonPromotions<kColor, kLance>(from, captures, stack);
  });

  // 3. 桂馬
  Bitboard knights = pos.pieces(kColor, kKnight);
  (knights & rank3_5).ForEach([&](Square from) {
    Bitboard promotions = pos.AttacksFrom<kColor, kKnight>(from) & movable;
    stack = GenPromotions<kColor, kKnight>(from, promotions, stack);
  });
  (knights & rank5_9).ForEach([&](Square from) {
    Bitboard captures = pos.AttacksFrom<kColor, kKnight>(from) & target;
    stack = GenNonPromotions<kColor, kKnight>(from, captures, stack);
  });

  // 4. 銀（取る手のみを生成する）
  Bitboard silvers = pos.pieces(kColor, kSilver);
  (silvers & rank1_3).ForEach([&](Square from) {
    Bitboard captures = pos.AttacksFrom<kColor, kSilver>(from) & target;
    captures.ForEach([&](Square to) {
      (stack++)->move = Move(kColor, kSilver, from, to, true);
      (stack++)->move = Move(kColor, kSilver, from, to);
    });
  });
  (silvers & rank4).ForEach([&](Square from) {
    Bitboard captures = pos.AttacksFrom<kColor, kSilver>(from) & target;
    stack = GenPromotions<kColor, kSilver>(from, captures & rank1_3, stack);
    stack = GenNonPromotions<kColor, kSilver>(from, captures, stack);
  });
  (silvers & rank5_9).ForEach([&](Square from) {
    Bitboard captures = pos.AttacksFrom<kColor, kSilver>(from) & target;
    stack = GenNonPromotions<kColor, kSilver>(from, captures, stack);
  });

  // 5. 金
  stack = GenNonPromotions<kColor, kGold   >(pos, target, stack);

  // 6. 角
  Bitboard bishops = pos.pieces(kColor, kBishop);
  (bishops & rank1_3).ForEach([&](Square from) {
    Bitboard promotions = pos.AttacksFrom<kColor, kBishop>(from) & movable;
    stack = GenPromotions<kColor, kBishop>(from, promotions, stack);
  });
  bishops.andnot(rank1_3).ForEach([&](Square from) {
    Bitboard attacks    = pos.AttacksFrom<kColor, kBishop>(from);
    Bitboard promotions = attacks & rank1_3 & movable;
    Bitboard captures   = attacks.andnot(rank1_3) & target;
    stack = GenPromotions<kColor, kBishop>(from, promotions, stack);
    stack = GenNonPromotions<kColor, kBishop>(from, captures, stack);
  });

  // 7. 飛車
  Bitboard rooks = pos.pieces(kColor, kRook);
  (rooks & rank1_3).ForEach([&](Square from) {
    Bitboard promotions = pos.AttacksFrom<kColor, kRook>(from) & movable;
    stack = GenPromotions<kColor, kRook>(from, promotions, stack);
  });
  rooks.andnot(rank1_3).ForEach([&](Square from) {
    Bitboard attacks    = pos.AttacksFrom<kColor, kRook>(from);
    Bitboard promotions = attacks & rank1_3 & movable;
    Bitboard captures   = attacks.andnot(rank1_3) & target;
    stack = GenPromotions<kColor, kRook>(from, promotions, stack);
    stack = GenNonPromotions<kColor, kRook>(from, captures, stack);
  });

  // 8. 成り駒（取る手のみを生成する）
  stack = GenNonPromotions<kColor, kPPawn  >(pos, target, stack);
  stack = GenNonPromotions<kColor, kPLance >(pos, target, stack);
  stack = GenNonPromotions<kColor, kPKnight>(pos, target, stack);
  stack = GenNonPromotions<kColor, kPSilver>(pos, target, stack);
  stack = GenNonPromotions<kColor, kHorse  >(pos, target, stack);
  stack = GenNonPromotions<kColor, kDragon >(pos, target, stack);

  // 9. 玉（取る手のみを生成する）
  if (pos.king_exists(kColor)) {
    Bitboard attacks = pos.AttacksFrom<kColor, kKing>(pos.king_square(kColor));
    stack = GenNonPromotions<kColor, kKing>(pos, attacks & target, stack);
  }

  // 10. 取った駒をセットする（ここでまとめてセットする方が簡単なので）
  for (ExtMove* it = stack_begin; it != stack; ++it) {
    Piece captured = pos.piece_on(it->move.to());
    it->move.set_captured_piece(captured);
  }

  return stack;
}

/**
 * 静かな手を生成します.
 * 基本的には、取る手と成る手は生成されませんが、「銀が駒を取らずに成る手」はここで生成されます。
 * これは、銀は、成る前後で駒の価値があまり変動しないので、例外的に静かな手と扱っているためです。
 */
template<Color kColor>
ExtMove* Generator<kQuiets, kColor>::GenerateMoves(const Position& pos,
                                                   ExtMove* stack) {
  assert(!pos.in_check());
  assert(stack != nullptr);

  const Bitboard rank1_3 = rank_bb<kColor, 1, 3>();
  const Bitboard rank3_8 = rank_bb<kColor, 3, 8>();
  const Bitboard rank4   = rank_bb<kColor, 4, 4>();
  const Bitboard rank4_9 = rank_bb<kColor, 4, 9>();
  const Bitboard rank5_9 = rank_bb<kColor, 5, 9>();

  const Bitboard empty_squares = rank_bb<1, 9>().andnot(pos.pieces());

  // 1. 歩
  Bitboard pawn_attacks = pawns_attacks_bb(pos.pieces(kColor, kPawn), kColor);
  (pawn_attacks & rank4_9 & empty_squares).Serialize([&](Square to) {
    Square from = to + (kColor == kBlack ? kDeltaS : kDeltaN);
    (stack++)->move = Move(kColor, kPawn, from, to);
  });

  // 2. 香車
  (pos.pieces(kColor, kLance) & rank4_9).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<kColor, kLance>(from) & empty_squares;
    stack = GenNonPromotions<kColor, kLance>(from, to_bb & rank3_8, stack);
  });

  // 3. 桂馬
  (pos.pieces(kColor, kKnight) & rank5_9).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<kColor, kKnight>(from) & empty_squares;
    stack = GenNonPromotions<kColor, kKnight>(from, to_bb, stack);
  });

  // 4. 銀
  Bitboard silvers = pos.pieces(kColor, kSilver);
  (silvers & rank1_3).ForEach([&](Square from) {
    Bitboard to_bb = pos.AttacksFrom<kColor, kSilver>(from) & empty_squares;
    to_bb.ForEach([&](Square to) {
      (stack++)->move = Move(kColor, kSilver, from, to, true);
      (stack++)->move = Move(kColor, kSilver, from, to);
    });
  });
  (silvers & rank4).ForEach([&](Square from) {
      Bitboard to_bb = pos.AttacksFrom<kColor, kSilver>(from) & empty_squares;
      (to_bb & rank1_3).ForEach([&](Square to) {
          (stack++)->move = Move(kColor, kSilver, from, to, true);
          (stack++)->move = Move(kColor, kSilver, from, to);
      });
      stack = GenNonPromotions<kColor, kSilver>(from, to_bb.andnot(rank1_3), stack);
  });
  (silvers & rank5_9).ForEach([&](Square from) {
      Bitboard to_bb = pos.AttacksFrom<kColor, kSilver>(from) & empty_squares;
      stack = GenNonPromotions<kColor, kSilver>(from, to_bb, stack);
  });

  // 5. 金
  stack = GenNonPromotions<kColor, kGold>(pos, empty_squares, stack);

  // 6. 角
  (pos.pieces(kColor, kBishop) & rank4_9).ForEach([&](Square from) {
    Bitboard attacks = pos.AttacksFrom<kColor, kBishop>(from);
    Bitboard to_bb   = attacks & empty_squares & rank4_9;
    stack = GenNonPromotions<kColor, kBishop>(from, to_bb, stack);
  });

  // 7. 飛車
  (pos.pieces(kColor, kRook) & rank4_9).ForEach([&](Square from) {
    Bitboard attacks = pos.AttacksFrom<kColor, kRook>(from);
    Bitboard to_bb   = attacks & empty_squares & rank4_9;
    stack = GenNonPromotions<kColor, kRook>(from, to_bb, stack);
  });

  // 8. 成り駒
  stack = GenNonPromotions<kColor, kPPawn  >(pos, empty_squares, stack);
  stack = GenNonPromotions<kColor, kPLance >(pos, empty_squares, stack);
  stack = GenNonPromotions<kColor, kPKnight>(pos, empty_squares, stack);
  stack = GenNonPromotions<kColor, kPSilver>(pos, empty_squares, stack);
  stack = GenNonPromotions<kColor, kHorse  >(pos, empty_squares, stack);
  stack = GenNonPromotions<kColor, kDragon >(pos, empty_squares, stack);

  // 9. 玉
  stack = GenNonPromotions<kColor, kKing>(pos, empty_squares, stack);

  // 10. 打つ手
  stack = GenDrops<kColor>(pos, empty_squares, stack);

  return stack;
}

/**
 * 王手回避手を生成します.
 */
template<Color kColor>
ExtMove* Generator<kEvasions, kColor>::GenerateMoves(const Position& pos,
                                                     ExtMove* stack) {
  assert(pos.in_check());
  assert(stack != nullptr);

  ExtMove* stack_begin = stack;
  const Bitboard target = Bitboard::board_bb().andnot(pos.pieces(kColor));

  // 1. 玉を動かす手
  if (pos.king_exists(kColor)) {
    // 王手している駒の利きを求める
    // TODO 利きデータを使って高速化
    Bitboard checkers = pos.checkers();
    Square s1 = checkers.pop_first_one();
    Bitboard attacked_by_checkers = min_attacks_bb(pos.piece_on(s1), s1);
    if (pos.num_checkers() == 2) {
      Square s2 = checkers.first_one();
      attacked_by_checkers |= min_attacks_bb(pos.piece_on(s2), s2);
    }
    // 玉を動かす手を生成する
    Square from = pos.king_square(kColor);
    Bitboard attacks = pos.AttacksFrom<kColor, kKing>(from);
    Bitboard evasions = (attacks & target).andnot(attacked_by_checkers);
    stack = GenNonPromotions<kColor, kKing>(from, evasions, stack);
    // 取った駒をセットする
    for (ExtMove* it = stack_begin; it != stack; ++it) {
      Piece captured = pos.piece_on(it->move.to());
      it->move.set_captured_piece(captured);
    }
    stack_begin = stack;
  }

  // 2. 玉を動かす手以外の手
  if (pos.num_checkers() == 1) {
    // a. 動かす手（王手している駒を取る手、合駒）
    Square checker_sq = pos.checkers().first_one();
    Bitboard captures      = pos.checkers();
    Bitboard interceptions = between_bb(pos.king_square(kColor), checker_sq);
    stack = GenMoves<kColor>(pos, target & (captures | interceptions), stack);

    // 取った駒をセットする
    for (ExtMove* it = stack_begin; it != stack; ++it) {
      Piece captured = pos.piece_on(it->move.to());
      it->move.set_captured_piece(captured);
    }

    // b. 打つ手（合駒のみ）
    stack = GenDrops<kColor>(pos, target & interceptions, stack);
  }

  return stack;
}

/**
 * 王手をすべて生成します.
 */
template<Color kColor>
ExtMove* Generator<kChecks, kColor>::GenerateMoves(const Position& pos,
                                                   ExtMove* stack) {
  assert(!pos.in_check());
  assert(stack != nullptr);

  ExtMove* const stack_begin = stack;
  const Square ksq = pos.king_square(~kColor);

  Bitboard dcc = pos.discovered_check_candidates();

  if (dcc.any()) {
    stack = GenDiscoveredChecks<kColor, kPawn  >(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kLance >(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kKnight>(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kSilver>(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kGold  >(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kBishop>(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kRook  >(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kKing  >(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kHorse >(pos, dcc, stack);
    stack = GenDiscoveredChecks<kColor, kDragon>(pos, dcc, stack);
  }

  stack = GenDirectChecks<kColor, kPawn  >(pos, ksq, stack);
  stack = GenDirectChecks<kColor, kLance >(pos, ksq, stack);
  stack = GenDirectChecks<kColor, kKnight>(pos, ksq, stack);
  stack = GenDirectChecks<kColor, kSilver>(pos, ksq, stack);
  stack = GenDirectChecks<kColor, kGold  >(pos, ksq, stack);

  const Bitboard occ = pos.pieces();
  const Bitboard rank1_3 = rank_bb<kColor, 1, 3>();
  const Bitboard target = Bitboard::board_bb().andnot(pos.pieces(kColor));
  const Bitboard king_neighborhood = step_attacks_bb(kBlackKing, ksq);
  const Bitboard bishop_target = bishop_attacks_bb(ksq, occ) & target;
  const Bitboard horse_target = (bishop_target | king_neighborhood) & target;

  // 角の王手
  const Bitboard bishops = pos.pieces(kColor, kBishop);
  (bishops & rank1_3).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackBishop, from).test(horse_target)) {
      Bitboard attacks    = pos.AttacksFrom<kColor, kBishop>(from);
      Bitboard promotions = attacks & horse_target;
      stack = GenPromotions<kColor, kBishop>(from, promotions, stack);
    }
  });
  bishops.andnot(rank1_3).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackBishop, from).test(horse_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kBishop>(from);
      Bitboard promotions     = attacks & horse_target & rank1_3;
      Bitboard non_promotions = (attacks & bishop_target).andnot(rank1_3);
      stack = GenPromotions<kColor, kBishop>(from, promotions, stack);
      stack = GenNonPromotions<kColor, kBishop>(from, non_promotions, stack);
    }
  });

  // 馬の王手
  pos.pieces(kColor, kHorse).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackHorse, from).test(horse_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kHorse>(from);
      Bitboard non_promotions = attacks & horse_target;
      stack = GenNonPromotions<kColor, kHorse>(from, non_promotions, stack);
    }
  });

  const Bitboard rook_target = rook_attacks_bb(ksq, occ) & target;
  const Bitboard dragon_target = (rook_target | king_neighborhood) & target;

  // 飛車の王手
  const Bitboard rooks = pos.pieces(kColor, kRook);
  (rooks & rank1_3).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackRook, from).test(dragon_target)) {
      Bitboard attacks    = pos.AttacksFrom<kColor, kRook>(from);
      Bitboard promotions = attacks & dragon_target;
      stack = GenPromotions<kColor, kRook>(from, promotions, stack);
    }
  });
  rooks.andnot(rank1_3).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackRook, from).test(dragon_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kRook>(from);
      Bitboard promotions     = attacks & dragon_target & rank1_3;
      Bitboard non_promotions = (attacks & rook_target).andnot(rank1_3);
      stack = GenPromotions<kColor, kRook>(from, promotions, stack);
      stack = GenNonPromotions<kColor, kRook>(from, non_promotions, stack);
    }
  });

  // 龍の王手
  pos.pieces(kColor, kDragon).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackDragon, from).test(dragon_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kDragon>(from);
      Bitboard non_promotions = attacks & dragon_target;
      stack = GenNonPromotions<kColor, kDragon>(from, non_promotions, stack);
    }
  });

  // 取った駒をセットする
  for (ExtMove* it = stack_begin; it != stack; ++it) {
    it->move.set_captured_piece(pos.piece_on(it->move.to()));
  }

  // 駒打の王手
  stack = GenDropChecks<kColor, kPawn  >(pos, ksq, stack);
  stack = GenDropChecks<kColor, kLance >(pos, ksq, stack);
  stack = GenDropChecks<kColor, kKnight>(pos, ksq, stack);
  stack = GenDropChecks<kColor, kSilver>(pos, ksq, stack);
  stack = GenDropChecks<kColor, kGold  >(pos, ksq, stack);
  if (pos.hand(kColor).has(kBishop)) {
    Bitboard checks = bishop_target.andnot(pos.pieces());
    checks.Serialize([&](Square to) {
      (stack++)->move = Move(kColor, kBishop, to);
    });
  }
  if (pos.hand(kColor).has(kRook)) {
    Bitboard checks = rook_target.andnot(pos.pieces());
    checks.Serialize([&](Square to) {
      (stack++)->move = Move(kColor, kRook, to);
    });
  }

  return stack;
}

/**
 * 静かな王手をすべて生成します.
 */
template<Color kColor>
ExtMove* Generator<kQuietChecks, kColor>::GenerateMoves(const Position& pos,
                                                        ExtMove* stack) {
  assert(!pos.in_check());
  assert(stack != nullptr);

  const Square ksq = pos.king_square(~kColor);
  Bitboard dcc = pos.discovered_check_candidates();

  if (dcc.any()) {
    stack = GenQuietDiscoveredChecks<kColor, kPawn  >(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kLance >(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kKnight>(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kSilver>(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kGold  >(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kBishop>(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kRook  >(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kKing  >(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kHorse >(pos, dcc, stack);
    stack = GenQuietDiscoveredChecks<kColor, kDragon>(pos, dcc, stack);
  }

  stack = GenQuietDirectChecks<kColor, kPawn  >(pos, ksq, stack);
  stack = GenQuietDirectChecks<kColor, kLance >(pos, ksq, stack);
  stack = GenQuietDirectChecks<kColor, kKnight>(pos, ksq, stack);
  stack = GenQuietDirectChecks<kColor, kSilver>(pos, ksq, stack);
  stack = GenQuietDirectChecks<kColor, kGold  >(pos, ksq, stack);

  const Bitboard occ = pos.pieces();
  const Bitboard rank1_3 = rank_bb<kColor, 1, 3>();
  const Bitboard target = Bitboard::board_bb().andnot(occ);
  const Bitboard king_neighborhood = step_attacks_bb(kBlackKing, ksq);
  const Bitboard bishop_target = bishop_attacks_bb(ksq, occ) & target;
  const Bitboard horse_target = (bishop_target | king_neighborhood) & target;

  // 角の王手
  const Bitboard bishops = pos.pieces(kColor, kBishop);
  bishops.andnot(rank1_3).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackBishop, from).test(bishop_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kBishop>(from);
      Bitboard non_promotions = (attacks & bishop_target).andnot(rank1_3);
      stack = GenNonPromotions<kColor, kBishop>(from, non_promotions, stack);
    }
  });

  // 馬の王手
  pos.pieces(kColor, kHorse).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackHorse, from).test(horse_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kHorse>(from);
      Bitboard non_promotions = attacks & horse_target;
      stack = GenNonPromotions<kColor, kHorse>(from, non_promotions, stack);
    }
  });

  const Bitboard rook_target = rook_attacks_bb(ksq, occ) & target;
  const Bitboard dragon_target = (rook_target | king_neighborhood) & target;

  // 飛車の王手
  const Bitboard rooks = pos.pieces(kColor, kRook);
  rooks.andnot(rank1_3).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackRook, from).test(dragon_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kRook>(from);
      Bitboard non_promotions = (attacks & rook_target).andnot(rank1_3);
      stack = GenNonPromotions<kColor, kRook>(from, non_promotions, stack);
    }
  });

  // 龍の王手
  pos.pieces(kColor, kDragon).ForEach([&](Square from) {
    if (max_attacks_bb(kBlackDragon, from).test(dragon_target)) {
      Bitboard attacks        = pos.AttacksFrom<kColor, kDragon>(from);
      Bitboard non_promotions = attacks & dragon_target;
      stack = GenNonPromotions<kColor, kDragon>(from, non_promotions, stack);
    }
  });

  // 駒打の王手
  stack = GenDropChecks<kColor, kPawn  >(pos, ksq, stack);
  stack = GenDropChecks<kColor, kLance >(pos, ksq, stack);
  stack = GenDropChecks<kColor, kKnight>(pos, ksq, stack);
  stack = GenDropChecks<kColor, kSilver>(pos, ksq, stack);
  stack = GenDropChecks<kColor, kGold  >(pos, ksq, stack);
  if (pos.hand(kColor).has(kBishop)) {
    Bitboard checks = bishop_target.andnot(pos.pieces());
    checks.Serialize([&](Square to) {
      (stack++)->move = Move(kColor, kBishop, to);
    });
  }
  if (pos.hand(kColor).has(kRook)) {
    Bitboard checks = rook_target.andnot(pos.pieces());
    checks.Serialize([&](Square to) {
      (stack++)->move = Move(kColor, kRook, to);
    });
  }

  return stack;
}

/**
 * 近接王手をすべて生成します.
 * ここで近接王手とは、合駒ができない位置への王手のことです。
 */
template<Color kColor>
ExtMove* Generator<kAdjacentChecks, kColor>::GenerateMoves(const Position& pos,
                                                           ExtMove* stack) {
  assert(!pos.in_check());
  assert(stack != nullptr);

  ExtMove* const stack_begin = stack;
  const Square ksq = pos.king_square(~kColor);

  // 動かす手の王手を生成する
  stack = GenAdjacentDirectChecks<kColor, kPawn  >(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kLance >(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kKnight>(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kSilver>(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kGold  >(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kBishop>(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kRook  >(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kHorse >(pos, ksq, stack);
  stack = GenAdjacentDirectChecks<kColor, kDragon>(pos, ksq, stack);

  // 取った駒をセットする
  for (ExtMove* it = stack_begin; it != stack; ++it) {
    it->move.set_captured_piece(pos.piece_on(it->move.to()));
  }

  // 駒打の王手を生成する
  stack = GenAdjacentDropChecks<kColor, kPawn  >(pos, ksq, stack);
  stack = GenAdjacentDropChecks<kColor, kLance >(pos, ksq, stack);
  stack = GenAdjacentDropChecks<kColor, kKnight>(pos, ksq, stack);
  stack = GenAdjacentDropChecks<kColor, kSilver>(pos, ksq, stack);
  stack = GenAdjacentDropChecks<kColor, kGold  >(pos, ksq, stack);
  stack = GenAdjacentDropChecks<kColor, kBishop>(pos, ksq, stack);
  stack = GenAdjacentDropChecks<kColor, kRook  >(pos, ksq, stack);

  return stack;
}

template<Color kColor>
struct Generator<kNonEvasions, kColor> {
  static ExtMove* GenerateMoves(const Position& pos, ExtMove* stack) {
    stack = Generator<kCaptures, kColor>::GenerateMoves(pos, stack);
    stack = Generator<kQuiets, kColor>::GenerateMoves(pos, stack);
    return stack;
  }
};

template<Color kColor>
struct Generator<kAllMoves, kColor> {
  static ExtMove* GenerateMoves(const Position& pos, ExtMove* stack) {
    return pos.in_check()
         ? Generator<kEvasions, kColor>::GenerateMoves(pos, stack)
         : Generator<kNonEvasions, kColor>::GenerateMoves(pos, stack);
  }
};

} // namespace

/**
 * 指定されたカテゴリの指し手を生成します.
 */
template<GeneratorType kGt>
ExtMove* GenerateMoves(const Position& pos, ExtMove* stack) {
  return pos.side_to_move() == kBlack
       ? Generator<kGt, kBlack>::GenerateMoves(pos, stack)
       : Generator<kGt, kWhite>::GenerateMoves(pos, stack);
}

// Explicit Template Specializations
template ExtMove* GenerateMoves<kRecaptures    >(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kCaptures      >(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kQuiets        >(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kEvasions      >(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kChecks        >(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kQuietChecks   >(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kAdjacentChecks>(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kNonEvasions   >(const Position&, ExtMove*);
template ExtMove* GenerateMoves<kAllMoves      >(const Position&, ExtMove*);

/**
 * 特定のマスに駒を動かす手を生成します.
 * この関数は、玉を動かす手を生成しないことに注意してください。
 * なお、この関数は、mate3.cc で利用されています。
 */
ExtMove* GenerateMovesTo(const Position& pos, Square to, ExtMove* stack) {
  return pos.side_to_move() == kBlack
       ? GenMovesTo<kBlack>(pos, to, stack)
       : GenMovesTo<kWhite>(pos, to, stack);
}

/**
 * 非合法手を取り除く.
 */
ExtMove* RemoveIllegalMoves(const Position& pos, ExtMove* begin, ExtMove* end) {
  return std::remove_if(begin, end, [&](const ExtMove& ext_move) {
    return !pos.PseudoLegalMoveIsLegal(ext_move.move);
  });
}
