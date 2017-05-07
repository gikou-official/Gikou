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

#ifndef NODE_H_
#define NODE_H_

#include <vector>
#include "evaluation.h"
#include "position.h"
#include "psq.h"
#include "zobrist.h"
class Search;
class HashTable;

/**
 * 局面クラスに、評価値計算やハッシュ値計算、千日手検出などの機能を追加したものです.
 * αβ探索（search.cc）において用いることが想定されています。
 */
class Node : public Position {
 public:
  /**
   * 任意の局面を用いて初期化を行います.
   * @param pos 初期化に用いる局面
   */
  Node(const Position& pos)
      : Position(pos),
        stack_(3), // (stack_.back() - 2)を参照可能にする
        psq_list_(pos) {
    Initialize();
  }

  /**
   * 任意の局面を用いて初期化を行います.
   * @param pos 初期化に用いる局面
   */
  Node(Position&& pos)
      : Position(pos),
        stack_(3), // (stack_.back() - 2)を参照可能にする
        psq_list_(pos) {
    Initialize();
  }

  /**
   * 千日手の検出を行います.
   *
   * scoreに保存される可能性のあるスコアは以下のとおりです。
   *   - 通常の千日手（引き分け）: kScoreDraw
   *   - 連続王手の千日手（手番側の反則）: -kScoreFoul
   *   - 連続王手の千日手（相手側の反則）: kScoreFoul
   *   - 盤上の駒はそのままに、手番側の持ち駒が増加する局面: kScoreSuperior
   *   - 盤上の駒はそのままに、手番側の持ち駒が減少する局面: -kScoreSuperior
   *
   * 最後の2つは、Strong Horizon Effect Killer (SHEK) に関するものです。
   * これは、千日手処理そのものではなく、明らかに得な局面・明らかに損な局面への遷移を検出するための処理です。
   * （参考文献）
   *   - 橋本剛: 将棋プログラムTACOSのアルゴリズム, 『コンピュータ将棋の進歩５』, pp.56-60,
   *     共立出版, 2005.
   *
   * @param score 千日手が検出された場合には、kScoreDrawなどのスコアを保存します
   * @return 千日手が検出された場合は、true
   */
  bool DetectRepetition(Score* score) const;

  /**
   * Zobrist Hashingにより計算された、現在の局面のハッシュ値を返します.
   * @return 64ビットのハッシュキー
   */
  Key64 key() const {
    return stack_.back().position_key;
  }

  /**
   * 指し手 move で１手進めた局面のハッシュキーを返します.
   * 例えば、置換表の投機的プリフェッチを行う際に用いられます。
   * @param move ハッシュ値を計算する局面を導く手
   * @return 指し手 move で１手進めた局面のハッシュキー
   */
  Key64 key_after(Move move) const {
    return key() + ComputeKey(move);
  }

  /**
   * １手パスした後の局面のハッシュキーを返します.
   */
  Key64 key_after_null_move() const {
    return key() + Zobrist::null_move(side_to_move());
  }

  /**
   * シンギュラー延長の探索時に用いるハッシュキーを返します.
   */
  Key64 exclusion_key() const {
    return key() + Zobrist::exclusion();
  }

  /**
   * 現在の局面の評価値を返します.
   * @param progress 進行度を出力するためのポインタです
   * @return 現在の局面の評価値（-kScoreMaxEvalからkScoreMaxEvalの値をとります）
   */
  Score Evaluate(double* progress = nullptr);

  /**
   * 指定された指し手を用いて局面を１手先に進めます（簡易版）.
   * move_gives_check及びkey_after_moveを引数として渡す関数と比較して、若干実行速度が落ちます。
   * @param move 指し手
   */
  void MakeMove(Move move) {
    Node::MakeMove(move, Position::MoveGivesCheck(move), key_after(move));
  }

  /**
   * 指定された指し手を用いて局面を１手先に進めます.
   * @param move             指し手
   * @param move_gives_check 指し手が王手かどうか
   * @param key_after_move   指し手を１手進めたときのハッシュ値
   */
  void MakeMove(Move move, bool move_gives_check, Key64 key_after_move);

  /**
   * 指定された指し手を用いて局面を１手前に戻します.
   * @param move 指し手（直前に行ったMakeMoveに渡した指し手と同じ指し手でなければなりません）
   */
  void UnmakeMove(Move move);

  /**
   * １手パスを行い、相手に手番を渡します.
   * 将棋のルール上はパスはありませんが、Null Move Pruning等を行うのに使います。
   */
  void MakeNullMove();

  /**
   * パスした局面を元に戻します.
   * MakeNullMoveを行うことなくMakeNullMoveを呼んだ場合、その動作は未定義です。
   */
  void UnmakeNullMove();

 private:

  struct Stack {
    PsqControlList psq_control_list;
    EvalDetail eval_detail;
    Key64 board_key;
    Key64 position_key;
    Hand hand;
    int plies_from_null   = 0;
    int continuous_checks = 0;
    bool eval_is_updated = false;
  };

  void Initialize();

  Key64 ComputeKey(Move move) const;

  std::vector<Stack> stack_;
  PsqList psq_list_;
};

#endif /* NODE_H_ */
