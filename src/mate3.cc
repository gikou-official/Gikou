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

#include "mate3.h"

#include "mate1ply.h"
#include "movegen.h"
#include "position.h"
#include "proofpiece.h"

namespace {

/**
 * ２手以内に手番側の玉が詰まされる場合は、trueを返します.
 */
bool IsMatedInTwoPlies(Position& pos, Mate3Result* result);

} // namespace

bool IsMateInThreePlies(Position& pos, Mate3Result* const result) {
  assert(!pos.in_check());
  assert(result != nullptr);

  // そもそも、受け方の玉が存在しなければ、詰まされることはない
  if (!pos.king_exists(~pos.side_to_move())) {
    return false;
  }

#ifndef NDEBUG
  const auto is_no_mate_in_three_plies = [](Position& p, Move m) -> bool {
    Mate3Result m3r;
    p.MakeMove(m);
    bool mated = IsMatedInTwoPlies(p, &m3r);
    p.UnmakeMove(m);
    return !mated;
  };
#endif

  // 近接王手を生成する
  SimpleMoveList<kAdjacentChecks> adjacent_checks(pos);

  // 近接王手が存在しなければ、ここで終了する
  if (adjacent_checks.size() == 0) {
    return false;
  }

  const Color side_to_move = pos.side_to_move();
  const Square ksq = pos.king_square(~side_to_move);

  for (const ExtMove& ext_move : adjacent_checks) {
    const Move move = ext_move.move;
    assert(pos.MoveGivesCheck(move));

    if (!pos.PseudoLegalMoveIsLegal(move)) {
      continue;
    }

    // Step 1. 受け方の玉で取り返されると１手で詰まなくなる場合は、その王手を枝刈りする
    if (   move.is_drop()
        && neighborhood8_bb(ksq).test(move.to())
        && !pos.square_is_attacked(side_to_move, move.to())) {
      // ひとつのメソッドで、２手分（1.駒打 -> 2.玉による取り返しの手）いっぺんに局面を進める
      // このメソッドを使うと、２回MakeMoveするよりも、少しだけ高速化できる
      pos.MakeDropAndKingRecapture(move);

      // １手詰関数を呼ぶ
      Move dummy_move;
      if (!pos.in_check() && !IsMateInOnePly(pos, &dummy_move)) {
        pos.UnmakeDropAndKingRecapture(move);
        assert(is_no_mate_in_three_plies(pos, move));
        continue; // Prune this check.
      }

      // ２手分局面を戻す
      pos.UnmakeDropAndKingRecapture(move);
    }

    pos.MakeMove(move, true);

    // Step 2. 残り２手で詰むか調べる
    Mate3Result r;
    if (IsMatedInTwoPlies(pos, &r)) {
      pos.UnmakeMove(move);
      // 打ち歩詰め
      if (move.is_pawn_drop() && r.mate_distance == 0) {
        continue;
      }
      // 結果を保存する
      result->mate_move = move;
      result->mate_distance = r.mate_distance + 1;
      result->proof_pieces = ProofPieces::AtAttackSide(r.proof_pieces, move);
      return true;
    }

    pos.UnmakeMove(move);
  }

  return false;
}

namespace {

bool IsMatedInTwoPlies(Position& pos, Mate3Result* const result) {
  assert(pos.in_check());
  assert(result != nullptr);

  int max_mate_distance = 0;
  Hand child_proof_pieces;
  EvasionPicker evasion_picker(pos);

  while (evasion_picker.has_next()) {
    const Move move = evasion_picker.next_move();

    if (!pos.PseudoLegalMoveIsLegal(move)) {
      continue;
    }

    // 受け方の手が逆王手になっている場合は、処理が難しくなるので、一律不詰とする
    if (pos.MoveGivesCheck(move)) {
      return false;
    }

    pos.MakeMove(move, false);

    // 残り１手で詰むか調べる
    Move mate_move;
    if (IsMateInOnePly(pos, &mate_move)) {
      max_mate_distance = 2;
      child_proof_pieces |= ProofPieces::AtFrontier(pos, mate_move);
      pos.UnmakeMove(move);
      continue;
    }

    pos.UnmakeMove(move);

    // 攻め方の王手から逃れる手を見つけたので、２手以内には詰まないことになる
    return false;
  }

  // 結果を保存する
  result->proof_pieces = ProofPieces::AtLeaf(pos) | child_proof_pieces;
  result->mate_distance = max_mate_distance;
  assert(result->mate_distance % 2 == 0);

  return true;
}

} // namespace

EvasionPicker::EvasionPicker(const Position& pos)
    : pos_(pos), stm_(pos.side_to_move()), ksq_(pos.stm_king_square()) {
  assert(pos.in_check());
  // 指し手のスタック・ポインタを初期化する
  cur_ = moves_.begin();
  end_ = moves_.begin();

  // 1. 王手している駒の利きを求めておく（１枚目）
  Bitboard checkers = pos.checkers();
  Square s1 = checkers.pop_first_one();
  attacked_by_checkers_ |= min_attacks_bb(pos.piece_on(s1), s1);

  // 1. 王手している駒の利きを求めておく（２枚目）
  if (pos.num_checkers() == 2) {
    Square s2 = checkers.first_one();
    attacked_by_checkers_ |= min_attacks_bb(pos.piece_on(s2), s2);
  }
}

bool EvasionPicker::GenerateNext() {
  switch (stage_) {
    case kKingCapturesOfChecker: {
      stage_ = kCapturesOfChecker;
      Bitboard target = neighborhood8_bb(ksq_) & pos_.checkers();
      if (target.any()) {
        assert(target.count() == 1);
        Square to = target.first_one();
        if (!attacked_by_checkers_.test(to)) {
          Piece captured = pos_.piece_on(to);
          (end_++)->move = Move(stm_, kKing, ksq_, to, false, captured);
          return true;
        }
      }
    }
    /* no break */

    case kCapturesOfChecker: {
      stage_ = kKingCaptures;
      if (pos_.num_checkers() == 1) {
        Square to = pos_.checkers().first_one();
        end_ = GenerateMovesTo(pos_, to, end_);
        if (cur_ != end_) {
          return true;
        }
      }
    }
    /* no break */

    case kKingCaptures: {
      stage_ = kKingNonCaptures;
      Bitboard target = (neighborhood8_bb(ksq_) & pos_.pieces(~stm_))
                        .andnot(pos_.checkers() | attacked_by_checkers_);
      target.ForEach([&](Square to) {
        Piece captured = pos_.piece_on(to);
        (end_++)->move = Move(stm_, kKing, ksq_, to, false, captured);
      });
      if (cur_ != end_) {
        return true;
      }
    }
    /* no break */

    case kKingNonCaptures: {
      stage_ = kStop;
      Bitboard target = neighborhood8_bb(ksq_).andnot(pos_.pieces());
      target.andnot(attacked_by_checkers_).ForEach([&](Square to) {
        (end_++)->move = Move(stm_, kKing, ksq_, to);
      });
      if (cur_ != end_) {
        return true;
      }
    }
    /* no break */

    case kStop:
      return false;

    default:
      assert(0);
      return false;
  }
}
