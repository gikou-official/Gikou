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

#include "proofpiece.h"

#include "position.h"

namespace {

/**
 * 攻め方の王手に対して、受け方が合駒ができる場合は、trueを返します.
 */
inline bool InterceptionIsPossible(const Position& pos) {
  assert(pos.in_check());

  // 1. ２つの駒から同時に王手されている場合
  if (pos.num_checkers() == 2) {
    return false;
  }

  // 2. １つの駒から王手されている場合
  assert(pos.num_checkers() == 1);
  Square checker_sq = pos.checkers().first_one();
  return between_bb(checker_sq, pos.king_square(pos.side_to_move())).any();
}

/**
 * 攻め方が独占している駒を返します.
 * @param pos         調べたい局面
 * @param attack_side 攻め方の手番
 * @return 攻め方が独占している駒
 */
inline Hand GetMonopolizedPieces(const Position& pos, Color attack_side) {
  return pos.hand(attack_side).GetMonopolizedPieces(pos.hand(~attack_side));
}

/**
 * 攻め方の王手に対して、受け方が合駒で防げる場合は、trueを返します.
 * @param pos  調べたい局面
 * @param move 攻め方の王手
 * @return 合駒で防げる場合はtrue
 */
inline bool CheckIsBlockable(const Position& pos, Move move) {
  assert(pos.MoveGivesCheck(move));

  const Square ksq = pos.king_square(~pos.side_to_move());

  // 1. 駒打の王手
  if (move.is_drop()) {
    return between_bb(ksq, move.to()).any();
  }

  // 2. 開き王手
  if (pos.discovered_check_candidates().test(move.from())) {
    Square from = move.from(), to = move.to();
    // 王手をかけている攻め方の駒の枚数を数える
    int num_checkers = 0;
    if (   max_attacks_bb(pos.piece_on(from), to).test(ksq)
        && (between_bb(to, ksq) & pos.pieces()).none()) {
      num_checkers += 1;
    }
    if (!line_bb(ksq, move.from()).test(move.to())) {
      num_checkers += 1;
    }
    // 王手している駒が１枚だけの場合は、合駒できる
    assert(num_checkers > 0);
    return num_checkers == 1;
  }

  // 3. 動かす手の王手
  return between_bb(move.to(), ksq).any();
}

} // namespace

Hand ProofPieces::AtLeaf(const Position& pos) {
  assert(pos.in_check());

  Hand proof_pieces;
  Color attack_side = ~pos.side_to_move();

  if (InterceptionIsPossible(pos)) {
    proof_pieces = GetMonopolizedPieces(pos, attack_side);
  }

  assert(pos.hand(attack_side).Dominates(proof_pieces));
  return proof_pieces;
}

Hand ProofPieces::AtFrontier(const Position& pos, Move mate_move) {
  assert(pos.MoveIsLegal(mate_move));

  Hand proof_pieces;
  Color attack_side = pos.side_to_move();

  if (CheckIsBlockable(pos, mate_move)) {
    proof_pieces = GetMonopolizedPieces(pos, attack_side);
  }

  if (mate_move.is_drop() && !proof_pieces.has(mate_move.piece_type())) {
    proof_pieces.add_one(mate_move.piece_type());
  }

  assert(pos.hand(attack_side).Dominates(proof_pieces));
  return proof_pieces;
}

Hand ProofPieces::AtAttackSide(Hand child, Move move) {
  Hand proof_pieces = child;

  if (move.is_drop()) {
    PieceType pt = move.piece_type();
    proof_pieces.add_one(pt);
  } else if (move.is_capture()) {
    PieceType captured = move.captured_piece().hand_type();
    if (proof_pieces.has(captured)) {
      proof_pieces.remove_one(captured);
    }
  }

  return proof_pieces;
}

Hand DisproofPieces::AtLeaf(const Position& pos) {
  const Color defence_side = ~pos.side_to_move();
  const Square ksq = pos.king_square(defence_side);
  const Bitboard occ = pos.pieces();

  Hand disproof_pieces = GetMonopolizedPieces(pos, defence_side);

  // 攻め方に持ち駒を渡しても、攻め方の王手の数が増えない場合は、
  // 受け方は、攻め方に対して、その駒を渡すことができる
  auto give_pieces = [&](PieceType pt, Bitboard target) {
    if ((target.andnot(occ)).none()) disproof_pieces.reset(pt);
  };
  give_pieces(kPawn  , step_attacks_bb(Piece(defence_side, kPawn  ), ksq));
  give_pieces(kKnight, step_attacks_bb(Piece(defence_side, kKnight), ksq));
  give_pieces(kSilver, step_attacks_bb(Piece(defence_side, kSilver), ksq));
  give_pieces(kGold  , step_attacks_bb(Piece(defence_side, kGold  ), ksq));
  give_pieces(kLance , lance_attacks_bb(ksq, occ, defence_side));
  give_pieces(kBishop, bishop_attacks_bb(ksq, occ));
  give_pieces(kRook  , rook_attacks_bb(ksq, occ));

  assert(pos.hand(defence_side).Dominates(disproof_pieces));
  return disproof_pieces;
}
