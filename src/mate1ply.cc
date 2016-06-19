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

#include "mate1ply.h"

#include "common/arraymap.h"
#include "position.h"

namespace {

// trueであれば、１手詰関数実行時にデバッグメッセージを表示します
constexpr bool kEnableDebugMessage = false;

/**
 * 駒打による１手詰の候補を表すクラスです.
 *
 * どの方向から打てば相手玉が詰む可能性があるか否かを、各駒種ごとに、DirectionSetで保持しています。
 *
 * このクラスを用いることで、
 *     7種類の駒 * 8方向 = 54通り
 * について、駒打による１手詰の可否を判定することができます。
 */
class MateCandidates {
 public:
  explicit MateCandidates(uint64_t q = 0)
      : qword_(q) {
  }

  /**
   * 全ての駒の種類について、同一の初期値 ds で初期化を行います.
   */
  explicit MateCandidates(DirectionSet ds)
      : qword_(static_cast<uint64_t>(ds) * UINT64_C(0x0101010101010101)) {
  }

  /**
   * 左辺と右辺の論理積を取ります.
   */
  MateCandidates operator&(MateCandidates rhs) const {
    return MateCandidates(qword_ & rhs.qword_);
  }

  /**
   * 駒種 pt について、駒打による１手詰が可能な方向をビットセットで取得します.
   */
  DirectionSet operator[](PieceType pt) const {
    assert(0 <= pt && pt < 8);
    return DirectionSet((qword_ >> (pt * 8)) & UINT64_C(0xff));
  }

  /**
   * 駒打による１手詰の候補が１つでも存在すれば、trueを返します.
   */
  bool any() const {
    return qword_ != 0;
  }

  /**
   * 駒打による１手詰の候補が存在しなければ、trueを返します.
   */
  bool none() const {
    return !any();
  }

  /**
   * 駒 pt を打ったときに１手詰の可能性がある場合、trueを返します.
   */
  bool contains(PieceType pt) const {
    assert(0 <= pt && pt < 8);
    return qword_ & (UINT64_C(0xff) << (pt * 8));
  }

  /**
   * 駒種 pt について、１手詰の候補をすべてリセットします.
   */
  void reset(PieceType pt) {
    assert(0 <= pt && pt < 8);
    qword_ &= ~(UINT64_C(0xff) << (pt * 8));
  }

  /**
   * 駒種 pt について、１手詰の候補をセットします.
   */
  void set(PieceType pt, DirectionSet ds) {
    assert(0 <= pt && pt < 8);
    reset(pt);
    qword_ |= static_cast<uint64_t>(ds) << (pt * 8);
  }

 private:
  uint64_t qword_;
};

/**
 * 駒打による１手詰の候補.
 *
 * 配列の添字の順番は、 [攻め方の手番][駒を打つ候補のマス][玉が移動可能なマス] です。
 *
 * (1)駒を打つ候補のマスと、(2)玉が移動可能なマスの組み合わせに関して、(1)のどこかに駒打を行い(2)を
 * 塞ぐのに必要な種類をあらかじめ求めてこのテーブルに保存しておきます。
 * そして、１手詰の判定時には、このテーブルと持ち駒を比較して、１手で詰ますために必要な持駒の種類を
 * 特定します。
 *
 * （参考文献）
 *   - 金子知適, et al.: 新規節点で固定深さの探索を併用するdf-pnアルゴリズム,
 *     第10回ゲームプログラミングワークショップ, pp.1-8, 2005.
 */
ArrayMap<MateCandidates, Color, DirectionSet, DirectionSet> g_drop_candidates;

/**
 * 移動手が１手詰になっているかを調べ、その手が１手詰になっていれば、trueを返します.
 * @param kColor       攻め方の手番
 * @param kPt          動かす駒の種類
 * @param kIsPromotion 成る手であれば、true
 * @param from         駒の移動元
 * @param to           駒の移動先
 * @param attack1      攻め方の利きが１つ以上あるマスのある方向
 * @param attacks      動かす駒の、移動元における利き
 * @return 与えられた手が１手詰の手であれば、true
 */
template<Color kColor, PieceType kPt, bool kIsPromotion>
FORCE_INLINE bool TestMoveGivesMate(const Square from, const Square to,
                                    const Position& pos,
                                    const DirectionSet attack1,
                                    const Bitboard attacks) {
  constexpr PieceType kPtAfterMove = kIsPromotion ? GetPromotedType(kPt) : kPt;
  const Square ksq = pos.king_square(~kColor);
  const Bitboard occ = pos.pieces().andnot(square_bb(ksq));
  const DirectionSet long_controls = pos.long_controls(kColor, from);

  if (kEnableDebugMessage) {
    std::printf("test move: %s\n",
                Move(kColor, kPt, from, to, kIsPromotion).ToSfen().c_str());
  }

  //
  // Step 1. 簡易チェック
  //
  if (true) {
    // 攻め方の利きの上限を求める
    Bitboard max_attacks = max_attacks_bb(Piece(kColor, kPtAfterMove), to);
    max_attacks |= direction_bb(ksq, attack1);
    if (long_controls.any()) { // TODO speed up
      max_attacks |= max_attacks_bb(kBlackRook  , to);
      max_attacks |= max_attacks_bb(kBlackBishop, to);
    }

    // 受け方の玉の逃げ道を、攻め方の利きによって塞ぐことができなければ、この手では詰まない
    Bitboard min_evasions = neighborhood8_bb(ksq).andnot(pos.pieces(~kColor) | max_attacks);
    if (min_evasions.any()) {
      return false;
    }

    if (kEnableDebugMessage) {
      std::printf("passed simple test.\n");
    }
  }

  //
  // Step 2. 詳細なチェック
  //

  Bitboard from_bb = square_bb(from);
  Bitboard to_bb   = square_bb(to);
  Bitboard new_occ = occ.andnot(from_bb) | to_bb;

  // 条件 1. 移動先のマスに、受け方の利きがないこと
  if (kPt == kKnight) {
    if (pos.AttackersTo<~kColor>(to, new_occ).any()) {
      return false;
    }
  } else {
    // 飛び駒以外の駒の利きは、すでにStep 1.で考慮されているので、ここではチェックしなくてよい
    if (pos.SlidersAttackingTo(to, new_occ, ~kColor).any()) {
      return false;
    }
  }
  if (kEnableDebugMessage) {
    std::printf("passed condition 1.\n");
  }

  // 条件 2. 受け方の玉が逃げられるマスがないこと
  Bitboard old_short_attacks = attacks;
  if (Piece(kColor, kPt).is_slider()) {
    old_short_attacks &= neighborhood8_bb(from);
  }
  Bitboard min_attacks = attacks_from<kColor, kPtAfterMove>(to, new_occ) | to_bb;
  Bitboard max_evasions = neighborhood8_bb(ksq).andnot(min_attacks | pos.pieces(~kColor));
  while (max_evasions.any()) {
    Square king_to = max_evasions.pop_first_one();
    // 移動元のマスからは、攻め方の駒がいなくなってしまうことに注意
    if (pos.AttackersTo<kColor>(king_to, new_occ).andnot(from_bb).none()) {
      return false;
    }
  }
  if (kEnableDebugMessage) {
    std::printf("passed condition 2.\n");
  }

  // 条件 3. 指し手が合法手であること
  if (   pos.pinned_pieces().test(from)
      && !line_bb(from, pos.king_square(kColor)).test(to)) {
    return false;
  }
  if (kEnableDebugMessage) {
    std::printf("passed condition 3.\n");
  }

  // 以上の条件をすべて満たしていたら、この指し手により受け方の玉は詰む
  return true;
}

/**
 * １手詰となる移動手を探し、１手詰となる移動手が見つかればtrueを返します.
 * @param kColor       攻め方の手番
 * @param kPt          動かす駒の種類
 * @param pos          １手詰の有無を調べたい局面
 * @param attack1      攻め方の利きが１つ以上あるマスのある方向
 * @param move_targets 駒を動かす候補のマス
 * @param mate_move    １手詰となる手（あれば）
 * @return １手詰の手が見つかれば、true
 */
template<Color kColor, PieceType kPt>
FORCE_INLINE bool FindMateMove(const Position& pos, const DirectionSet attack1,
                               Bitboard move_targets, Move* const mate_move) {
  static_assert(kPt <= kRook || kHorse <= kPt, "");
  assert(mate_move != nullptr);

  constexpr Piece kPiece(kColor, kPt);
  constexpr PieceType kPromoted = GetPromotedType(kPt);
  const Square ksq = pos.king_square(~kColor);

  // Step 1. そもそも近接王手できる駒があるのか否かを確認する
  Bitboard pieces = kPt == kGold ? pos.golds(kColor) : pos.pieces(kColor, kPt);
  Bitboard candidates = adjacent_check_candidates_bb(kPiece, ksq);
  if (!pieces.test(candidates)) {
    return false;
  }

  // Step 2. 攻め方が近接王手できるマスを調べる
  Bitboard promotion_target = min_attacks_bb(Piece(~kColor, kPromoted), ksq);
  Bitboard non_promotion_target = min_attacks_bb(Piece(~kColor, kPt), ksq);
  promotion_target &= move_targets;
  non_promotion_target &= (kPt == kKnight ? ~pos.pieces(kColor) : move_targets);

  // Step 3. 近接王手を生成し、１手ずつ、詰みか否かを調べる
  Bitboard checker_candidates = pieces & candidates;
  do {
    Square from = checker_candidates.pop_first_one();
    Bitboard attacks = pos.AttacksFrom<kColor, kPt>(from);

    // 1. 成る王手を調べる
    if (kPiece.can_promote()) {
      Bitboard promotion_checks = attacks & promotion_target;

      // 歩・香・桂: 成る手の場合、移動先が敵陣でなければならない（後ろに下がれない駒なので）
      // それ以外の駒: 成る手の場合、移動元または移動先が敵陣でなければならない
      if (   (kPt == kPawn || kPt == kLance || kPt == kKnight)
          || !from.is_promotion_zone_of(kColor)) {
        promotion_checks &= rank_bb<kColor, 1, 3>();
      }

      // それぞれの王手で詰むかを調べる
      while (promotion_checks.any()) {
        Square to = promotion_checks.pop_first_one();
        if (TestMoveGivesMate<kColor, kPt, true>(from, to, pos, attack1, attacks)) {
          Piece piece_captured = pos.piece_on(to);
          *mate_move = Move(kPiece, from, to, true, piece_captured);
          assert(pos.MoveIsLegal(*mate_move));
          return true;
        }
      }
    }

    // 2. 不成の王手を調べる
    {
      Bitboard non_promotion_checks = attacks & non_promotion_target;

      // 非合法手や、損な不成を取り除く
      if (kPt == kPawn || kPt == kBishop || kPt == kRook) {
        non_promotion_checks &= rank_bb<kColor, 4, 9>();
      } else if (kPt == kLance || kPt == kKnight) {
        non_promotion_checks &= rank_bb<kColor, 3, 9>();
      }

      // それぞれの王手で詰むかを調べる
      while (non_promotion_checks.any()) {
        Square to = non_promotion_checks.pop_first_one();
        if (TestMoveGivesMate<kColor, kPt, false>(from, to, pos, attack1, attacks)) {
          Piece piece_moved = kPt == kGold ? pos.piece_on(from) : kPiece;
          Piece piece_captured = pos.piece_on(to);
          *mate_move = Move(piece_moved, from, to, false, piece_captured);
          assert(pos.MoveIsLegal(*mate_move));
          return true;
        }
      }
    }
  } while (checker_candidates.any());

  return false;
}

/**
 * 駒打が１手詰になっているかを調べ、その手が１手詰になっていれば、trueを返します.
 * @param kColor 攻め方の手番
 * @param kPt    打つ駒の種類
 * @param to     駒を打つマス
 * @return 与えられた手が１手詰の手であれば、true
 */
template<Color kColor, PieceType kPt>
FORCE_INLINE bool TestDropGivesMate(const Position& pos, const Square to) {
  static_assert(IsDroppablePieceType(kPt) && kPt != kPawn, "");

  if (kEnableDebugMessage) {
    std::printf("test drop: %s\n", Move(kColor, kPt, to).ToSfen().c_str());
  }

  const DirectionSet long_controls = pos.long_controls(kColor, to);

  // 1. もし、攻め方が打った駒が攻め方の長い利きを遮らなかった場合、この王手は１手詰みと直ちに判定してよい。
  //    事前計算されたテーブル（g_drop_candidates）により、受け方の玉が逃げられないことは確認済みなので。
  if (kPt != kKnight && long_controls.none()) {
    return true;
  }

  const Square ksq = pos.king_square(~kColor);
  const Bitboard new_occ = pos.pieces().andnot(square_bb(ksq)) | square_bb(to);

  // 2. 攻め方が駒を打つマスに受け方の利きがある場合、１手詰みではないと判定する。
  //    桂馬以外を打つ手については、すでに確認済みなので、ここで別途調べる必要はない。
  if (kPt == kKnight && pos.AttackersTo<~kColor>(to, new_occ).any()) {
    return false;
  }

  // 3. 受け方の玉の逃げ道があるか否かを調べる
  //    もし、１マスでも受け方の玉が逃げられるマスがあれば、その手では詰まないことになる
  // TODO 高速化
  Bitboard blocked_attacks = max_attacks_bb(kBlackBishop, to) | max_attacks_bb(kBlackRook, to);
  Bitboard min_attacks = attacks_from<kColor, kPt>(to, new_occ) | square_bb(to);
  Bitboard king_moves = neighborhood8_bb(ksq).andnot(min_attacks | pos.pieces(~kColor));
  Bitboard possible_evasions = king_moves & blocked_attacks;
  while (possible_evasions.any()) {
    Square king_to = possible_evasions.pop_first_one();
    if (pos.AttackersTo(king_to, new_occ, kColor).none()) {
      return false;
    }
  }

  return true;
}

/**
 * １手詰となる駒打を探し、１手詰となる駒打が見つかればtrueを返します.
 * @param kColor    攻め方の手番
 * @param kPt       打つ駒の種類
 * @param pos       １手詰の有無を調べたい局面
 * @param mc        駒打による１手詰の候補
 * @param mate_move １手詰となる手（あれば）
 * @return １手詰の手が見つかれば、true
 */
template<Color kColor, PieceType kPt>
FORCE_INLINE bool FindMateDrop(const Position& pos, const MateCandidates& mc,
                               Move* const mate_move) {
  static_assert(IsDroppablePieceType(kPt) && kPt != kPawn, "");

  const Hand hand = pos.hand(kColor);

  // 指定された種類の持ち駒を持っていない場合は、リターンする
  if (!hand.has(kPt)) {
    return false;
  }

  // 駒打を打つ候補のマスが存在しなければ、リターンする
  if (!mc.contains(kPt)) {
    return false;
  }

  // 持ち駒の間に優越関係があれば、一部の王手は調べなくてよい
  if (kPt == kLance && hand.has(kRook)) {
    return false;
  }
  if (kPt == kSilver && hand.has(kBishop) && hand.has(kGold)) {
    return false;
  }

  const Square ksq = pos.king_square(~kColor);
  const Bitboard occ = pos.pieces().andnot(square_bb(ksq));
  Bitboard target = kPt == kKnight
                  ? attackers_to<kColor, kKnight>(ksq, occ).andnot(occ)
                  : direction_bb(ksq, mc[kPt]);

  // 優越関係を利用して、できる限り駒を打つ候補のマスを絞り込む
  if (kPt == kGold && hand.has(kRook)) {
    Bitboard omittable = step_attacks_bb(Piece(kColor, kPawn), ksq);
    target = target.andnot(omittable);
  }
  if (kPt == kSilver) {
    if (hand.has(kGold)) {
      Bitboard omittable = step_attacks_bb(Piece(~kColor, kSilver), ksq)
                         & step_attacks_bb(Piece(~kColor, kGold  ), ksq);
      target = target.andnot(omittable);
    } else if (hand.has(kBishop)) {
      Bitboard ommitable = step_attacks_bb(Piece(kColor, kSilver), ksq)
                         & step_attacks_bb(Piece(kColor, kGold  ), ksq);
      target = target.andnot(ommitable);
    }
  }

  // 駒打が詰みか否かを１手ずつ調べる
  while (target.any()) {
    Square to = target.pop_first_one();
    if (TestDropGivesMate<kColor, kPt>(pos, to)) {
      *mate_move = Move(kColor, kPt, to);
      return true;
    }
  }

  return false;
}

template<Color kColor>
bool IsMateInOnePly(const Position& pos, Move* const mate_move) {
  assert(!pos.in_check());
  assert(mate_move != nullptr);

  const Square ksq = pos.king_square(~kColor);
  const ExtendedBoard& eb = pos.extended_board();
  EightNeighborhoods neighborhood_attacks  = eb.GetEightNeighborhoodControls( kColor, ksq);
  EightNeighborhoods neighborhood_defenses = eb.GetEightNeighborhoodControls(~kColor, ksq);

  DirectionSet attack1 = neighborhood_attacks.more_than(0);
  DirectionSet attack2 = neighborhood_attacks.more_than(1);
  DirectionSet defense = neighborhood_defenses.more_than(1);
  DirectionSet on_board = neighborhood8_bb(ksq).neighborhood8(ksq);
  DirectionSet own_pieces = pos.pieces(kColor).neighborhood8(ksq);
  DirectionSet move_targets = on_board & attack2 & ~(defense | own_pieces);

  // Step 1. 動かす手で１手詰とならないかを調べる
  //         証明駒を最小にするために、駒打よりも動かす手を優先的に調べる
  Bitboard target_bb = direction_bb(ksq, move_targets);
  if (move_targets.any()) {
    if (FindMateMove<kColor, kPawn  >(pos, attack1, target_bb, mate_move)) return true;
    if (FindMateMove<kColor, kLance >(pos, attack1, target_bb, mate_move)) return true;
    if (FindMateMove<kColor, kSilver>(pos, attack1, target_bb, mate_move)) return true;
    if (FindMateMove<kColor, kGold  >(pos, attack1, target_bb, mate_move)) return true;
    if (FindMateMove<kColor, kBishop>(pos, attack1, target_bb, mate_move)) return true;
    if (FindMateMove<kColor, kRook  >(pos, attack1, target_bb, mate_move)) return true;
    if (FindMateMove<kColor, kHorse >(pos, attack1, target_bb, mate_move)) return true;
    if (FindMateMove<kColor, kDragon>(pos, attack1, target_bb, mate_move)) return true;
  }
  if (FindMateMove<kColor, kKnight>(pos, attack1, target_bb, mate_move)) {
    return true;
  }

  // Step 2. 駒打で１手詰とならないかを調べる
  if (pos.hand(kColor).has_any_piece_except(kPawn)) {
    DirectionSet occ = pos.pieces().neighborhood8(ksq);
    DirectionSet drop_targets = on_board & attack1 & ~(defense | occ);
    DirectionSet evasions = on_board & ~attack1 & (~occ | own_pieces);
    MateCandidates mc = g_drop_candidates[kColor][drop_targets][evasions];
    if (mc.any()) {
      if (FindMateDrop<kColor, kLance >(pos, mc, mate_move)) return true;
      if (FindMateDrop<kColor, kKnight>(pos, mc, mate_move)) return true;
      if (FindMateDrop<kColor, kSilver>(pos, mc, mate_move)) return true;
      if (FindMateDrop<kColor, kGold  >(pos, mc, mate_move)) return true;
      if (FindMateDrop<kColor, kBishop>(pos, mc, mate_move)) return true;
      if (FindMateDrop<kColor, kRook  >(pos, mc, mate_move)) return true;
    }
  }

  return false;
}

}  // namespace

bool IsMateInOnePly(const Position& pos, Move* const mate_move) {
  // そもそも、受け方の玉が存在しなければ、詰まされることはない
  if (!pos.king_exists(~pos.side_to_move())) {
    return false;
  }

  // １手詰関数の実装を呼び出す
  if (pos.side_to_move() == kBlack) {
    return IsMateInOnePly<kBlack>(pos, mate_move);
  } else {
    return IsMateInOnePly<kWhite>(pos, mate_move);
  }
}

void InitMateInOnePly() {
  const Square king_square(kFile5, kRank5);

  // g_drop_candidates のテーブルを予め計算しておく
  for (Color c : { kBlack, kWhite })
    for (int i = 0; i < 256; ++i)
      for (int j = 0; j < 256; ++j) {
        DirectionSet target_ptn(i), evasion_ptn(j);
        Bitboard target_bb = direction_bb(king_square, target_ptn);
        Bitboard evasion_bb = direction_bb(king_square, evasion_ptn);

        // 打ち歩詰めは反則
        g_drop_candidates[c][target_ptn][evasion_ptn].reset(kPawn);

        // 桂打で詰むのは、玉が逃げられるマスが１マスも存在しない場合のみ
        if (evasion_bb.none()) {
          // 桂馬の利きは、周囲８方向への利きではないので、便宜的に、全方向のフラグを代わりに立てておく
          DirectionSet all_set = DirectionSet().set();
          g_drop_candidates[c][target_ptn][evasion_ptn].set(kKnight, all_set);
        }

        // 打った駒の利きにより、受け方の玉の逃げ道をすべて塞ぐことができたら、
        // その駒打の手は、１手詰の候補手とする
        for (PieceType pt : { kLance, kSilver, kGold, kBishop, kRook }) {
          Bitboard drop_candidates;
          target_bb.ForEach([&](Square to) {
            Bitboard attacked = max_attacks_bb(Piece(c, pt), to);
            bool gives_check = attacked.test(king_square);
            bool king_can_evade = evasion_bb.andnot(attacked).any();
            if (gives_check && !king_can_evade) {
              drop_candidates.set(to);
            }
          });
          DirectionSet ds = drop_candidates.neighborhood8(king_square);
          g_drop_candidates[c][target_ptn][evasion_ptn].set(pt, ds);
        }
      }
}
