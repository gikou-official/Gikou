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

#ifndef COMMON_SEQUENCE_H_
#define COMMON_SEQUENCE_H_

#include "iterator.h"

/**
 * min_valueからmax_valueまでの、連続した値を表すクラスです.
 *
 * 範囲for文を使って、範囲内の全要素を簡単に列挙できます。
 *
 * 使用例：
 * @code
 * // 0から7までの連続した値
 * Sequence<int> seq(0, 7);
 *
 * // 範囲for文で列挙する
 * for (int x : seq) {
 *   std::printf("%d ", x);
 * }
 * // => "0 1 2 3 4 5 6 7 "と出力される
 * @endcode
 */
template<typename Incrementable>
class Sequence {
 public:
  Sequence(Incrementable min_value, Incrementable max_value)
      : min_(min_value), max_(max_value) {
  }

  Incrementable min() const {
    return min_;
  }

  Incrementable max() const {
    return max_;
  }

  CountingIterator<Incrementable> begin() const {
    return CountingIterator<Incrementable>(min_);
  }

  CountingIterator<Incrementable> end() const {
    Incrementable temp = max_;
    return CountingIterator<Incrementable>(++temp);
  }

 private:
  Incrementable min_, max_;
};

#endif /* COMMON_SEQUENCE_H_ */
