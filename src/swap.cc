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

} // namespace

Score Swap::Evaluate(const Move move, const Position& pos) {
  assert(pos.MoveIsPseudoLegal(move));

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
