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

#include "notations.h"

#include <iostream>
#include <map>

namespace {

const std::map<std::string, PieceType> g_piece_type_from_csa = {
    {"FU", kPawn   },
    {"KY", kLance  },
    {"KE", kKnight },
    {"GI", kSilver },
    {"KI", kGold   },
    {"KA", kBishop },
    {"HI", kRook   },
    {"OU", kKing   },
    {"TO", kPPawn  },
    {"NY", kPLance },
    {"NK", kPKnight},
    {"NG", kPSilver},
    {"UM", kHorse  },
    {"RY", kDragon },
};

Square SquareFromCsa(const std::string& csa) {
  assert(csa.size() == 2);
  File file = static_cast<File>(std::stoi(csa.substr(0, 1)) - 1 + kFile1);
  Rank rank = static_cast<Rank>(std::stoi(csa.substr(1, 1)) - 1 + kRank1);
  assert(kFile1 <= file && file <= kFile9);
  assert(kRank1 <= rank && rank <= kRank9);
  return Square(file, rank);
}

}

Move Csa::ParseMove(const std::string& csa, const Position& pos) {
  assert(csa.size() == 6 || csa.size() == 7);
  // 1. CSA表記の文字列を、各部分に切り分ける
  size_t offset = csa.size() - 6;
  std::string from_str  = csa.substr(offset + 0, 2);
  std::string to_str    = csa.substr(offset + 2, 2);
  std::string piece_str = csa.substr(offset + 4, 2);

  // 2. 打つ手と動かす手の共通部分を処理する
  bool is_drop = (from_str == "00");
  Square to = SquareFromCsa(to_str);
  PieceType pt = g_piece_type_from_csa.at(piece_str);

  // 3. 打つ手と動かす手で異なる部分を処理する
  if (is_drop) {
    return Move(pos.side_to_move(), pt, to);
  } else {
    Square from = SquareFromCsa(from_str);
    PieceType pt_from = pos.piece_on(from).type();
    bool is_promotion = pt_from != pt;
    Piece captured = pos.piece_on(to);
    return Move(pos.side_to_move(), pt_from, from, to, is_promotion, captured);
  }
}

