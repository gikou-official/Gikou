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

#ifndef MOVEGEN_H_
#define MOVEGEN_H_

#include "common/array.h"
#include "move.h"

class Position;

/**
 * 指し手生成のタイプです.
 */
enum GeneratorType {
  kRecaptures,     /**< 取り返しの手 */
  kCaptures,       /**< 取る手 */
  kQuiets,         /**< 静かな手 */
  kEvasions,       /**< 王手回避手 */
  kChecks,         /**< 王手 */
  kQuietChecks,    /**< 王手（静かな手のみ） */
  kAdjacentChecks, /**< 近接王手（合駒が効かない王手） */
  kNonEvasions,    /**< すべての手（王手がかかっていない場合のみ利用可） */
  kAllMoves,       /**< すべての手 */
};

/**
 * 指し手を生成します.
 * @param kGt   指し手生成のタイプ
 * @param pos   指し手生成の対象局面
 * @param stack 指し手を保存するスタックの始まりを指すポインタ
 * @return 指し手の終わりを指すポインタ
 */
template<GeneratorType kGt>
ExtMove* GenerateMoves(const Position& pos, ExtMove* stack);

/**
 * 特定のマスに移動する手を生成します.
 * @param pos   指し手生成の対象局面
 * @param to    移動先のマス
 * @param stack 指し手を保存するスタックの始まりを指すポインタ
 * @return 指し手の終わりを指すポインタ
 */
ExtMove* GenerateMovesTo(const Position& pos, Square to, ExtMove* stack);

/**
 * 非合法手を取り除きます.
 * @param pos   指し手生成の対象局面
 * @param stack 指し手を保存するスタックの始まりを指すポインタ
 * @param stack 指し手を保存するスタックの終わりを指すポインタ
 * @return 指し手の終わりを指すポインタ
 */
ExtMove* RemoveIllegalMoves(const Position& pos, ExtMove* begin, ExtMove* end);

/**
 * 指し手生成機能がついた、簡易な指し手のコンテナです.
 */
template<GeneratorType kGt, bool kRemoveIllegalMoves = false>
class SimpleMoveList {
 public:
  SimpleMoveList(const Position& pos) {
    end_ = GenerateMoves<kGt>(pos, moves_.begin());
    if (kRemoveIllegalMoves) {
      end_ = RemoveIllegalMoves(pos, moves_.begin(), end_);
    }
  }
  ExtMove& operator[](size_t n) {
    return moves_[n];
  }
  const ExtMove& operator[](size_t n) const {
    return moves_[n];
  }
  bool empty() const {
    return end_ == moves_.begin();
  }
  size_t size() const {
    return end_ - moves_.begin();
  }
  ExtMove* begin() {
    return moves_.begin();
  }
  const ExtMove* begin() const {
    return moves_.begin();
  }
  ExtMove* end() {
    return end_;
  }
  const ExtMove* end() const {
    return end_;
  }
 private:
  enum {
    kSize = kGt == kEvasions ? Move::kMaxLegalEvasions : Move::kMaxLegalMoves
  };
  ExtMove* end_;
  Array<ExtMove, kSize> moves_;
};

#endif /* MOVEGEN_H_ */
