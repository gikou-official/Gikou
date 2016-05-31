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

#ifndef STATS_H_
#define STATS_H_

#include <functional>
#include "common/array.h"
#include "common/arraymap.h"
#include "move.h"

/**
 * ヒストリー値の統計を取るためのクラスです.
 */
class HistoryStats {
 public:
  enum {
    kMax = 8192,
  };

  /**
   * ヒストリー値を取得します.
   * 現在の実装では、過去のβカット率を返します。
   */
  int32_t operator[](Move move) const {
    assert(move.is_quiet());
    const Entry& entry = table_[move.PerfectHash()];
    // additive smoothing
    return kMax * (entry.good + 1) / (entry.tried + 2);
  }

  /**
   * 過去の探索において、βカットした回数よりも、βカットしない回数の方が多かった場合に、trueを返します.
   */
  bool HasNegativeScore(Move move) const {
    assert(move.is_quiet());
    const Entry& entry = table_[move.PerfectHash()];
    // (entry.good + 1) / (entry.tried + 2) < 0.5
    return 2 * (entry.good + 1) < entry.tried + 2;
  }

  /**
   * 与えられた指し手でβカットした場合に、その指し手の得点を加算します.
   */
  void UpdateSuccess(Move move, Depth depth) {
    assert(move.is_quiet());
    assert(depth > kDepthZero);
    uint32_t key = move.PerfectHash();
    int32_t score = static_cast<int32_t>(depth / kOnePly);
    // オーバーフローを回避する
    if (table_[key].tried + score >= kMax) {
      table_[key].good  /= 2;
      table_[key].tried /= 2;
    }
    table_[key].good  += score;
    table_[key].tried += score;
    assert(table_[key].good < kMax);
    assert(table_[key].tried < kMax);
  }

  /**
   * 与えられた指し手がβカットしなかった場合に、その指し手の得点を減点します.
   */
  void UpdateFail(Move move, Depth depth) {
    assert(move.is_quiet());
    assert(depth > kDepthZero);
    uint32_t key = move.PerfectHash();
    int32_t score = static_cast<int32_t>(depth / kOnePly);
    // オーバーフローを回避する
    if (table_[key].tried + score >= kMax) {
      table_[key].good  /= 2;
      table_[key].tried /= 2;
    }
    table_[key].tried += score;
    assert(table_[key].good < kMax);
    assert(table_[key].tried < kMax);
  }

  void Clear() {
    table_.clear();
  }

 private:
  struct Entry {
    int32_t good  = 0;
    int32_t tried = 0;
  };
  Array<Entry, Move::kPerfectHashSize> table_;
};

/**
 * ゲイン（１手前の評価値と、現局面の評価値の差）の統計を取るためのクラスです.
 */
class GainsStats {
 public:
  GainsStats() {
    Clear();
  }

  Score operator[](Move move) const {
    assert(move.is_quiet());
    return table_[move.PerfectHash()];
  }

  void Update(Move move, Score score) {
    assert(move.is_quiet());
    uint32_t key = move.PerfectHash();
    table_[key] = std::max(score, table_[key] - 1);
  }

  void Clear() {
    table_.clear();
  }
 private:
  Array<Score, Move::kPerfectHashSize> table_;
};

/**
 * カウンター手等の統計を取るためのクラスです.
 */
class MovesStats {
 public:
  MovesStats() {
    Clear();
  }

  Array<Move, 2> operator[](Move previous) const {
    if (previous.is_real_move()) {
      return table_[previous.to()][previous.piece()];
    } else {
      return Array<Move, 2>{kMoveNone, kMoveNone};
    }
  }

  void Update(Move previous, Move following) {
    assert(previous.is_real_move());
    assert(following.is_quiet());
    Piece p = previous.piece();
    Square sq = previous.to();
    if (following != table_[sq][p][0]) {
      table_[sq][p][1] = table_[sq][p][0];
      table_[sq][p][0]  = following;
    }
  }

  void Clear() {
    table_.clear();
  }

 private:
  ArrayMap<Array<Move, 2>, Square, Piece> table_;
};

#endif /* STATS_H_ */
