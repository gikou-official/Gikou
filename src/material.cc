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

#include "material.h"

#include <utility>
#include "common/array.h"
#include "evaluation.h"

ArrayMap<Score, PieceType> Material::values_;
ArrayMap<Score, PieceType> Material::promotion_values_;
ArrayMap<Score, PieceType> Material::exchange_values_;
ArrayMap<Score, PieceType> Material::exchange_orders_;

void Material::Init() {
  values_[kNoPieceType]           = kScoreZero;
  promotion_values_[kNoPieceType] = kScoreZero;
  exchange_values_[kNoPieceType]  = kScoreZero;
  exchange_orders_[kNoPieceType]  = kScoreZero;
  SetValue(kKing, kScoreZero);
  UpdateTables();
}

void Material::UpdateTables() {
  for (PieceType pt : Piece::all_piece_types()) {
    if (pt != kKing) {
      SetValue(pt, g_eval_params->material[pt]);
    }
  }
  UpdateExchangeOrders();
}

void Material::SetValue(PieceType pt, Score value) {
  // 1. 駒の価値を更新する
  values_[pt] = value;

  // 2. 駒が成る価値（promotion_values_）と駒の交換値（exchange_values_）を更新する
  if (IsPromotablePieceType(pt)) {
    promotion_values_[pt] = values_[GetPromotedType(pt)] - value;
    exchange_values_[pt] = 2 * value;
  } else if (IsPromotedPieceType(pt)) {
    PieceType ot = GetOriginalType(pt);
    promotion_values_[ot] = value - values_[ot];
    exchange_values_[pt] = value + values_[ot];
  } else {
    assert(pt == kGold || pt == kKing);
    assert(promotion_values_[pt] == kScoreZero);
    exchange_values_[pt] = 2 * value;
  }
}

void Material::UpdateExchangeOrders() {
  // 1. 駒の価値を、一時的に別の配列にコピーする
  typedef std::pair<PieceType, Score> Pair;
  Array<Pair, 16> temp;
  for (PieceType pt : Piece::all_piece_types()) {
    temp[pt] = std::make_pair(pt, exchange_values_[pt]);
  }

  // 2. 駒の価値の昇順で並べ替える
  std::stable_sort(temp.begin(), temp.end(),
                   [&](const Pair& lhs, const Pair& rhs) {
    return lhs.second < rhs.second;
  });

  // 3. 駒の交換順位（exchange_orderes_）を更新する
  int order = 0;
  for (const Pair& pair : temp) {
    PieceType pt = pair.first;
    if (pt != kNoPieceType) {
      exchange_orders_[pt] = static_cast<Score>(++order);
    }
  }
}
