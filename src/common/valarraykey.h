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

#ifndef COMMON_VALARRAYKEY_H_
#define COMMON_VALARRAYKEY_H_

#include <cassert>
#include <valarray>
#include "limits.h"

namespace valarraykey {

template<typename T>
constexpr size_t compute_size() {
  return Limits<T>::max() - Limits<T>::min() + 1;
}

template<typename T, typename T2, typename... T3>
constexpr size_t compute_size() {
  return compute_size<T>() * compute_size<T2, T3...>();
}

template<size_t kNumKeys, typename... Keys>
struct KeySize {
  static constexpr size_t kSize = compute_size<Keys...>();
};

template<>
struct KeySize<0> {
  static constexpr size_t kSize = 1;
};

/**
 * std::valarrayを多次元配列風に扱うためのキーを作成するクラスです.
 *
 * 使用例１：単一のキーを用いる場合
 * @code
 * // 1. 変数vaをint[81][81]と同様に扱うためのキーを作ります。
 * ValarrayKey<Square, Square> key;
 *
 * // 2. std::valarrayを準備します。
 * std::valarray<int> va(key.size()); // key.total_size() == 81*81
 *
 * // 3. ValarrayKey::operator()を使って、int[81][81]のような多次元配列風にアクセスできます。
 * int value = va[key(kSquare1A, kSquare3E)];
 * @endcode
 *
 * 使用例２：複数のキーを用いる場合
 * @code
 * // 1. valarray用のキーを２つ作ってみます
 * // なお、C++11のautoキーワードを使って、
 * //   auto key2 = key_chain.CreateKey<Piece>();
 * // のように省略して書くこともできます。
 * ValarrayKeyChain key_chain;
 * ValarrayKey<Square, Square> key1 = key_chain.CreateKey<Square, Square>();
 * ValarrayKey<Piece> key2 = key_chain.CreateKey<Piece>();
 *
 * // 2. std::valarrayを準備する
 * std::valarray<int> va(key_chain.size()); // va(81*81 + 32)
 *
 * // key1の最後の要素+1がkey2の最初の要素となっているので、変数vaは、まるで２つの多次元配列を
 * // 1つに結合したようなvalarrayになります。
 * assert(key1.size() == key2.start());
 *
 * // 3. ValarrayKey::operator()を使って、任意の場所にアクセスできます
 * int value1 = va[key1(kSquare2C, kSquare8I)]; // 参照もできますし、
 * va[key2(kBlackPawn)] = 321;                  // 代入もできます。
 * @endcode
 */
template<typename... Keys>
class ValarrayKey {
 public:
  /**
   * インデックスがゼロから始まるキーを作ります.
   */
  constexpr ValarrayKey()
      : start_(0) {
  }
  
  /**
   * インデックスがstartから始まるキーを作ります.
   */
  explicit constexpr ValarrayKey(size_t start)
      : start_(start) {
  }

  void operator=(const ValarrayKey&) = delete;

  size_t operator()(Keys... keys) const {
    assert(keys_are_ok(keys...));
    return start_ + compute_index(keys...);
  }

  operator std::slice() const {
    return std::slice(start_, size(), 1);
  }

  constexpr size_t start() const {
    return start_;
  }

  constexpr size_t size() const {
    return KeySize<sizeof...(Keys), Keys...>::kSize;
  }

 private:
  static constexpr size_t compute_index() {
    return 0;
  }

  template<typename T>
  static constexpr size_t compute_index(T key) {
    return key - Limits<T>::min();
  }

  template<typename T, typename... T2>
  static constexpr size_t compute_index(T key, T2... key2) {
    return compute_index(key) * compute_size<T2...>() + compute_index(key2...);
  }

  static constexpr bool keys_are_ok() {
    return true;
  }

  template<typename T>
  static constexpr bool keys_are_ok(T key) {
    return compute_index(key) < compute_size<T>();
  }

  template<typename T, typename... T2>
  static constexpr bool keys_are_ok(T key, T2... key2) {
    return keys_are_ok(key) && keys_are_ok(key2...);
  }

  const size_t start_;
};

/**
 * ValarrayKeyをつなげて使うための仕組みを提供するクラスです.
 */
class ValarrayKeyChain {
 public:
  /**
   * 特定の型を持ったValarrayKeyを作ります.
   *
   * この関数により作成されたValarrayKeyは、連続した値を持つようになっています。
   * 例えば、
   * @code
   * ValarrayKeyChain key_chain;
   * auto key1 = key_chain.CreateKey<Square>();
   * auto key2 = key_chain.CreateKey<Piece>();
   * @endcode
   * と書いた場合は、key1は0から80までの値をとり、key2は、81から112までの値を取ります。
   *
   * このように連続した番号をふることによって、本来ならば２つの配列も、１つのvalarrayとして
   * 扱うことができます。例えば、
   * @code
   * ValarrayKeyChain key_chain;
   * auto key1 = key_chain.CreateKey<Square>();
   * auto key2 = key_chain.CreateKey<Piece>();
   * std::valarray<int> va(key_chain.total_size_of_keys());
   * @endcode
   * と書いた場合は、変数vaは、key1とkey2の合計サイズである、81+32=113要素を持つvalarray
   * となります。
   */
  template<typename... T>
  ValarrayKey<T...> CreateKey() {
    ValarrayKey<T...> key(total_size_of_keys_);
    total_size_of_keys_ += key.size();
    return key;
  }

  /**
   * これまでに作成したキーの合計サイズを返します.
   */
  size_t total_size_of_keys() const {
    return total_size_of_keys_;
  }

 private:
  size_t total_size_of_keys_ = 0;
};

} // namespace valarraykey

using valarraykey::ValarrayKey;
using valarraykey::ValarrayKeyChain;

#endif /* COMMON_VALARRAYKEY_H_ */
