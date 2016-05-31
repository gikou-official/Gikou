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

#ifndef COMMON_TRIANGULAR_ARRAYMAP_H_
#define COMMON_TRIANGULAR_ARRAYMAP_H_

#include <cassert>
#include <cstring>
#include <type_traits>
#include "limits.h"

/**
 * 三角配列を利用した、2次元mapコンテナです.
 *
 * 内部的には2次元配列を用いて実装されています。
 * 三角配列の利点は、通常の二次元配列に比べて、メモリ使用量が約半分になることです。
 *
 * TriangularArrayMapの使用例：
 * @code
 * TriangularArrayMap<int, Square> array;
 * Square sq1 = kSquare2C, sq2 = kSquare2D;
 *
 * // at()メソッドは、std::array::at()と同様に参照を返すので、直接代入できます。
 * array.at(sq1, sq2) = 12345;
 *
 * int a = array.at(sq1, sq2); // a = 12345です。
 * int b = array.at(sq2, sq1); // インデックスを左右逆にしても同じ場所を参照するので、b=12345です。
 *
 * array.at(sq2, sq1) = 0;
 * int c = array.at(sq1, sq2); // c = 0です。
 * int d = array.at(sq2, sq1); // d = 0です。
 * @endcode
 */
template<typename T, typename Key>
class TriangularArrayMap {
  static_assert(std::is_convertible<Key, size_t>::value,
                "The template parameter Key must be implicitly convertible to size_t.");
 public:
  /**
   * デフォルトコンストラクタです（注：高速化を重視して、ゼロ初期化は行いません）.
   */
  constexpr TriangularArrayMap() {}

  T* begin() {
    return &array_[0];
  }

  T* end() {
    return begin() + kArraySize;
  }

  constexpr const T* begin() const {
    return &array_[0];
  }

  constexpr const T* end() const {
    return begin() + kArraySize;
  }

  const T& at(Key key1, Key key2) const {
    assert(key_is_ok(key1) && key_is_ok(key2));
    size_t i = key_to_index(key1);
    size_t j = key_to_index(key2);
    return i >= j ? array_[compute_index(i, j)] : array_[compute_index(j, i)];
  }

  T& at(Key key1, Key key2) {
    assert(key_is_ok(key1) && key_is_ok(key2));
    size_t i = key_to_index(key1);
    size_t j = key_to_index(key2);
    return i >= j ? array_[compute_index(i, j)] : array_[compute_index(j, i)];
  }

  const T& operator[](size_t i) const {
    assert(i < size());
    return array_[i];
  }

  T& operator[](size_t i) {
    assert(i < size());
    return array_[i];
  }

  constexpr size_t size() const {
    return kArraySize;
  }

  void clear() {
    std::memset(&array_[0], 0, sizeof(T) * size());
  }

 private:
  static bool key_is_ok(Key k) {
    return k >= Limits<Key>::min() && k <= Limits<Key>::max();
  }

  static size_t key_to_index(Key k) {
    return static_cast<size_t>(k - Limits<Key>::min());
  }

  static constexpr size_t compute_index(size_t i, size_t j) {
    return i * (i + 1)/2 + j;
  }

  static constexpr size_t kMaxIndex = Limits<Key>::max() - Limits<Key>::min();
  static constexpr size_t kArraySize = compute_index(kMaxIndex, kMaxIndex) + 1;

  T array_[kArraySize];
};

#endif /* COMMON_TRIANGULAR_ARRAYMAP_H_ */
