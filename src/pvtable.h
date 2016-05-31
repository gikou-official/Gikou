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

#ifndef PVTABLE_H_
#define PVTABLE_H_

#include "common/array.h"
#include "move.h"

/**
 * 探索中にPV（Principal Variation）を保存しておくためのテーブルです.
 */
class PvTable {
 public:

  PvTable() {
    pv_length_[0] = 0;
  }

  Move operator[](int ply) const {
    assert(0 <= ply && ply < static_cast<int>(size()));
    return pv_[0][ply];
  }

  size_t size() const {
    return pv_length_[0];
  }

  const Move* begin() const {
    return &pv_[0][0];
  }

  const Move* end() const {
    return begin() + size();
  }

  void ClosePv(int ply) {
    assert(0 <= ply && ply <= kMaxPly);
    pv_length_[ply] = ply;
  }

  void CopyPv(Move move, int ply) {
    assert(move != kMoveNone);
    assert(0 <= ply && ply < kMaxPly);

    int length = pv_length_[ply + 1];
    assert(0 <= length && length <= kMaxPly);

    pv_[ply][ply] = move;
    pv_length_[ply] = length;

    for (int i = ply + 1; i < length; ++i) {
      pv_[ply][i] = pv_[ply + 1][i];
    }
  }

  void Clear() {
    // Logical Clear
    pv_length_[0] = 0;
  }

 private:
  Array<Move, kMaxPly + 1, kMaxPly + 1> pv_;
  Array<int, kMaxPly + 1> pv_length_;
};

#endif /* PVTABLE_H_ */
