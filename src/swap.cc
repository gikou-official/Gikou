/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
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

#include "swap.h"

#include "common/array.h"
#include "material.h"
#include "position.h"

namespace {

template<PieceType PT>
inline bool AttackerIsFound(const Position& pos, Bitboard attackers,
                            Square* const attacker_sq) {
  assert(attacker_sq != nullptr);
  if (attackers.test(pos.pieces(PT))) {
    *attacker_sq = (attackers & pos.pieces(PT)).first_one();
    return true;
  }
  return false;
}

PieceType FindLeastValuableAttacker(const Position& pos, Bitboard pieces,
                                    Square* const attacker_sq) {
  assert(pieces.any());
  if (AttackerIsFound<kPawn   >(pos, pieces, attacker_sq)) return kPawn   ;
  if (AttackerIsFound<kLance  >(pos, pieces, attacker_sq)) return kLance  ;
  if (AttackerIsFound<kKnight >(pos, pieces, attacker_sq)) return kKnight ;
  if (AttackerIsFound<kPPawn  >(pos, pieces, attacker_sq)) return kPPawn  ;
  if (AttackerIsFound<kPLance >(pos, pieces, attacker_sq)) return kPLance ;
  if (AttackerIsFound<kSilver >(pos, pieces, attacker_sq)) return kSilver ;
  if (AttackerIsFound<kPKnight>(pos, pieces, attacker_sq)) return kPKnight;
  if (AttackerIsFound<kPSilver>(pos, pieces, attacker_sq)) return kPSilver;
  if (AttackerIsFound<kGold   >(pos, pieces, attacker_sq)) return kGold   ;
  if (AttackerIsFound<kBishop >(pos, pieces, attacker_sq)) return kBishop ;
  if (AttackerIsFound<kRook   >(pos, pieces, attacker_sq)) return kRook   ;
  if (AttackerIsFound<kHorse  >(pos, pieces, attacker_sq)) return kHorse  ;
  if (AttackerIsFound<kDragon >(pos, pieces, attacker_sq)) return kDragon ;
  return kKing;
}

/**
 * GlobalSwap内で用いられる、局面データです.
 * 必要最低限のデータに絞ることで、PositionクラスよりもMakeMoveが高速になっています。
 */
class MinimumPos : public ExtendedBoard {
 public:
  MinimumPos(const Position& pos)
      : ExtendedBoard(pos.extended_board()),
        color_bb_(pos.color_bb()),
        type_bb_(pos.type_bb()),
        occupied_bb_(pos.pieces()),
        side_to_move_(pos.side_to_move()) {
  }

  void MakeMove(Move move) {
    // 1. 将棋盤を更新
    if (move.is_drop()) {
      Square to = move.to();
      occupied_bb_.set(to);
      color_bb_[side_to_move_].set(to);
      type_bb_[move.piece_type()].set(to);
      MakeDropMove(move);
    } else if (move.is_capture()) {
      Square from = move.from(), to = move.to();
      occupied_bb_.reset(from);
      color_bb_[side_to_move_].reset(from).set(to);
      color_bb_[~side_to_move_].reset(to);
      type_bb_[move.captured_piece_type()].reset(to);
      type_bb_[move.piece_type()].reset(from);
      type_bb_[move.piece_type_after_move()].set(to);
      MakeCaptureMove(move);
    } else {
      Square from = move.from(), to = move.to();
      occupied_bb_.reset(from).set(to);
      color_bb_[side_to_move_].reset(from).set(to);
      type_bb_[move.piece_type()].reset(from);
      type_bb_[move.piece_type_after_move()].set(to);
      MakeNonCaptureMove(move);
    }

    // 2. 手番を更新
    side_to_move_ = ~side_to_move_;
  }

  Square FindMostValuablePiece(Bitboard pieces) const {
    return FindPiece<true>(pieces);
  }

  Square FindLeastValuablePiece(Bitboard pieces) const {
    return FindPiece<false>(pieces);
  }

  Bitboard AttackersTo(Square to) const {
    Bitboard black = color_bb_[kBlack];
    Bitboard white = color_bb_[kWhite];
    Bitboard hdk = type_bb(kHorse, kDragon, kKing);
    Bitboard rd  = type_bb(kRook, kDragon);
    Bitboard bh  = type_bb(kBishop, kHorse);
    Bitboard golds = golds_bb();
    Bitboard occ = occupied_bb_;
    return (attackers_to<kBlack, kKing  >(to, occ) & hdk                     )
         | (attackers_to<kBlack, kRook  >(to, occ) & rd                      )
         | (attackers_to<kBlack, kBishop>(to, occ) & bh                      )
         | (attackers_to<kBlack, kGold  >(to, occ) & golds            & black)
         | (attackers_to<kWhite, kGold  >(to, occ) & golds            & white)
         | (attackers_to<kBlack, kSilver>(to, occ) & type_bb(kSilver) & black)
         | (attackers_to<kWhite, kSilver>(to, occ) & type_bb(kSilver) & white)
         | (attackers_to<kBlack, kKnight>(to, occ) & type_bb(kKnight) & black)
         | (attackers_to<kWhite, kKnight>(to, occ) & type_bb(kKnight) & white)
         | (attackers_to<kBlack, kLance >(to, occ) & type_bb(kLance ) & black)
         | (attackers_to<kWhite, kLance >(to, occ) & type_bb(kLance ) & white)
         | (attackers_to<kBlack, kPawn  >(to, occ) & type_bb(kPawn  ) & black)
         | (attackers_to<kWhite, kPawn  >(to, occ) & type_bb(kPawn  ) & white);
  }

  Bitboard pieces(Color c) const {
    return color_bb_[c];
  }

  Bitboard pieces() const {
    return occupied_bb_;
  }

  Color side_to_move() const {
    return side_to_move_;
  }

 private:
  template<bool kMost>
  Square FindPiece(Bitboard pieces) const {
    assert(pieces.any());

    Square best_piece_square;
    PieceType best_piece_type;
    int best_piece_value = kMost ? -kScoreInfinite : kScoreInfinite;

    pieces.ForEach([&](Square sq) {
      PieceType piece_type = piece_on(sq).type();
      int piece_value = piece_type == kKing ? 9999 : Material::exchange_value(piece_type);
      // 最も価値の高い駒 or 最も価値の低い駒を更新する
      if (kMost ? (piece_value > best_piece_value) : (piece_value < best_piece_value)) {
        best_piece_square = sq;
        best_piece_type   = piece_type;
        best_piece_value  = piece_value;
      }
    });

    return best_piece_square;
  }

  Bitboard golds_bb() const {
    return type_bb(kGold, kPPawn, kPLance, kPKnight, kPSilver);
  }

  Bitboard type_bb(PieceType pt) const {
    return type_bb_[pt];
  }

  template<typename ...Args>
  Bitboard type_bb(PieceType pt1, Args... pt2) const {
    return type_bb_[pt1] | type_bb(pt2...);
  }

  ArrayMap<Bitboard, Color> color_bb_;
  ArrayMap<Bitboard, PieceType> type_bb_;
  Bitboard occupied_bb_;
  Color side_to_move_;
};

} // namespace

Score Swap::Evaluate(const Move move, const Position& pos) {
//  assert(pos.MoveIsPseudoLegal(move));

  Array<Score, 40> gain;
  const Square to = move.to();
  Square from = move.is_drop() ? move.to() : move.from();

  // 最初の１手について、駒割りの増分を求める
  gain[0] = Material::exchange_value(move.captured_piece_type());
  if (move.is_promotion()) {
    gain[0] += Material::promotion_value(move.piece_type());
  }

  // 移動先のマスに利いている相手の駒を求める
  Color stm = ~pos.side_to_move();
  Bitboard occ = pos.pieces().andnot(square_bb(from));
  Bitboard attackers = pos.AttackersTo(to, occ);
  Bitboard stm_attackers = attackers & pos.pieces(stm);

  // 移動先のマスに相手の駒が利いていなければ、直ちにリターンする
  // TODO 利きデータを使って高速化
  if (stm_attackers.none()) {
    return gain[0];
  }

  int depth = 1;
  PieceType captured = move.piece_type_after_move();

  // 取る手が存在しなくなるまで、取り合いを続ける
  while (true) {
    assert(occ.any());
    assert(stm_attackers.any());

    gain[depth] = Material::exchange_value(captured) - gain[depth - 1];

    // 最も安い駒を見つける
    captured = FindLeastValuableAttacker(pos, stm_attackers, &from);

    // 成ることができる場合は、必ず成ると仮定する
    if (   IsPromotablePieceType(captured)
        && promotion_zone_bb(stm).test(square_bb(from) | square_bb(to))) {
      gain[depth] += Material::promotion_value(captured);
      captured = GetPromotedType(captured);
    }

    // 指し手に沿って、将棋盤を動かす
    ++depth;
    occ.reset(from);
    stm = ~stm;
    attackers |= pos.SlidersAttackingTo(to, occ);
    attackers &= occ;
    stm_attackers = attackers & pos.pieces(stm);

    // 取る手がなくなったら、終了する
    if (stm_attackers.none()) {
      break;
    }

    // 相手の玉を取る手を指してしまう前に、終了する
    if (captured == kKing) {
      gain[depth++] = static_cast<Score>(9999);
      break;
    }
  }

  // ミニマックス計算をして、SEE値を求める
  while (--depth) {
    gain[depth - 1] = std::min(-gain[depth], gain[depth - 1]);
  }

  return gain[0];
}

bool Swap::IsWinning(Move move, const Position& pos) {
  Score gain = Material::value(move.captured_piece_type());
  Score loss = Material::value(move.piece_type());
  Color stm = pos.side_to_move();
  bool opponent_can_promote = move.to().is_promotion_zone_of(~stm);
  if (gain > loss && !opponent_can_promote) {
    return true;
  }
  return Evaluate(move, pos) > kScoreZero;
}

bool Swap::IsLosing(Move move, const Position& pos) {
  Score gain = Material::value(move.captured_piece_type());
  Score loss = Material::value(move.piece_type());
  Color stm = pos.side_to_move();
  bool opponent_can_promote = move.to().is_promotion_zone_of(~stm);
  if (gain >= loss && !opponent_can_promote) {
    return false;
  }
  return Evaluate(move, pos) < kScoreZero;
}

Score Swap::EvaluateGlobalSwap(const Move move, const Position& pos,
                               const int depth_limit) {
  assert(pos.MoveIsPseudoLegal(move));
  assert(depth_limit <= 40);

  auto get_material_gain = [](Move m) -> Score {
    Score gain = Material::exchange_value(m.captured_piece_type());
    if (m.is_promotion()) {
      gain += Material::promotion_value(m.piece_type());
    }
    return gain;
  };

  Array<Score, 40> gain;

  // 必要最低限の盤面データを準備する
  MinimumPos min_pos(pos);

  // 最初の１手について、駒割りの増分を求める
  gain[0] = get_material_gain(move);

  // 指し手に沿って局面を進める
  min_pos.MakeMove(move);

  int depth;
  for (depth = 1; depth < depth_limit; ++depth) {
    Color stm = min_pos.side_to_move();

    // 取れる相手駒がないか調べる
    Bitboard targets = min_pos.pieces(~stm) & min_pos.GetControlledSquares(stm);
    if (targets.none()) {
      break;
    }

    // 取れる相手駒のうち、最も高い駒を探す
    Square victim_sq = min_pos.FindMostValuablePiece(targets);
    Piece victim = min_pos.piece_on(victim_sq);
    if (victim.is(kKing)) {
      gain[depth++] = static_cast<Score>(9999);
      break;
    }

    // 最も高い相手駒を取ることができる味方駒を列挙する
    Bitboard attacker_candidates = min_pos.AttackersTo(victim_sq) & min_pos.pieces(stm);
    assert(attacker_candidates.any());

    // 最も高い相手駒を取ることができる味方駒のうち、最も安い駒を探す
    Square attacker_sq = min_pos.FindLeastValuablePiece(attacker_candidates);
    Piece attacker = min_pos.piece_on(attacker_sq);

    // 「最も高い駒を、最も安い駒で取る手」を生成する（成れる場合は必ず成る）
    Square from = attacker_sq, to = victim_sq;
    bool promotion =   attacker.can_promote()
                    && promotion_zone_bb(stm).test(square_bb(from) | square_bb(to));
    Move best_capture(attacker, from, to, promotion, victim);

    // 駒割りの増分を求める
    gain[depth] = get_material_gain(best_capture) - gain[depth - 1];

    // 生成された手に沿って局面を進める
    min_pos.MakeMove(best_capture);
  }

  // ミニマックス計算をして、 盤面全体の駒交換の損得（Global SEE値）を求める
  while (--depth > 0) {
    gain[depth - 1] = std::min(-gain[depth], gain[depth - 1]);
  }

  return gain[0];
}
