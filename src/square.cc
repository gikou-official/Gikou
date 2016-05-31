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

#include "square.h"

#include <cstdlib>
#include <algorithm>

ArrayMap<int, Square, Square> Square::distance_;
ArrayMap<int, Square, Square> Square::relation_;
ArrayMap<Square, Direction> Square::direction_to_delta_;

void Square::Init() {
  // distance_
  for (Square i : Square::all_squares())
    for (Square j : Square::all_squares()) {
      int x_distance = std::abs(i.file() - j.file());
      int y_distance = std::abs(i.rank() - j.rank());
      int chebychev_distance = std::max(x_distance, y_distance);
      distance_[i][j] = chebychev_distance;
    }

  // relation_
  for (Square from : Square::all_squares())
    for (Square to : Square::all_squares()) {
      int x = std::abs(static_cast<int>(to.file() - from.file()));
      int y = to.rank() - from.rank() + kRank9;
      int r = x + 9 * y;
      assert(0 <= r && r <= 152);
      relation_[from][to] = r;
    }

  // direction_to_delta_
  direction_to_delta_[kDirNE] = kDeltaNE;
  direction_to_delta_[kDirE ] = kDeltaE ;
  direction_to_delta_[kDirSE] = kDeltaSE;
  direction_to_delta_[kDirN ] = kDeltaN ;
  direction_to_delta_[kDirS ] = kDeltaS ;
  direction_to_delta_[kDirNW] = kDeltaNW;
  direction_to_delta_[kDirW ] = kDeltaW ;
  direction_to_delta_[kDirSW] = kDeltaSW;
}
