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

#ifndef ZOBRIST_H_
#define ZOBRIST_H_

#include "common/array.h"
#include "common/arraymap.h"
#include "piece.h"
#include "square.h"
#include "types.h"

/**
 * Zobristハッシュのシードを保存しておくテーブルです.
 */
class Zobrist {
 public:

  enum {
    kPathTableSize = 16
  };

  /**
   * ハッシュ値のシードを乱数で初期化します.
   */
  static void Init();

  /**
   * 通常とは異なる場所に保存するたためのハッシュ値です.
   */
  static Key64 exclusion() {
    return exclusion_;
  }

  /**
   * パス（null move）のハッシュ値を返します.
   */
  static Key64 null_move(Color c) {
    return null_move_[c];
  }

  /**
   * 手番を表すハッシュ値を返します.
   */
  static Key64 initial_side(Color c) {
    return c == kBlack ? Key64(0) : null_move_[kBlack];
  }

  /**
   * 駒とマスの組み合わせに対応するハッシュ値を返します.
   */
  static Key64 psq(Piece p, Square s) {
    return psq_[s][p];
  }

  /**
   * 持ち駒のハッシュ値を返します.
   */
  static Key64 hand(Piece p) {
    return hand_[p];
  }

  /**
   * 経路のハッシュ値を求めるためのハッシュ値を返します.
   */
  static Key64 path(Piece p, Square s, int ply) {
    return path_[ply % kPathTableSize][s][p];
  }

  /**
   * 経路上にあるnull moveのハッシュ値を返します.
   * plyの奇偶で手番を判定できるため、手番による区別はありません。
   */
  static Key64 null_move_on_path(int ply) {
    return null_move_on_path_[ply % kPathTableSize];
  }

 private:
  static Key64 exclusion_;
  static ArrayMap<Key64, Color> null_move_;
  static ArrayMap<Key64, Square, Piece> psq_;
  static ArrayMap<Key64, Piece> hand_;
  static Array<ArrayMap<Key64, Square, Piece>, kPathTableSize> path_;
  static Array<Key64, kPathTableSize> null_move_on_path_;
};

#endif /* ZOBRIST_H_ */
