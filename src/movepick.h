/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016 Yosuke Demura
 * except where otherwise indicated.
 *
 * The MovePicker class below is derived from Stockfish 6.
 * Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 * Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish author)
 * Copyright (C) 2016 Yosuke Demura
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

#ifndef MOVEPICK_H_
#define MOVEPICK_H_

#include "common/array.h"
#include "movegen.h"
#include "search.h"

/**
 * 指し手の逐次生成を担当するクラスです.
 */
class MovePicker {
 public:
  static constexpr Depth kDepthQsChecks     =  0 * kOnePly;
  static constexpr Depth kDepthQsNoChecks   =  0 * kOnePly;
  static constexpr Depth kDepthQsRecaptures = -5 * kOnePly;

  /**
   * 通常探索用のコンストラクタです.
   */
  MovePicker(const Position& pos, const HistoryStats& history,
             const GainsStats& gains, Depth depth, Move hash_move,
             const Array<Move, 2>& killermoves,
             const Array<Move, 2>& countermoves,
             const Array<Move, 2>& followupmoves, Search::Stack* ss);

  /**
   * 静止探索用のコンストラクタです.
   */
  MovePicker(const Position& pos, const HistoryStats& history,
             const GainsStats& gains, Depth depth, Move hash_move);

  /**
   * ProbCut用のコンストラクタです.
   */
  MovePicker(const Position& pos, const HistoryStats& history,
             const GainsStats& gains, Move hash_move);

  /**
   * 次の手（残りの手の中で、最もβカットの可能性が高い手）を返します.
   * @param probability 指し手の実現確率（を保存するためのポインタ）
   * @return 次の手（残りの手の中で、最もβカットの可能性が高い手）
   */
  Move NextMove(double* probability);

 private:
  /**
   * 後で指し手をソートするため、指し手に得点を付与します.
   */
  template<GeneratorType> void ScoreMoves();

  /**
   * 次のカテゴリの指し手を生成します.
   */
  void GenerateNext();

  const Position& pos_;
  const HistoryStats& history_;
  const GainsStats& gains_;
  Search::Stack* ss_ = nullptr;
  ExtMove* cur_;
  ExtMove* end_;
  ExtMove* end_quiets_;
  ExtMove *end_bad_captures_;
  int stage_;
  Score capture_threshold_;
  Depth depth_;
  Move hash_move_ = kMoveNone;
  const Array<Move, 2> killermoves_;
  const Array<Move, 2> countermoves_;
  const Array<Move, 2> followupmoves_;
  Array<ExtMove, 6> killers_;
  Array<ExtMove, Move::kMaxLegalMoves> moves_;
};

#endif /* MOVEPICK_H_ */
