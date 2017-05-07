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

#ifndef COMMON_ARRAYMAP_H_
#define COMMON_ARRAYMAP_H_

#include <cassert>
#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include "limits.h"

namespace arraymap {

template<typename, typename ...> class ArrayMap;
template<typename, typename ...> struct ValueType;

template<typename T, typename Key, typename ...Key2>
struct ValueType<T, Key, Key2...> {
  typedef ArrayMap<T, Key2...> type;
};

template<typename T, typename Key>
struct ValueType<T, Key> {
  typedef T type;
};

/**
 * 配列を用いて実装された、キーを値にマッピングするためのクラスです.
 *
 * C++のネイティブ配列と比較したときの利点：
 *   - 予めキーの値域をLimitsクラスを用いて定義しておけば、配列の要素数を自動で決定してくれる
 *   - キーの型が正しいかコンパイラによるチェックが入るので、バグを発見しやすい
 *
 * 注意: キー（テンプレートパラメータのKey, Key2...）は、Limitsクラスを用いて予め値域を定義
 * しておく必要があります。Limitsクラスで値域を定義する方法については、limits.hを参照してください。
 *
 * 使用例：
 * @code
 * // Squareクラスをキーとし、int型を要素とするコンテナ
 * ArrayMap<int, Square> square_map; // 内部的には、要素数81の配列が確保される
 *
 * square_map[kSquare1A] = 13;    // １一のマスに値をセットする
 * int x = square_map[kSquare5C]; // ５三のマスから値を取得する
 *
 * // ArrayMap<int, Square>型の場合、Square型がキーになっているので、
 * // Square型以外で要素にアクセスすることはできない
 * square_map[17] = 10; // コンパイルエラー！
 *
 * // ArrayMap<long, File, Rank>は、ArrayMap<ArrayMap<long, Rank>, File>と同じ意味
 * ArrayMap<long, File, Rank> fr_map; // 内部的には、9*9の２次元配列が確保される
 *
 * fr_map[kFile1][kRank7] = 17;     // 値をセットする
 * long y = fr_map[kFile1][kRank7]; // 値を取得する
 *
 * // キーの順序を逆にすることはできない
 * fr_map[kRank8][kFile2] = 21; // コンパイルエラー！
 * @endcode
 *
 * @see Limits
 */
template<typename T, typename Key, typename ...Key2>
class ArrayMap<T, Key, Key2...> {
 public:
  // キーとして使われるクラスは、size_t型に暗黙の型変換ができなければならない
  static_assert(std::is_convertible<Key, size_t>::value, "");

  typedef typename ValueType<T, Key, Key2...>::type value_type;

  constexpr ArrayMap() {
    // 速度低下を防止するため、特にゼロ初期化等は行わない
  }

  ArrayMap(std::initializer_list<value_type> list) {
    assert(list.size() <= kArraySize);
    std::copy(list.begin(), list.end(), begin());
  }

  ArrayMap(std::initializer_list<std::pair<Key, value_type>> list) {
    for (const auto& pair : list) {
      auto key   = pair.first;
      auto value = pair.second;
      assert(key_is_ok(key));
      array_[key_to_index(key)] = value;
    }
  }

  value_type* begin() {
    return &array_[0];
  }

  value_type* end() {
    return begin() + kArraySize;
  }

  constexpr const value_type* begin() const {
    return &array_[0];
  }

  constexpr const value_type* end() const {
    return begin() + kArraySize;
  }

  value_type& operator[](const Key& key) {
    assert(key_is_ok(key)); // デバッグ・ビルド時は、範囲外アクセスのチェックが入る
    return array_[key_to_index(key)];
  }

  const value_type& operator[](const Key& key) const {
    assert(key_is_ok(key)); // デバッグ・ビルド時は、範囲外アクセスのチェックが入る
    return array_[key_to_index(key)];
  }

  constexpr size_t size() const {
    return kArraySize;
  }

  void clear() {
    std::memset(&array_[0], 0, sizeof(value_type) * size());
  }

 private:
  static constexpr size_t kArraySize = Limits<Key>::max() - Limits<Key>::min() + 1;

  static constexpr bool key_is_ok(Key k) {
    return k >= Limits<Key>::min() && k <= Limits<Key>::max();
  }

  static constexpr size_t key_to_index(Key k) {
    return static_cast<size_t>(k - Limits<Key>::min());
  }

  value_type array_[kArraySize];
};

}  // namespace arraymap

using arraymap::ArrayMap;

#endif /* COMMON_ARRAYMAP_H_ */
