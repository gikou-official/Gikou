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

#ifndef SHARED_DATA_H_
#define SHARED_DATA_H_

#include "hash_table.h"
#include "signals.h"

/**
 * ルート局面における指し手の情報を保存するためのクラスです。
 */
class RootMove {
 public:
  RootMove(Move m)
      : move(m),
        pv{m} {
  }
  bool operator<(const RootMove& rm) const { return score < rm.score; }
  bool operator>(const RootMove& rm) const { return score > rm.score; }
  bool operator==(Move m) const { return move == m; }

  /** 指し手 */
  Move move;

  /** この指し手を探索して得られた評価値 */
  Score score = -kScoreInfinite;

  /** 反復深化探索における、１イテレーション前の評価値 */
  Score previous_score = -kScoreInfinite;

  /** その手以下を探索したノード数 */
  uint64_t nodes = 0;

  /** その手のPV */
  std::vector<Move> pv;
};

/**
 * 複数の探索スレッドで共有するデータをひとまとめにしたクラスです。
 */
struct SharedData {
  /** 置換表 */
  HashTable hash_table;

  /** 探索停止等の指示を出すシグナル */
  Signals signals;
};

#endif /* SHARED_DATA_H_ */
