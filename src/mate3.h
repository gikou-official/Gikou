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

#ifndef MATE3_H_
#define MATE3_H_

#include "common/array.h"
#include "bitboard.h"
#include "hand.h"
#include "move.h"
class Position;

/**
 * ３手詰関数の結果を保存するためのクラスです.
 */
struct Mate3Result {
  /** 受け方の玉を３手以内に詰ますことができる手. */
  Move mate_move;

  /** 相手玉が詰むとして、それは何手詰か. */
  int mate_distance;

  /** 証明駒（相手玉を詰ますのに最低限必要な、攻め方の持ち駒）. */
  Hand proof_pieces;
};

/**
 * 与えられた局面について、３手以内の詰みが存在する場合は、trueを返します.
 *
 * なお、現在の実装では、高速化の観点から、合駒できない王手（玉の8近傍からの王手 or 桂馬による王手）
 * のみを調べるようになっています。
 * （参考文献）
 *   - 岸本章宏: 難問詰将棋をコンピュータで解く, 『コンピュータ将棋の進歩６』, pp.116-118,
 *     共立出版, 2012.
 *
 * @param pos    詰みの有無を調べたい局面
 * @param result 詰みが見つかった場合に、その結果を保存する場所です
 * @return ３手以内の詰みが存在する場合は、true
 */
bool IsMateInThreePlies(Position& pos, Mate3Result* result);

/**
 * 王手回避手を逐次生成するためのクラスです.
 *
 * 注意：現在の実装では、３手詰み関数内部において合駒できない王手しか調べていないため、
 * このクラスの指し手生成部では、合駒をする手は生成されません。
 */
class EvasionPicker {
 public:
  EvasionPicker(const Position& pos);

  /**
   * 次の手が存在する場合は、trueを返します.
   */
  bool has_next() {
    return cur_ != end_ || GenerateNext();
  }

  /**
   * 次の手を返します.
   */
  Move next_move() {
    assert(cur_ != end_);
    return (cur_++)->move;
  }

 private:

  /**
   * 指し手生成の段階です.
   * 実際に指し手を生成する順番に並んでいます。
   */
  enum Stage {
    /** 玉で王手駒を取る手 */
    kKingCapturesOfChecker = 0,

    /** 玉以外の駒で王手駒を取る手 */
    kCapturesOfChecker,

    /** 玉で王手駒以外の駒を取る手 */
    kKingCaptures,

    /** 玉が駒を取らずに移動する手 */
    kKingNonCaptures,

    /** 指し手生成をストップする */
    kStop
  };

  /**
   * 次のカテゴリの手を生成します.
   * @return 指し手を１手以上生成した場合は、true
   */
  bool GenerateNext();

  ExtMove* cur_;
  ExtMove* end_;
  const Position& pos_;
  Stage stage_ = kKingCapturesOfChecker;
  const Color stm_;
  const Square ksq_;
  Bitboard attacked_by_checkers_;
  Array<ExtMove, Move::kMaxLegalEvasions> moves_;
};

#endif /* MATE3_H_ */
