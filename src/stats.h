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

#ifndef STATS_H_
#define STATS_H_

#include <functional>
#include <memory>
#include "common/array.h"
#include "common/arraymap.h"
#include "move.h"

/**
 * ヒストリー値の統計を取るためのクラスです.
 */
class HistoryStats {
 public:
  static constexpr int32_t kMax = 250;

  /**
   * ヒストリー値を取得します.
   * 現在の実装では、過去のβカット率を返します。
   */
  int32_t operator[](Move move) const {
    assert(move.is_quiet());
    return table_[move.to()][move.piece()];
  }

  /**
   * 過去の探索において、βカットした回数よりも、βカットしない回数の方が多かった場合に、trueを返します.
   */
  bool HasNegativeScore(Move move) const {
    assert(move.is_quiet());
    return table_[move.to()][move.piece()] < 0;
  }

  /**
   * 与えられた指し手でβカットした場合に、その指し手の得点を加算します.
   */
  void UpdateSuccess(Move move, Depth depth) {
    assert(move.is_quiet());
    assert(depth > kDepthZero);
    Square to = move.to();
    Piece pc = move.piece();
    int d = depth / kOnePly;
    int score = d * d;
    if (table_[to][pc] + score < kMax) {
      table_[to][pc] += score;
    }
  }

  /**
   * 与えられた指し手がβカットしなかった場合に、その指し手の得点を減点します.
   */
  void UpdateFail(Move move, Depth depth) {
    assert(move.is_quiet());
    assert(depth > kDepthZero);
    Square to = move.to();
    Piece pc = move.piece();
    int d = depth / kOnePly;
    int score = d * d;
    if (table_[to][pc] - score > -kMax) {
      table_[to][pc] -= score;
    }
  }

  void Clear() {
    table_.clear();
  }

 private:
  ArrayMap<int16_t, Square, Piece> table_;
};

/**
 * カウンター手のヒストリー値の統計を取るためのクラスです.
 */
class CountermovesHistoryStats {
 public:
  CountermovesHistoryStats()
      : table_(new ArrayMap<HistoryStats, Square, Piece>()) {
    Clear();
  }

  const HistoryStats* operator[](Move move) const {
    assert(move.is_real_move());
    return &(*table_)[move.to()][move.piece()];
  }

  HistoryStats* operator[](Move move) {
    assert(move.is_real_move());
    return &(*table_)[move.to()][move.piece()];
  }

  void Clear() {
    for (Square s : Square::all_squares()) {
      for (Piece p : Piece::all_pieces()) {
        (*table_)[s][p].Clear();
      }
    }
  }

 private:
  std::unique_ptr<ArrayMap<HistoryStats, Square, Piece>> table_;
};

/**
 * ゲイン（１手前の評価値と、現局面の評価値の差）の統計を取るためのクラスです.
 */
class GainsStats {
 public:
  struct Gain {
    int32_t sum;
    int32_t count;
  };

  GainsStats() {
    Clear();
  }

  Score operator[](Move move) const {
    assert(move.is_quiet());
    const Gain& gain = table_[move.PerfectHash()];
    return static_cast<Score>(gain.sum / (gain.count + 1));
  }

  void Update(Move move, Score score) {
    assert(move.is_quiet());
    uint32_t key = move.PerfectHash();
    Gain gain = table_[key];
    if (gain.count >= 256) {
      gain.sum /= 2;
      gain.count /= 2;
    }
    gain.sum += score;
    gain.count += 1;
    table_[key] = gain;
  }

  void Clear() {
    table_.clear();
  }
 private:
  Array<Gain, Move::kPerfectHashSize> table_;
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
