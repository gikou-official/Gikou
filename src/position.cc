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

#include "position.h"

#include <cstring>
#include <algorithm>
#include <sstream>
#include "zobrist.h"

Position::Position()
    : state_infos_(1),  // 最初から１要素分確保しておく
      current_state_info_(state_infos_.begin()) {
  num_unused_pieces_[kPawn  ] = 18;
  num_unused_pieces_[kLance ] =  4;
  num_unused_pieces_[kKnight] =  4;
  num_unused_pieces_[kSilver] =  4;
  num_unused_pieces_[kGold  ] =  4;
  num_unused_pieces_[kBishop] =  2;
  num_unused_pieces_[kRook  ] =  2;
  num_unused_pieces_[kKing  ] =  2;
}

Position::Position(Position&& pos)
    : hand_(pos.hand_),
      king_square_(pos.king_square_),
      state_infos_(pos.state_infos_),
      nodes_searched_(pos.nodes_searched_),
      side_to_move_(pos.side_to_move_),
      occupied_bb_(pos.occupied_bb_),
      color_bb_(pos.color_bb_),
      type_bb_(pos.type_bb_),
      piece_on_(pos.piece_on_),
      num_unused_pieces_(pos.num_unused_pieces_) {
  assert(pos.state_infos_.size() >= 1);
  auto offset = pos.current_state_info_ - pos.state_infos_.begin();
  current_state_info_ = state_infos_.begin() + offset; // イテレータを付け替える
}

Position::Position(const Position& pos)
    : hand_(pos.hand_),
      king_square_(pos.king_square_),
      state_infos_(pos.state_infos_),
      nodes_searched_(pos.nodes_searched_),
      side_to_move_(pos.side_to_move_),
      occupied_bb_(pos.occupied_bb_),
      color_bb_(pos.color_bb_),
      type_bb_(pos.type_bb_),
      piece_on_(pos.piece_on_),
      num_unused_pieces_(pos.num_unused_pieces_) {
  assert(pos.state_infos_.size() >= 1);
  auto offset = pos.current_state_info_ - pos.state_infos_.begin();
  current_state_info_ = state_infos_.begin() + offset; // イテレータを付け替える
}

Position& Position::operator=(const Position& pos) {
  assert(pos.state_infos_.size() >= 1);
  hand_ = pos.hand_;
  king_square_ = pos.king_square_;
  state_infos_ = pos.state_infos_;
  auto offset = pos.current_state_info_ - pos.state_infos_.begin();
  current_state_info_ = state_infos_.begin() + offset; // イテレータを付け替える
  nodes_searched_ = pos.nodes_searched_;
  side_to_move_ = pos.side_to_move_;
  occupied_bb_ = pos.occupied_bb_;
  color_bb_ = pos.color_bb_;
  type_bb_ = pos.type_bb_;
  piece_on_ = pos.piece_on_;
  num_unused_pieces_ = pos.num_unused_pieces_;
  return *this;
}

bool Position::operator==(const Position& rhs) const {
  assert(IsOk());
  assert(rhs.IsOk());

  // 1. 盤上の駒を比較する
  for (Square s : Square::all_squares())
    if (piece_on(s) != rhs.piece_on(s)) {
      return false;
    }

  // 2. 持ち駒を比較する
  if (hand(kBlack) != rhs.hand(kBlack) || hand(kWhite) != rhs.hand(kWhite))
    return false;

  // 3. 手番を比較する
  if (side_to_move() != rhs.side_to_move())
    return false;

  return true;
}

bool Position::operator!=(const Position& rhs) const {
  return !(*this == rhs);
}

Bitboard Position::AttackersTo(Square to, Bitboard occ) const {
  Bitboard hdk = pieces(kHorse, kDragon, kKing);
  Bitboard rd  = pieces(kRook, kDragon);
  Bitboard bh  = pieces(kBishop, kHorse);
  return (attackers_to<kBlack, kKing  >(to, occ) & hdk                 )
       | (attackers_to<kBlack, kRook  >(to, occ) & rd                  )
       | (attackers_to<kBlack, kBishop>(to, occ) & bh                  )
       | (attackers_to<kBlack, kGold  >(to, occ) & golds(kBlack)       )
       | (attackers_to<kWhite, kGold  >(to, occ) & golds(kWhite)       )
       | (attackers_to<kBlack, kSilver>(to, occ) & pieces(kBlackSilver))
       | (attackers_to<kWhite, kSilver>(to, occ) & pieces(kWhiteSilver))
       | (attackers_to<kBlack, kKnight>(to, occ) & pieces(kBlackKnight))
       | (attackers_to<kWhite, kKnight>(to, occ) & pieces(kWhiteKnight))
       | (attackers_to<kBlack, kLance >(to, occ) & pieces(kBlackLance ))
       | (attackers_to<kWhite, kLance >(to, occ) & pieces(kWhiteLance ))
       | (attackers_to<kBlack, kPawn  >(to, occ) & pieces(kBlackPawn  ))
       | (attackers_to<kWhite, kPawn  >(to, occ) & pieces(kWhitePawn  ));
}

Bitboard Position::SlidersAttackingTo(Square to, Bitboard occ) const {
  return (rook_attacks_bb(to, occ) & pieces(kRook, kDragon))
       | (bishop_attacks_bb(to, occ) & pieces(kBishop, kHorse))
       | (lance_attacks_bb(to, occ, kWhite) & pieces(kBlackLance))
       | (lance_attacks_bb(to, occ, kBlack) & pieces(kWhiteLance));
}

Bitboard Position::SlidersAttackingTo(Square to, Bitboard occ, Color c) const {
  Bitboard attackers = (rook_attacks_bb(to, occ) & pieces(kRook, kDragon))
                     | (bishop_attacks_bb(to, occ) & pieces(kBishop, kHorse))
                     | (lance_attacks_bb(to, occ, ~c) & pieces(kLance));
  return attackers & pieces(c);
}

bool Position::MoveIsLegal(Move move) const {
  return MoveIsPseudoLegal(move) && PseudoLegalMoveIsLegal(move);
}
bool Position::MoveIsPseudoLegal(Move move) const {
  assert(IsOk());
  assert(move.IsOk());
  assert(move.is_real_move());

  const Piece piece = move.piece();

  // 0. 手番が正しいこと
  if (piece.color() != side_to_move_)
    return false;

  if (move.is_drop()) {
    const PieceType pt = piece.type();

    // 1. 手番側が、その種類の持ち駒を持っていること
    if (!hand_[side_to_move_].has(pt))
      return false;

    Square to = move.to();

    // 2. 駒を打つマスは、空きマスであること
    if (!is_empty(to))
      return false;

    // 3. 二歩ではないこと
    if (pt == kPawn && pawn_on_file(to.file(), side_to_move_))
      return false;

    // 4. 王手がかかっている場合、駒打が合駒になっていること
    if (in_check()) {
      if (num_checkers() == 2) {
        return false;
      } else {
        assert(num_checkers() == 1);
        if (!between_bb(stm_king_square(), checkers().first_one()).test(to))
          return false;
      }
    }
  } else {
    // 1. 移動元に、これから動かす駒があること
    if (piece_on(move.from()) != piece)
      return false;

    // 2. 移動先に味方の駒がないこと
    if (pieces(side_to_move_).test(move.to()))
      return false;

    // 3. 駒が、移動先のマスがある方向に動けること
    if (!max_attacks_bb(piece, move.from()).test(move.to()))
      return false;

    // 4. 成る手の場合は、成ることができる駒であること
    if (move.is_promotion()) {
      if (!piece.can_promote())
        return false;
      if (   !move.to().is_promotion_zone_of(side_to_move_)
          && !move.from().is_promotion_zone_of(side_to_move_))
        return false;
    } else {
      // 行きどころのない駒になっていないこと
      if (   relative_rank(side_to_move_, move.to().rank()) == kRank1
          && (piece.is(kPawn) || piece.is(kLance)))
        return false;
#ifndef NDEBUG
      // この条件は、条件3.で含意されているので、デバッグ時のみチェックする
      if (piece.is(kKnight)) {
        assert(relative_rank(side_to_move_, move.to().rank()) >= kRank3);
      }
#endif
    }

    // 5. 取る手の場合は、取る駒が移動先のマスに存在していること
    if (piece_on(move.to()) != move.captured_piece())
      return false;

    // 6. 飛び駒の場合、駒の移動を邪魔する駒がないこと
    if (piece.is_slider()) {
      if ((between_bb(move.from(), move.to()) & pieces()).any())
        return false;
    }

    // 7. 王手がかかっている場合は、王手放置の手ではないこと
    if (in_check() && !move.piece().is(kKing)) {
      // ２枚の駒で王手をかけられた場合は、玉が逃げる手のみが合法手となる
      if (num_checkers() == 2) {
        return false;
      }
      // １枚の駒で王手をかけられた場合は、取る手または合駒をすることができる
      assert(num_checkers() == 1);
      Square own_king_sq = stm_king_square();
      Square checker_sq = checkers().first_one();
      Bitboard target = checkers() | between_bb(own_king_sq, checker_sq);
      if (!target.test(move.to())) {
        return false;
      }
    }
  }

  return true;
}
bool Position::NonDropMoveIsLegal(Move move) const {
  assert(IsOk());
  assert(move.IsOk());
  assert(move.is_real_move());
  assert(!move.is_drop());
  assert(MoveIsPseudoLegal(move));

  // 味方の玉がいない場合（片玉の詰将棋など）、自殺手になることはない
  if (!king_exists(side_to_move_)) {
    return true;
  }

  if (move.piece().is(kKing)) {
    // 移動先に相手の利きがある場合、そのマスには進めない
    if (square_is_attacked(~side_to_move_, move.to())) {
      return false;
    }

    // 玉に長い利きで王手がかけられている場合、その長い利きの方向には逃げられない
    DirectionSet long_attacks = long_controls(~side_to_move_, move.from());
    if (long_attacks.any()) {
      return !direction_bb(move.from(), long_attacks).test(move.to());
    }

    return true;
  } else {
    // ピンされていない駒を動かす場合には、自殺手になることはない
    if (!pinned_pieces().test(move.from())) {
      return true;
    }
    // ピンされている駒を動かす場合は、自殺手になることがあるので、チェックする
    Bitboard on_line = line_bb(move.from(), stm_king_square());
    assert(on_line.any());
    return on_line.test(move.to());
  }
}
bool Position::MoveGivesCheck(Move move) const {
  assert(move.IsOk());
  assert(move.is_real_move());
  assert(MoveIsPseudoLegal(move));

  // 相手番の玉がいなければ、王手をかけることはできない
  if (!king_exists(~side_to_move_)) {
    return false;
  }

  const Square to = move.to();
  const Square opp_king_sq = king_square(~side_to_move_);

  // 1. 通常の王手
  if (move.piece_type() != kKing) {
    if (   max_attacks_bb(move.piece_after_move(), to).test(opp_king_sq)
        && (between_bb(to, opp_king_sq) & pieces()).none()) {
      return true;
    }
  }

  // 2. 開き王手
  if (!move.is_drop()) {
    Square from = move.from();
    if (   discovered_check_candidates().test(from)
        && !line_bb(from, opp_king_sq).test(to)) {
      return true;
    }
  }

  return false;
}

void Position::MakeMove(Move move) {
  MakeMove(move, MoveGivesCheck(move));
}
void Position::MakeMove(Move move, bool move_gives_check) {
  assert(IsOk());
  assert(move.is_real_move());
  assert(MoveIsLegal(move));

  // スタックが足りなくなったら新たに割り当てる
  if (current_state_info_ == state_infos_.end() - 1) {
    size_t size = state_infos_.size();
    state_infos_.resize(2 * size);
    current_state_info_ = state_infos_.begin() + size;
  } else {
    ++current_state_info_;
  }
  const auto current_state = current_state_info_;
  const auto previous_state = current_state_info_ - 1;

  const Color stm = side_to_move_;
  current_state->extended_board = previous_state->extended_board;

  if (move.is_drop()) {
    Square to = move.to();
    PieceType pt = move.piece_type();

    // 1. 持ち駒を１枚減らす
    hand_[stm].remove_one(pt);

    // 2. 将棋盤の上に駒を置く
    Piece piece = move.piece();
    occupied_bb_  .set(to);
    color_bb_[stm].set(to);
    type_bb_ [pt ].set(to);
    piece_on_[to] = piece;

    // 3. 王手している駒を更新する
    if (move_gives_check) {
      current_state->checkers = square_bb(to);
      current_state->num_checkers = 1;
    } else {
      current_state->checkers.reset();
      current_state->num_checkers = 0;
    }

    // 4. 利き数を更新する
    current_state->extended_board.MakeDropMove(move);
  } else {
    Square from = move.from();
    Square to   = move.to();
    PieceType pt_from = move.piece_type();
    PieceType pt_to   = move.piece_type_after_move();

    // 1. ビットボードと持ち駒を更新する
    if (move.is_capture()) {
      // 取った駒を盤上から取り除く
      PieceType captured_pt = move.captured_piece_type();
      color_bb_[~stm       ].reset(to);
      type_bb_ [captured_pt].reset(to);

      // 駒を動かす
      occupied_bb_      .reset(from);
      color_bb_[stm    ].reset(from).set(to);
      type_bb_ [pt_from].reset(from);
      type_bb_ [pt_to  ].set(to);

      // 取った駒を持ち駒に加える
      hand_[stm].add_one(move.captured_piece().hand_type());

      // 利き数を更新する
      current_state->extended_board.MakeCaptureMove(move);
    } else {
      // 駒を動かす
      occupied_bb_      .reset(from).set(to);
      color_bb_[stm    ].reset(from).set(to);
      type_bb_ [pt_from].reset(from);
      type_bb_ [pt_to  ].set(to);

      // 利き数を更新する
      current_state->extended_board.MakeNonCaptureMove(move);
    }

    // 2. 盤上の駒を更新する
    piece_on_[from] = kNoPiece;
    piece_on_[to  ] = Piece(stm, pt_to);

    // 3. 玉のいるマスを更新する
    if (pt_from == kKing) {
      king_square_[stm] = to;
    }

    // 4. 王手している駒を求める
    if (move_gives_check) {
      Bitboard checkers_bb;
      Bitboard occ = pieces();
      Piece piece_to(stm, pt_to);
      Square king_sq = king_square(~stm);

      // 直接王手の場合
      if (   max_attacks_bb(piece_to, to).test(king_sq)
          && (between_bb(to, king_sq) & occ).none()) {
        checkers_bb.set(to);
      }

      // 開き王手の場合
      if (previous_state->discovered_check_candidates.test(from)) {
        checkers_bb |= SlidersAttackingTo(king_sq, occ, stm);
      }

      current_state->checkers     = checkers_bb;
      current_state->num_checkers = checkers_bb.count();
    } else {
      current_state->checkers.reset();
      current_state->num_checkers = 0;
    }
  }

  // 手番を変更する
  side_to_move_ = ~stm;

  // StateInfoを現在の状態に更新する
  current_state->pinned_pieces = ComputePinnedPieces();
  current_state->discovered_check_candidates = ComputeDiscoveredCheckCandidates();
  current_state->last_move = move;

  // 探索ノード数を１増やす
  ++nodes_searched_;

  assert(IsOk());
}

void Position::UnmakeMove(Move move) {
  assert(IsOk());
  assert(move.is_real_move());

  // 1. 手番を元に戻す
  side_to_move_ = ~side_to_move_;
  const Color stm = side_to_move_;

  if (move.is_drop()) {
    Square to = move.to();
    PieceType pt = move.piece_type();

    // 1. 将棋盤から打った駒を取り除く
    occupied_bb_  .reset(to);
    color_bb_[stm].reset(to);
    type_bb_ [pt ].reset(to);
    piece_on_[to] = kNoPiece;

    // 2. 駒を持ち駒に戻す
    hand_[stm].add_one(pt);
  } else {
    Square from = move.from();
    Square to   = move.to();
    PieceType pt_from = move.piece_type();
    PieceType pt_to   = move.piece_type_after_move();

    // 1. ビットボードと持ち駒を更新する
    if (move.is_capture()) {
      // 駒を移動先から移動元に戻す
      occupied_bb_      .set(from);
      color_bb_[stm    ].reset(to).set(from);
      type_bb_ [pt_from].set(from);
      type_bb_ [pt_to  ].reset(to);

      // 取った相手の駒を、元の位置に戻す
      PieceType captured_pt = move.captured_piece_type();
      color_bb_[~stm       ].set(to);
      type_bb_ [captured_pt].set(to);

      // 持ち駒を元に戻す
      hand_[stm].remove_one(move.captured_piece().hand_type());

      // 盤上の駒を更新する
      piece_on_[from] = move.piece();
      piece_on_[to  ] = move.captured_piece();
    } else {
      // ビットボードを更新する
      occupied_bb_      .reset(to).set(from);
      color_bb_[stm    ].reset(to).set(from);
      type_bb_ [pt_from].set(from);
      type_bb_ [pt_to  ].reset(to);

      // 盤上の駒を更新する
      piece_on_[from] = move.piece();
      piece_on_[to  ] = kNoPiece;
    }

    // 2. 玉のいるマスを更新する
    if (pt_from == kKing) {
      king_square_[stm] = from;
    }
  }

  // 3. StateInfoをひとつ前の状態に戻す
  --current_state_info_;

  assert(IsOk());
}

void Position::MakeNullMove() {
  assert(IsOk());
  assert(!in_check());

  // スタックが足りなくなったら新たに割り当てる
  if (current_state_info_ == state_infos_.end() - 1) {
    size_t size = state_infos_.size();
    state_infos_.resize(2 * size);
    current_state_info_ = state_infos_.begin() + size;
  } else {
    ++current_state_info_;
  }
  const auto current_state = current_state_info_;
  const auto previous_state = current_state_info_ - 1;

  // 1. 手番を更新する
  side_to_move_ = ~side_to_move_;

  // 2. StateInfoを更新する
  current_state->num_checkers   = 0;
  current_state->checkers       = Bitboard();
  current_state->pinned_pieces  = ComputePinnedPieces();
  current_state->discovered_check_candidates = ComputeDiscoveredCheckCandidates();
  current_state->last_move      = kMoveNull;
  current_state->extended_board = previous_state->extended_board;

  // 3. 探索ノード数を１増やす
  ++nodes_searched_;

  assert(IsOk());
}

void Position::MakeDropAndKingRecapture(Move move) {
  assert(IsOk());
  assert(move.is_drop());
  assert(MoveIsLegal(move));
  assert(MoveGivesCheck(move));
  assert(king_exists(~side_to_move_));
  assert(Square::distance(move.to(), king_square(~side_to_move_)) == 1);
  assert(AttackersTo(move.to(), pieces(), side_to_move_).none());

  // スタックが足りなくなったら新たに割り当てる
  if (current_state_info_ == state_infos_.end() - 1) {
    size_t size = state_infos_.size();
    state_infos_.resize(2 * size);
    current_state_info_ = state_infos_.begin() + size;
  } else {
    ++current_state_info_;
  }
  const auto current_state = current_state_info_;
  const auto previous_state = current_state_info_ - 1;

  const Color stm = side_to_move_;
  const Piece king = Piece(~stm, kKing);
  const Square king_from = king_square(~stm);
  const Square king_to   = move.to();

  // 1. 攻め方が打った駒を、受け方の持ち駒に加える
  PieceType pt = move.piece_type();
  hand_[ stm].remove_one(pt);
  hand_[~stm].add_one(pt);

  // 2. 受け方の玉を移動させる
  Bitboard from_to_bb = square_bb(king_from) | square_bb(king_to);
  occupied_bb_    ^= from_to_bb;  // reset and set
  color_bb_[~stm] ^= from_to_bb;
  type_bb_[kKing] ^= from_to_bb;
  piece_on_[king_from] = kNoPiece;
  piece_on_[king_to  ] = king;
  king_square_[~stm] = king_to;

  // 3. 利き数を更新する
  current_state->extended_board = previous_state->extended_board;
  current_state->extended_board.MakeDropAndKingRecapture(king_from, king_to, ~stm);

  // 4. StateInfoを更新する
  current_state->num_checkers = 0;
  current_state->checkers = Bitboard();
  current_state->pinned_pieces = ComputePinnedPieces();
  current_state->discovered_check_candidates  = ComputeDiscoveredCheckCandidates();
  current_state->last_move = Move(king, king_from, king_to, false, move.piece());

  // 5. 開き王手を検出する
  if (king_exists(stm)) {
    Bitboard between = between_bb(king_from, stm_king_square());
    if (between.any() && (between & pieces()).none()) {
      Bitboard line = line_bb(king_from, stm_king_square());
      if (!line.test(king_to)) {
        // 影の利きを見る
        Bitboard r = max_attacks_bb(kBlackRook, king_from);
        Bitboard b = max_attacks_bb(kBlackBishop, king_from);
        Bitboard l = max_attacks_bb(Piece(stm, kLance), king_from);
        Bitboard hidden_attackers = (r & pieces(kRook  , kDragon))
                                  | (b & pieces(kBishop, kHorse ))
                                  | (l & pieces(kLance          ));
        hidden_attackers &= pieces(~stm) & line;
        // 開き王手になるかを調べる
        while (hidden_attackers.any()) {
          Square attacker_sq = hidden_attackers.pop_first_one();
          if ((pieces() & between_bb(king_from, attacker_sq)).none()) {
            current_state->num_checkers = 1;
            current_state->checkers = square_bb(attacker_sq);
            break;
          }
        }
      }
    }
  }

  assert(IsOk());
}

void Position::UnmakeDropAndKingRecapture(Move move) {
  assert(IsOk());
  assert(move.is_drop());

  const Color stm = side_to_move_;
  const Square king_from = current_state_info_->last_move.from();
  const Square king_to   = current_state_info_->last_move.to();

  // 1. 攻め方が打った駒を、攻め方の駒台に元に戻す
  PieceType pt = move.piece_type();
  hand_[ stm].add_one(pt);
  hand_[~stm].remove_one(pt);

  // 2. 受け方の玉を元に戻す
  Bitboard from_to_bb = square_bb(king_from) | square_bb(king_to);
  occupied_bb_    ^= from_to_bb;  // reset and set
  color_bb_[~stm] ^= from_to_bb;
  type_bb_[kKing] ^= from_to_bb;
  piece_on_[king_from] = Piece(~stm, kKing);
  piece_on_[king_to  ] = kNoPiece;
  king_square_[~stm] = king_from;

  // 3. StateInfoをひとつ前の状態に戻す
  --current_state_info_;

  assert(IsOk());
}

Key64 Position::ComputeBoardKey() const {
  Key64 key = Zobrist::initial_side(side_to_move_);
  for (Square s : Square::all_squares()) {
    key += Zobrist::psq(piece_on(s), s);
  }
  return key;
}

Key64 Position::ComputePositionKey() const {
  Key64 key = Zobrist::initial_side(side_to_move_);
  for (Square s : Square::all_squares()) {
    key += Zobrist::psq(piece_on(s), s);
  }
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      for (int n = hand_[c].count(pt); n > 0; --n) {
        key += Zobrist::hand(Piece(c, pt));
      }
    }
  }
  return key;
}
void Position::PutPiece(Piece p, Square s) {
  assert(num_unused_pieces(p.original_type()) > 0);
  assert(!occupied_bb_.test(s));
  assert(!color_bb_[p.color()].test(s));
  assert(!type_bb_[p.type()].test(s));
  assert(piece_on_[s] == kNoPiece);

  occupied_bb_        .set(s);
  color_bb_[p.color()].set(s);
  type_bb_ [p.type() ].set(s);
  piece_on_[s] = p;

  if (p.is(kKing)) {
    king_square_[p.color()] = s;
  }

  --num_unused_pieces_[p.original_type()];
}
Piece Position::RemovePiece(Square s) {
  assert(occupied_bb_.test(s));
  assert(color_bb_[piece_on(s).color()].test(s));
  assert(type_bb_[piece_on(s).type()].test(s));
  assert(piece_on_[s] != kNoPiece);

  Piece removed = piece_on(s);

  occupied_bb_              .reset(s);
  color_bb_[removed.color()].reset(s);
  type_bb_ [removed.type() ].reset(s);
  piece_on_[s] = kNoPiece;

  if (removed.is(kKing)) {
    king_square_[removed.color()] = kSquareNone;
  }

  ++num_unused_pieces_[removed.original_type()];

  return removed;
}

void Position::AddOneToHand(Color c, PieceType pt) {
  assert(c == kBlack || c == kWhite);
  assert(IsDroppablePieceType(pt));
  hand_[c].add_one(pt);
  num_unused_pieces_[pt]--;
  assert(IsOk());
}

bool Position::WinDeclarationIsPossible(const bool is_csa_rule) const {
  assert(king_exists(side_to_move_));

  const Color stm = side_to_move_;
  const Bitboard promotion_zone = promotion_zone_bb(stm);

  // 1. 宣言側の玉が敵陣3段目以内に入っている。
  if (!promotion_zone.test(king_square(stm))) {
    return false;
  }

  // 2. 宣言側の敵陣3段目以内の駒は、玉を除いて10枚以上存在する。
  Bitboard non_king_pieces = pieces(stm).andnot(pieces(kKing)) & promotion_zone;
  if (non_king_pieces.count() < 10) {
    return false;
  }

  // 3. 宣言側の玉に王手がかかっていない。
  if (in_check()) {
    return false;
  }

  // 4. 宣言側が、大駒5点、小駒1点で計算して、規定以上の点数があること。
  //    点数の対象となるのは、玉を除く宣言側の持駒と敵陣3段目以内に存在する宣言側の駒のみ。
  Hand hand_pieces = hand(stm);
  Bitboard large_pieces = pieces(kBishop, kRook, kHorse, kDragon);
  // 小駒
  int num_small_pieces = non_king_pieces.andnot(large_pieces).count();
  num_small_pieces += hand_pieces.count(kPawn  );
  num_small_pieces += hand_pieces.count(kLance );
  num_small_pieces += hand_pieces.count(kKnight);
  num_small_pieces += hand_pieces.count(kSilver);
  num_small_pieces += hand_pieces.count(kGold  );
  // 大駒
  int num_large_pieces = (non_king_pieces & large_pieces).count();
  num_large_pieces += hand_pieces.count(kBishop);
  num_large_pieces += hand_pieces.count(kRook  );
  // 大駒5点、小駒1点で持点を計算する
  int points = num_small_pieces + (5 * num_large_pieces);

  // CSAルールか、24点法かで場合分けする
  if (is_csa_rule) {
    // CSAルール：先手の場合28点以上、後手の場合27点以上の持点があること。
    return points >= (stm == kBlack ? 28 : 27);
  } else {
    // 24点法：31点以上の持点があること。
    return points >= 31;
  }
}

std::string Position::ToSfen() const {
  std::string sfen;

  // 1. 盤上の駒
  for (Rank r = kRank1; r <= kRank9; ++r) {
    int num_empty_squares = 0;
    for (File f = kFile9; f >= kFile1; --f) {
      if (is_empty(Square(f, r))) {
        ++num_empty_squares;
        continue;
      }
      if (num_empty_squares > 0) {
        sfen += std::to_string(num_empty_squares);
        num_empty_squares = 0;
      }
      sfen += piece_on(Square(f, r)).ToSfen();
    }
    if (num_empty_squares > 0) {
      sfen += std::to_string(num_empty_squares);
    }
    if (r != kRank9) {
      sfen += '/';
    }
  }

  // 2. 手番
  sfen += side_to_move() == kBlack ? " b" : " w";

  // 3. 持ち駒
  if (hand(kBlack).none() && hand(kWhite).none()) {
    sfen += " -";
  } else {
    sfen += " ";
    sfen += hand(kBlack).ToSfen(kBlack);
    sfen += hand(kWhite).ToSfen(kWhite);
  }

  // 4. 手数
  sfen += " 1";

  return sfen;
}
Position Position::FromSfen(const std::string& sfen) {
  Position pos;
  std::istringstream is(sfen);
  std::string board_str, stm_str, hand_str, ply_str;

  // SFEN表記の文字列を、４つの部分に分ける
  if (!(is >> board_str >> stm_str >> hand_str >> ply_str)) {
    assert(0);
  }

  // 1. 盤上の駒
  std::string rank_str;
  std::istringstream board_is(board_str);
  for (Rank r = kRank1;
       r <= kRank9 && std::getline(board_is, rank_str, '/'); ++r) {
    char token;
    std::istringstream rank_is(rank_str);
    for (File f = kFile9; f >= kFile1 && (rank_is >> token); --f) {
      // 空きマス
      if (std::isdigit(token)) {
        int skip = std::stoi(std::string{token}) - 1; // ダブルカウントを防ぐ
        f -= skip;
        continue;
      }
      // 駒がある場合
      if (token == '+') {
        if (!(rank_is >> token)) {
          assert(0);
          break;
        }
        pos.PutPiece(Piece::FromSfen(std::string{'+', token}), Square(f, r));
      } else {
        pos.PutPiece(Piece::FromSfen(std::string{token}), Square(f, r));
      }
    }
  }

  // 2. 手番
  pos.side_to_move_ = (stm_str == "b" ? kBlack : kWhite);

  // 3. 持ち駒
  if (hand_str != "-") {
    Hand b = Hand::FromSfen(hand_str, kBlack);
    Hand w = Hand::FromSfen(hand_str, kWhite);
    pos.hand_[kBlack] = b;
    pos.hand_[kWhite] = w;
    pos.num_unused_pieces_[kPawn  ] -= b.count(kPawn  ) + w.count(kPawn  );
    pos.num_unused_pieces_[kLance ] -= b.count(kLance ) + w.count(kLance );
    pos.num_unused_pieces_[kKnight] -= b.count(kKnight) + w.count(kKnight);
    pos.num_unused_pieces_[kSilver] -= b.count(kSilver) + w.count(kSilver);
    pos.num_unused_pieces_[kGold  ] -= b.count(kGold  ) + w.count(kGold  );
    pos.num_unused_pieces_[kBishop] -= b.count(kBishop) + w.count(kBishop);
    pos.num_unused_pieces_[kRook  ] -= b.count(kRook  ) + w.count(kRook  );
  }

  // 4. StateInfoを初期化する
  pos.InitStateInfo();

  return pos;
}

Position Position::CreateStartPosition() {
  return FromSfen("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1");
}

Position& Position::Flip() {
  ArrayMap<Piece, Square> copy_of_piece_on = piece_on_;

  // 1. 盤上の駒の配置を１８０度回転させる
  for (Square s : Square::all_squares()) {
    if (!is_empty(s.inverse_square())) {
      RemovePiece(s.inverse_square());
    }
  }
  for (Square s : Square::all_squares()) {
    if (copy_of_piece_on[s] != kNoPiece) {
      PutPiece(copy_of_piece_on[s].opponent_piece(), s.inverse_square());
    }
  }

  // 2. 先手と後手の持ち駒を入れ替える
  std::swap(hand_[kBlack], hand_[kWhite]);

  // 3. 手番を逆にする
  side_to_move_ = ~side_to_move_;

  InitStateInfo();
  assert(IsOk());
  return *this;
}bool Position::IsOk(std::string* const error_message) const {
#define EXPECT(cond) if (!(cond)) { \
    if (error_message) \
      *error_message = __FILE__ ":" + std::to_string(__LINE__) + ": " #cond; \
    return false; \
  }

  // 1. side_to_move() が正しいこと
  const Color stm = side_to_move();
  EXPECT(stm == kBlack || stm == kWhite);

  // 2. ビットボードに余計なビットが立っていないこと
  EXPECT(!pieces().HasExcessBits());
  EXPECT(!pieces(kBlack).HasExcessBits());
  EXPECT(!pieces(kWhite).HasExcessBits());
  for (PieceType pt : Piece::all_piece_types()) {
    EXPECT(!pieces(pt).HasExcessBits());
  }
  for (Piece p : Piece::all_pieces()) {
    EXPECT(!pieces(p).HasExcessBits());
  }

  // 3. ビットボード間の整合性がとれていること
  EXPECT((pieces(kBlack) & pieces(kWhite)).none());
  EXPECT((pieces(kBlack) | pieces(kWhite)) == pieces());
  for (PieceType pt : Piece::all_piece_types()) {
    EXPECT(pieces(pt).none() || (pieces(pt) & pieces()).any());
  }
  for (PieceType pt1 : Piece::all_piece_types())
    for (PieceType pt2 : Piece::all_piece_types())
      if (pt1 != pt2) {
        EXPECT((pieces(pt1) & pieces(pt2)).none());
      }

  // 4. ビットボードと、piece_on()メソッドの結果の間で、整合性がとれていること
  for (Square s : Square::all_squares()) {
    Piece piece = piece_on(s);
    if (piece == kNoPiece) {
      EXPECT(!pieces().test(s));
      EXPECT(!pieces(kBlack).test(s));
      EXPECT(!pieces(kWhite).test(s));
      for (PieceType pt : Piece::all_piece_types()) {
        EXPECT(!pieces(pt).test(s));
      }
    } else {
      Color c = piece.color();
      EXPECT(pieces().test(s));
      EXPECT(pieces(c).test(s));
      EXPECT(!pieces(~c).test(s));
      for (PieceType pt : Piece::all_piece_types()) {
        if (pt == piece.type()) {
          EXPECT(pieces(pt).test(s));
        } else {
          EXPECT(!pieces(pt).test(s));
        }
      }
    }
  }

  // 5. 駒の枚数が正しいこと
  int num_pawn   = num_pieces(kPawn  ) + num_pieces(kPPawn  );
  int num_lance  = num_pieces(kLance ) + num_pieces(kPLance );
  int num_knight = num_pieces(kKnight) + num_pieces(kPKnight);
  int num_silver = num_pieces(kSilver) + num_pieces(kPSilver);
  int num_gold   = num_pieces(kGold  );
  int num_bishop = num_pieces(kBishop) + num_pieces(kHorse  );
  int num_rook   = num_pieces(kRook  ) + num_pieces(kDragon );
  int num_king   = num_pieces(kKing  );
  int num_b_king = num_pieces(kBlack, kKing);
  int num_w_king = num_pieces(kWhite, kKing);
  EXPECT(0 <= num_pawn   && num_pawn   <= 18);
  EXPECT(0 <= num_lance  && num_lance  <=  4);
  EXPECT(0 <= num_knight && num_knight <=  4);
  EXPECT(0 <= num_silver && num_silver <=  4);
  EXPECT(0 <= num_gold   && num_gold   <=  4);
  EXPECT(0 <= num_bishop && num_bishop <=  2);
  EXPECT(0 <= num_rook   && num_rook   <=  2);
  EXPECT(0 <= num_king   && num_king   <=  2);
  EXPECT(0 <= num_b_king && num_b_king <=  1);
  EXPECT(0 <= num_w_king && num_w_king <=  1);
  EXPECT(num_pawn   + num_unused_pieces(kPawn  ) == 18);
  EXPECT(num_lance  + num_unused_pieces(kLance ) ==  4);
  EXPECT(num_knight + num_unused_pieces(kKnight) ==  4);
  EXPECT(num_silver + num_unused_pieces(kSilver) ==  4);
  EXPECT(num_gold   + num_unused_pieces(kGold  ) ==  4);
  EXPECT(num_bishop + num_unused_pieces(kBishop) ==  2);
  EXPECT(num_rook   + num_unused_pieces(kRook  ) ==  2);
  EXPECT(num_king   + num_unused_pieces(kKing  ) ==  2);

  // 6. king_square()メソッドと、他のメソッドとの間で、整合性がとれていること
  for (Color c : {kBlack, kWhite}) {
    if (pieces(c, kKing).any()) {
      EXPECT(king_exists(c));
      EXPECT(king_square(c) == pieces(c, kKing).first_one());
      EXPECT(piece_on(king_square(c)) == Piece(c, kKing));
      EXPECT(pieces(c, kKing).count() == 1);
    } else {
      EXPECT(!king_exists(c));
    }
  }

  // 7. 「行きどころのない駒」が存在しないこと
  EXPECT(!pieces(kBlackPawn  ).test(rank_bb<1, 1>()));
  EXPECT(!pieces(kBlackLance ).test(rank_bb<1, 1>()));
  EXPECT(!pieces(kBlackKnight).test(rank_bb<1, 2>()));
  EXPECT(!pieces(kWhitePawn  ).test(rank_bb<9, 9>()));
  EXPECT(!pieces(kWhiteLance ).test(rank_bb<9, 9>()));
  EXPECT(!pieces(kWhiteKnight).test(rank_bb<8, 9>()));

  // 8. 二歩でないこと
  for (File f = kFile1; f <= kFile9; ++f) {
    EXPECT((pieces(kBlackPawn) & file_bb(f)).count() <= 1);
    EXPECT((pieces(kWhitePawn) & file_bb(f)).count() <= 1);
  }

  // 9. state_info_ が最新の盤面情報を反映していること
  EXPECT(checkers() == ComputeCheckers());
  EXPECT(num_checkers() == ComputeCheckers().count());
  EXPECT(pinned_pieces() == ComputePinnedPieces());
  EXPECT(discovered_check_candidates() == ComputeDiscoveredCheckCandidates());

  // 10. last_move() が正しいこと
  EXPECT(last_move().IsOk());

  // 11. 王手している駒がある場合、その駒は２枚以内であること
  EXPECT(num_checkers() <= 2);
  EXPECT(checkers().count() <= 2);

  // 12. 相手の玉を取る手が存在しないこと
  //    （もしも、相手の玉を取る手が存在する場合、それ以前に王手放置をしていることになるため）
  if (king_exists(~stm)) {
    EXPECT(AttackersTo(king_square(~stm), pieces(), stm).none());
  }

  // 13. 利き数の計算結果が、各所で整合性がとれていること
  for (Color c : {kBlack, kWhite}) {
    for (Square s : Square::all_squares()) {
      Bitboard attackers = AttackersTo(s, pieces(), c);
      int n = current_state_info_->extended_board.num_controls(c, s);
      EXPECT(n == attackers.count());
    }
  }

  // 14. extended_boardと、piece_on_とで、整合性がとれていること
  for (Square s : Square::all_squares()) {
    EXPECT(current_state_info_->extended_board.piece_on(s) == piece_on_[s]);
  }

  return true;

#undef EXPECT
}
void Position::Print(Move move) const {
  for (Rank r = kRank1; r <= kRank9; ++r) {
    for (File f = kFile9; f >= kFile1; --f) {
      if (is_empty(Square(f, r))) {
        std::printf("  .");
      } else {
        std::printf(" %2s", piece_on(Square(f, r)).ToSfen().c_str());
      }
    }
    std::printf("\n");
  }
  std::printf("%s\n", ToSfen().c_str());
  if (move.IsOk()) {
    if (move == kMoveNull) {
      std::printf("move: null move\n");
    } else if (move == kMoveNone) {
      std::printf("move: move none\n");
    } else {
      std::printf("move: %s\n", move.ToSfen().c_str());
    }
  } else {
    std::printf("move: invalid move\n");
  }
}

void Position::InitStateInfo() {
  state_infos_.clear();
  state_infos_.emplace_back();
  current_state_info_ = state_infos_.begin();
  current_state_info_->checkers = ComputeCheckers();
  current_state_info_->pinned_pieces = ComputePinnedPieces();
  current_state_info_->discovered_check_candidates = ComputeDiscoveredCheckCandidates();
  current_state_info_->last_move = kMoveNone;
  current_state_info_->num_checkers = current_state_info_->checkers.count();
  current_state_info_->extended_board.Clear();
  current_state_info_->extended_board.SetAllPieces(*this);
}

Bitboard Position::ComputeObstructingPieces(Color king_color) const {
  if (!king_exists(king_color)) {
    return Bitboard();
  }

  Square king_sq = king_square(king_color);
  Bitboard r = max_attacks_bb(kBlackRook, king_sq);
  Bitboard b = max_attacks_bb(kBlackBishop, king_sq);
  Bitboard l = max_attacks_bb(Piece(king_color, kLance), king_sq);

  // 1. ピンしている駒の候補を求める
  Bitboard pinners = (r & pieces(kRook, kDragon))
                   | (b & pieces(kBishop, kHorse))
                   | (l & pieces(kLance));
  pinners &= pieces(~king_color);
  pinners = pinners.andnot(max_attacks_bb(kBlackKing, king_sq));

  // 2. ピンされている駒を求める
  Bitboard obstructing_pieces;
  pinners.ForEach([&](Square pinner_sq) {
    Bitboard bb = pieces() & between_bb(king_sq, pinner_sq);
    if (bb.count() == 1) {
      obstructing_pieces |= bb;
    }
  });

  return obstructing_pieces;
}
