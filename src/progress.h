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

#ifndef PROGRESS_H_
#define PROGRESS_H_

#include "psq.h"

class Position;

/**
 * 進行度を計算するためのクラスです.
 */
class Progress {
 public:

  /** 重みを固定小数点で扱うためのスケールです. */
  static constexpr int32_t kWeightScale = 1 << 16;

  /**
   * 進行度を計算するためのパラメータをファイル（"progress.bin"）から読み込みます.
   */
  static void ReadWeightsFromFile();

  /**
   * 進行度を推定するためのパラメータを学習します.
   */
  static void LearnParameters();

  /**
   * 局面全体の進行度を推定します.
   * @param pos 進行度を求めたい局面
   * @param psq_list
   * @return 進行度（0.0から1.0まで。値が大きいほど終盤であることを表す。）
   */
  static double EstimateProgress(const Position& pos, const PsqList& psq_list);

  /**
   * 局面全体の進行度を推定します.
   * 内部でPsqListを生成するため、PsqListを引数に渡さないメソッドと比べて速度が遅くなります。
   * @param pos 進行度を求めたい局面
   * @return 進行度（0.0から1.0まで。値が大きいほど終盤であることを表す。）
   */
  static double EstimateProgress(const Position& pos);

  /**
   * 進行度を推定するために使用される、重みベクトルです.
   */
  static ArrayMap<int32_t, Square, PsqIndex> weights;
};

#endif /* PROGRESS_H_ */
