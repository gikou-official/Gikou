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

#ifndef SWAP_H_
#define SWAP_H_

#include "move.h"
class Position;

/**
 * 駒交換の評価（Static Exchange Evaluation）を行います.
 */
class Swap {
 public:
  /**
   * 駒交換の損得（SEE値）を計算します.
   */
  static Score Evaluate(Move move, const Position& pos);

  /**
   * 駒交換が得になる場合（SEE値 > 0 の場合）に、trueを返します.
   */
  static bool IsWinning(Move move, const Position& pos);

  /**
   * 駒交換が損になる場合（SEE値 < 0 の場合）に、trueを返します.
   */
  static bool IsLosing(Move move, const Position& pos);

  /**
   * 盤全体での駒交換の損得を計算します.
   */
  static Score EvaluateGlobalSwap(Move move, const Position& pos, int depth_limit);
};

#endif /* SWAP_H_ */
