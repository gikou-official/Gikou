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

#ifndef MOVE_FEATURE_H_
#define MOVE_FEATURE_H_

#include <vector>
#include "common/array.h"
#include "bitboard.h"
#include "move.h"
class Position;
class HistoryStats;
class GainsStats;

// 指し手の特徴のIDに使う型を定義します
typedef uint32_t MoveFeatureIndex;

/**
 * 指し手の特徴のリストです.
 */
struct MoveFeatureList : std::vector<MoveFeatureIndex> {
  double history; // history値は連続値なので、別途保存場所を用意する
};

extern const int kNumMoveFeatures;
extern const int kHistoryFeatureIndex;

/**
 * 指し手の特徴を抽出する際に用いる、局面の情報です.
 */
struct PositionInfo {
  PositionInfo(const Position&, const HistoryStats&, const GainsStats&);

  /** history値の統計 */
  const HistoryStats& history;

  /** gain値の統計 */
  const GainsStats& gains;

  /** 敵の利きが付いているマス */
  Bitboard attacked_squares;

  /** 味方の利きが付いているマス */
  Bitboard defended_squares;

  /** 当たりをかけている、最も価値の高い敵の駒 */
  Bitboard most_valuable_victim;

  /** ピンしている相手の駒 */
  Bitboard opponent_pinned_pieces;

  /** 当たりになっている味方の駒 */
  Bitboard threatened_pieces;

  /** 当たりになっている駒で、最も価値の高い味方の駒 */
  Bitboard most_valuable_threatened_piece;

  /** 直前に動いた駒で取られそうな、最も価値の高い味方の駒 */
  Bitboard pieces_attacked_by_last_move;

  /** 直前に動いた駒で取られそうな味方の駒を、合駒して守る手 */
  Bitboard intercept_attacks_by_last_move;

  /** 自玉の8近傍で、敵の利きがあり、かつ敵の利きが味方の利き数を上回っているマス */
  Bitboard dangerous_king_neighborhood_squares;

  /** 相手が持ち駒で自玉に有効王手をかけることができるマス */
  Bitboard opponent_effective_drop_checks;

  /** 敵玉の8近傍のマス */
  Bitboard opponent_king_neighborhoods8;

  /** 敵玉の24近傍のマス */
  Bitboard opponent_king_neighborhoods24;

  /** 敵玉24近傍にある敵の金 */
  Bitboard opponent_king_neighborhood_golds;

  /** 敵玉24近傍にある敵の銀 */
  Bitboard opponent_king_neighborhood_silvers;
};

/**
 * 指し手の特徴を抽出します.
 *
 * 抽出すべき指し手の特徴を決めるにあたっては、以下の文献も参考にしています。
 *
 * （参考文献）
 *   - 山下宏: YSS--『コンピュータ将棋の進歩２』以降の改良点, 『コンピュータ将棋の進歩５』,
 *     pp.14-20, 共立出版, 2005.
 *   - 橋本剛: 将棋プログラムTACOSのアルゴリズム, 『コンピュータ将棋の進歩５』, pp.39-46,
 *     共立出版, 2005.
 *   - 大槻知史: コンピュータ将棋プログラム「大槻将棋」, 『コンピュータ将棋の進歩６』, pp.53-57,
 *     共立出版, 2012.
 *   - 鶴岡慶雅: 「激指」の最近の改良について --コンピュータ将棋と機械学習--,
 *     『コンピュータ将棋の進歩６』, pp.72-77, 共立出版, 2012.
 *
 * @param move     特徴を抽出したい指し手
 * @param pos      現局面
 * @param pos_info 現局面の情報
 * @return 抽出された、指し手の特徴のリスト
 */
MoveFeatureList ExtractMoveFeatures(const Move move, const Position& pos,
                                    const PositionInfo& pos_info);

#endif /* MOVE_FEATURE_H_ */
