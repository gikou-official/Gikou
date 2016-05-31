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

#include "hand.h"

#include <cctype>

constexpr Hand::Key Hand::keys_[8];

Hand& Hand::operator|=(Hand rhs) {
  // 1. 除算命令を用いて、左辺の持ち駒の方が多い駒種を特定する
  uint32_t diff = (hand_ | kBorrowMask) - rhs.hand_;

  // 2. 優越している駒のマスク（superior_mask）を作成する
  uint32_t superior_p      = (diff & kBorrowPawn) >> 5;
  uint32_t superior_lnsgbr = (diff & (kBorrowMask & ~kBorrowPawn)) >> 3;
  uint32_t superior_all    = superior_p | superior_lnsgbr;
  uint32_t superior_mask   = (kBorrowMask - superior_all) & ~kBorrowMask;

  // 3. 両辺の持ち駒の和集合を計算する
  uint32_t sum_of_sets = (hand_ & superior_mask) | (rhs.hand_ & ~superior_mask);
  hand_ = BitField<uint32_t>(sum_of_sets);
  return *this;
}

Hand Hand::GetMonopolizedPieces(Hand rhs) const {
  // 1. 右辺の持ち駒において、１枚も存在しない駒種を特定する
  uint32_t diff = kBorrowMask - rhs.hand_;
  uint32_t no_p      = (diff & kBorrowPawn) >> 5;
  uint32_t no_lnsgbr = (diff & (kBorrowMask & ~kBorrowPawn)) >> 3;
  uint32_t no_piece  = no_p | no_lnsgbr;

  // 2. 右辺に１枚も存在しない駒のマスクを作成する
  uint32_t no_piece_mask = kBorrowMask - no_piece;

  // 3. 左辺が独占している持ち駒を計算する
  return Hand(hand_ & no_piece_mask);
}

HandSet Hand::GetHandSet() const {
  uint32_t hand_set = 0;
  uint32_t is_not_zero = ((kBorrowMask - hand_) & kBorrowMask) ^ kBorrowMask;
  hand_set |= (is_not_zero & kBorrowPawn  ) >> ( 5 - kPawn );
  hand_set |= (is_not_zero & kBorrowLance ) >> ( 9 - kLance);
  hand_set |= (is_not_zero & kBorrowKnight) >> (13 - kKnight);
  hand_set |= (is_not_zero & kBorrowSilver) >> (17 - kSilver);
  hand_set |= (is_not_zero & kBorrowGold  ) >> (21 - kGold  );
  hand_set |= (is_not_zero & kBorrowBishop) >> (25 - kBishop);
  hand_set |= (is_not_zero & kBorrowRook  ) >> (29 - kRook  );
  return HandSet(hand_set);
}

std::string Hand::ToSfen(Color side_to_move) const {
  std::string result;
  auto descending = {kRook, kBishop, kGold, kSilver, kKnight, kLance, kPawn};
  for (PieceType pt : descending) {
    int n = count(pt);
    if (n > 0) {
      if (n >= 2) {
        result += std::to_string(n);
      }
      result += Piece(side_to_move, pt).ToSfen()[0];
    }
  }
  return result;
}

Hand Hand::FromSfen(const std::string& sfen, Color side_to_move) {
  Hand hand;
  int n = 0;
  for (size_t i = 0; i < sfen.size(); ++i) {
    if (std::isdigit(sfen[i])) {
      n *= 10;
      n += std::stoi(std::string{sfen[i]});
    } else {
      assert(std::isalpha(sfen[i]));
      if (   (std::isupper(sfen[i]) && side_to_move == kBlack)
          || (std::islower(sfen[i]) && side_to_move == kWhite)) {
        PieceType pt = Piece::FromSfen(sfen.substr(i, 1)).type();
        hand.set(pt, n >= 2 ? n : 1);
      }
      n = 0;
    }
  }
  return hand;
}

bool Hand::IsOk() const {
  // 各持ち駒の枚数が、ルール上ありうる枚数の範囲に収まっているかをチェックるする
  return count(kPawn  ) >= 0 && count(kPawn  ) <= 18
      && count(kLance ) >= 0 && count(kLance ) <=  4
      && count(kKnight) >= 0 && count(kKnight) <=  4
      && count(kSilver) >= 0 && count(kSilver) <=  4
      && count(kGold  ) >= 0 && count(kGold  ) <=  4
      && count(kBishop) >= 0 && count(kBishop) <=  2
      && count(kRook  ) >= 0 && count(kRook  ) <=  2;
}
