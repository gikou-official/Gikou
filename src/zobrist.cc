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

#include "zobrist.h"

#include <random>

Key64 Zobrist::exclusion_;
ArrayMap<Key64, Color> Zobrist::null_move_;
ArrayMap<Key64, Square, Piece> Zobrist::psq_;
ArrayMap<Key64, Piece> Zobrist::hand_;
Array<ArrayMap<Key64, Square, Piece>, Zobrist::kPathTableSize> Zobrist::path_;

void Zobrist::Init() {
  std::random_device rd;
  std::mt19937_64 gen(rd());

  // 非ゼロの一様乱数を使う
  std::uniform_int_distribution<int64_t> dis(INT64_C(1), INT64_MAX);

  exclusion_ = Key64(dis(gen));

  Key64 side_key = Key64(dis(gen));
  null_move_[kBlack] = +side_key;
  null_move_[kWhite] = -side_key;

  for (Square s : Square::all_squares())
    for (Piece p : Piece::all_pieces()) {
      psq_[s][p] = Key64(dis(gen));
    }

  for (Piece p : Piece::all_pieces()) {
    hand_[p] = Key64(dis(gen));
  }

  for (int i = 0; i < kPathTableSize; ++i)
    for (Square s : Square::all_squares())
      for (Piece p : Piece::all_pieces()) {
        path_[i][s][p] = Key64(dis(gen));
      }
}
