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

#ifndef MATERIAL_H_
#define MATERIAL_H_

#include "piece.h"

/**
 * 駒の価値を管理するクラスです.
 *
 * 以下の4種類の駒の価値を保持しています。
 *   - value() => 駒の価値
 *   - promotion_value() => 駒が成る価値
 *   - exchange_value() => 駒の交換値
 *   - exchange_order() =>　駒の交換順位（駒の交換値が低い方から数えて何番目か）
 *
 * このように駒の価値を整理しておくと、SEE(Static Exchange Evaluation)や、
 * 探索等のコードを書くときに便利です。
 */
class Material {
 public:
  /**
   * 各種テーブルを初期化します.
   */
  static void Init();

  static Score value(PieceType pt) {
    return values_[pt];
  }

  static Score promotion_value(PieceType pt) {
    return promotion_values_[pt];
  }

  static Score exchange_value(PieceType pt) {
    return exchange_values_[pt];
  }

  static Score exchange_order(PieceType pt) {
    return exchange_orders_[pt];
  }

  /**
   * 駒の価値を保持している内部テーブルを更新します.
   * 具体的には、g_eval_params->materialの現在値を読み出し、その値でテーブルを更新します。
   * g_eval_paramsについての詳細は、evaluation.hを参照してください。
   */
  static void UpdateTables();

 private:
  static void SetValue(PieceType pt, Score value);
  static void UpdateExchangeOrders();
  static ArrayMap<Score, PieceType> values_;
  static ArrayMap<Score, PieceType> promotion_values_;
  static ArrayMap<Score, PieceType> exchange_values_;
  static ArrayMap<Score, PieceType> exchange_orders_;
};

#endif /* MATERIAL_H_ */
