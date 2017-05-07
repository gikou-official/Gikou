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

#ifndef HASH_ENTRY_H_
#define HASH_ENTRY_H_

#include "move.h"
#include "types.h"

/**
 * ハッシュテーブルに保存するデータをひとまとめにしたクラスです.
 */
class HashEntry {
 public:

  enum Flag {
    /** 何もフラグが立てられていないことを示します. */
    kFlagNone  = 0x00,

    /** ３手詰め関数をスキップすべきことを知らせるフラグです. */
    kSkipMate3 = 0x10,
  };

  // flag_メンバ変数には、Boundも保存されるので、それとビットが重ならないようにする
  static_assert((kSkipMate3 & kBoundExact) == 0, "");

  /**
   * エントリが空であれば、trueを返します.
   */
  bool empty() const {
    return key32_ == 0;
  }

  /**
   * このエントリのハッシュキーを返します.
   */
  Key32 key32() const {
    return key32_;
  }

  /**
   * 最善手またはベータカットを起こした手を返します.
   */
  Move move()  const {
    return move_;
  }

  /**
   * 探索によって得た評価値を返します.
   */
  Score score() const {
    return static_cast<Score>(score_);
  }

  /**
   * 評価関数の評価値を返します.
   * score()とは異なり、探索を経た評価値ではないことに注意して下さい。
   */
  Score eval() const {
    return static_cast<Score>(eval_ );
  }

  /**
   * 探索した深さを返します.
   */
  Depth depth() const {
    return static_cast<Depth>(depth_);
  }

  /**
   * 探索によって得た評価値（score()のこと）が、上限値か下限値かそれとも正確な値かを返します.
   */
  Bound bound() const {
    return static_cast<Bound>(flags_ & kBoundExact);
  }

  /**
   * このエントリが保存された時期を返します.
   * 値が小さいほど、以前に保存されていたことを示します。
   */
  uint8_t age() const {
    return age_;
  }

  /**
   * ３手詰め関数をスキップ可能であればtrueを返します.
   */
  bool skip_mate3() const {
    return flags_ & kSkipMate3;
  }

  void set_age(uint8_t new_age) {
    age_ = new_age;
  }

  /**
    * エントリに探索によって得られたデータを保存します.
    */
  void Save(Key64 key64, Score score, Bound bound, Depth depth, Move move,
            Score eval, Flag flag, uint8_t age) {
    key32_ = key64.ToKey32();
    move_  = move;
    score_ = static_cast<int16_t>(score);
    eval_  = static_cast<int16_t>(eval);
    depth_ = static_cast<int16_t>(depth);
    flags_ = static_cast<uint8_t>(bound | flag);
    age_   = age;
  }

 private:
  friend class HashTable;

  Key32   key32_;
  Move    move_;
  int16_t score_;
  int16_t eval_;
  int16_t depth_;
  uint8_t flags_;
  uint8_t age_;
};

// TTEntryがぴったり１６バイトになっているかチェックする
static_assert(sizeof(HashEntry) == 16, "");

#endif /* HASH_ENTRY_H_ */
