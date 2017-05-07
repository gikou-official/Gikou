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

#ifndef PROOFPIECE_H_
#define PROOFPIECE_H_

#include "hand.h"
#include "move.h"
class Position;

/**
 * 証明駒（受け方の玉を詰ますのに最低限必要な、攻め方の持ち駒）を扱うためのクラスです.
 *
 * 証明駒についての解説は、以下の文献が参考になると思います。
 *
 * （参考文献）
 *   - 岸本章宏: 詰将棋を解くための探索技術について, 人工知能学会誌, Vol.26, No.4,
 *     pp.392-398, 2011.
 */
class ProofPieces {
 public:
  /**
   * リーフノードにおける証明駒を返します.
   * @param pos リーフノードの局面
   * @return 証明駒
   */
  static Hand AtLeaf(const Position& pos);

  /**
   * フロンティアノード（リーフノードの１つ上のノード）における証明駒を返します.
   * @param pos       フロンティアノードの局面
   * @param mate_move フロンティアノードにおいて、受け方の玉を詰ます手
   * @return 証明駒
   */
  static Hand AtFrontier(const Position& pos, Move mate_move);

  /**
   * 内部ノードにおける、攻め方の証明駒を返します.
   * @param child 子ノードの証明駒
   * @param move  子ノードに遷移する指し手
   * @return 現ノードの証明駒
   */
  static Hand AtAttackSide(Hand child, Move move);
};

/**
 * 反証駒（攻め方の詰手順を逃れるために最低限必要な、受け方の持ち駒）を扱うためのクラスです.
 */
class DisproofPieces {
 public:
  /**
   * リーフノードにおける反証駒を返します.
   * @param pos リーフノードの局面
   * @return 反証駒
   */
  static Hand AtLeaf(const Position& pos);

  /**
   * 内部ノードにおける、受け方の反証駒を返します.
   * @param child 子ノードの反証駒
   * @param move  子ノードに遷移する指し手
   * @return 現ノードの反証駒
   */
  static Hand AtDefenseSide(Hand child, Move move);
};

inline Hand DisproofPieces::AtDefenseSide(Hand child, Move move) {
  // 証明駒と反証駒の対称性を利用可能
  return ProofPieces::AtAttackSide(child, move);
}

#endif /* PROOFPIECE_H_ */
