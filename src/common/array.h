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

#ifndef COMMON_ARRAY_H_
#define COMMON_ARRAY_H_

#include <cassert>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <initializer_list>

namespace array {

template<typename, size_t...> class Array;
template<typename, size_t...> struct ValueType;

template<typename T, size_t kSize, size_t ...kSize2>
struct ValueType<T, kSize, kSize2...> {
  typedef Array<T, kSize2...> type;
};

template<typename T, size_t kSize>
struct ValueType<T, kSize> {
  typedef T type;
};

/**
 * assertマクロによる境界チェックが付いた、多次元配列です.
 *
 * デバッグ・ビルド時（NDEBUGマクロが定義されていないとき）は、添字の範囲が配列の大きさを超えていないか
 * チェックされるため、バグを発見しやすくなります。
 *
 * 内部実装にはC++のネイティブ配列を用いているため、実行速度はネイティブ配列と同等です。
 *
 * 使用例：
 * @code
 * // 使用例１：int型が32個の配列
 * Array<int, 32> int_array;
 * int_array[12] = 3; // 配列のように代入可能
 * int x = int_array[12]; // 配列のように参照可能
 * int y = int_array[64]; // 範囲外アクセスを行っているため、デバッグ・ビルド時はassertマクロにひっかかる
 *
 * // 使用例２：long型の16*64の２次元配列
 * Array<long, 16, 64> long_array;
 * Array<long, 64>& inner_array = long_array[1]; // 配列の配列という構造をとっています
 * long_array[12][34] = 5; // ２次元配列のように代入可能
 * int x = long_array[12][34]; // ２次元配列のように参照可能
 * @endcode
 */
template<typename T, size_t kSize, size_t ...kSize2>
class Array<T, kSize, kSize2...> {
 public:

  static_assert(kSize > 0, "");

  typedef typename ValueType<T, kSize, kSize2...>::type value_type;

  constexpr Array() {
    // 速度低下を防止するため、特にゼロ初期化等は行わない
  }

  Array(std::initializer_list<value_type> list) {
    std::copy(list.begin(), list.end(), begin());
  }

  value_type* begin() {
    return &array_[0];
  }

  value_type* end() {
    return begin() + kSize;
  }

  constexpr const value_type* begin() const {
    return &array_[0];
  }

  constexpr const value_type* end() const {
    return begin() + kSize;
  }

  constexpr size_t size() const {
    return kSize;
  }

  value_type& operator[](size_t n) {
    assert(n < kSize); // デバッグ・ビルド時は、範囲外アクセスのチェックが入る
    return array_[n];
  }

  const value_type& operator[](size_t n) const {
    assert(n < kSize); // デバッグ・ビルド時は、範囲外アクセスのチェックが入る
    return array_[n];
  }

  void clear() {
    std::memset(&array_[0], 0, sizeof(value_type) * kSize);
  }

 private:
  value_type array_[kSize];
};

}  // namespace array

using array::Array;

#endif /* COMMON_ARRAY_H_ */
