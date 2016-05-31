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

#ifndef MOVE_PROBABILITY_H_
#define MOVE_PROBABILITY_H_

#include <unordered_map>
#include <vector>
#include "move_feature.h"
class Position;

/**
 * 指し手が指される確率を計算するためのクラスです.
 */
struct MoveProbability {
  /**
   * 指し手が指される確率を計算します.
   */
  static std::unordered_map<uint32_t, float> ComputeProbabilities(const Position& pos,
                                                                  const HistoryStats& history,
                                                                  const GainsStats& gains);

  /**
   * 指し手が指される確率を棋譜から学習します.
   *
   * 現在の実装では、激指の方法（二値分類）を発展させて、多クラスロジスティック回帰により、
   * 実現確率を計算しています。
   *
   * （実現確率についての参考文献）
   *   - 鶴岡慶雅: 「激指」の最近の改良について --コンピュータ将棋と機械学習--,
   *     『コンピュータ将棋の進歩６』, pp.72-77, 共立出版, 2012.
   *
   * （多クラスロジスティック回帰についての参考文献）
   *   - C. M. ビショップ: 『パターン認識と機械学習・上』, pp.208-209, 丸善出版, 2012.
   */
  static void Learn();

  /**
   * 確率を計算するために必要なテーブルの初期化処理を行います.
   */
  static void Init();
};

#endif /* MOVE_PROBABILITY_H_ */
