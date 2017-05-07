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

#ifndef COMMON_BITSET_H_
#define COMMON_BITSET_H_

#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include "bitop.h"
#include "iterator.h"

/**
 * 符号なし整数を用いて実装された、ビットセットです.
 *
 * BitIteratorを用いて高速に内部の値を列挙できるのが特徴です。
 *
 * 使用例：
 * @code
 * // 駒の集合を定義
 * BitSet<Piece, 32> piece_set;
 *
 * // 値のセット
 * piece_set.set(kBlackPawn);
 * piece_set.set(kWhiteKnight);
 *
 * // BitIteratorを用いた値の参照
 * for (BitIterator<Piece> i = piece_set.begin(); i != piece_set.end(); ++i) {
 *   Piece piece = *i;
 *   std::printf("%s ", piece.ToSfen().c_str());
 * }
 * // => "P n " と出力される
 *
 * // 同じことを範囲for文を用いて書くこともできます
 * for (Piece piece : piece_set) {
 *   std::printf("%s ", piece.ToSfen().c_str());
 * }
 * // => "P n " と出力される
 * @endcode
 */
template<typename Key, size_t kSize, typename Unsigned = unsigned>
class BitSet {
 public:
  static_assert(std::is_convertible<Key, size_t>::value, "");
  static_assert(kSize != 0 && kSize <= 8 * sizeof(Unsigned), "");
  static_assert(std::is_unsigned<Unsigned>::value, "");

  typedef BitIterator<Key, Unsigned> const_iterator;

  explicit constexpr BitSet(Unsigned n = 0)
      : bitset_(n) {
  }

  template<typename ... Args>
  explicit constexpr BitSet(Key key1, Args ... key2)
      : bitset_(make_mask(key1, key2...)) {
  }

  operator Unsigned() const {
    assert(is_ok());
    return bitset_;
  }

  bool operator[](Key key) const {
    return test(key);
  }

  bool test(Key key) const {
    assert(key_is_ok(key));
    return bitset_ & make_mask(key);
  }

  bool all() const {
    return bitset_ == kMaskAll;
  }

  bool any() const {
    return bitset_;
  }

  bool none() const {
    return !any();
  }

  int count() const {
    static_assert(sizeof(bitset_) <= sizeof(uint64_t), "");
    return bitop::popcnt64(bitset_);
  }

  BitSet& operator&=(BitSet rhs) {
    assert(rhs.is_ok());
    bitset_ &= rhs.bitset_;
    return *this;
  }

  BitSet& operator|=(BitSet rhs) {
    assert(rhs.is_ok());
    bitset_ |= rhs.bitset_;
    return *this;
  }

  BitSet& operator^=(BitSet rhs) {
    assert(rhs.is_ok());
    bitset_ ^= rhs.bitset_;
    return *this;
  }

  BitSet operator~() const {
    return BitSet(~bitset_ & kMaskAll);
  }

  BitSet operator|(BitSet rhs) const {
    return BitSet(*this) |= rhs;
  }

  BitSet operator&(BitSet rhs) const {
    return BitSet(*this) &= rhs;
  }

  BitSet operator^(BitSet rhs) const {
    return BitSet(*this) ^= rhs;
  }

  BitSet& set() {
    bitset_ |= kMaskAll;
    return *this;
  }

  BitSet& set(Key key) {
    assert(key_is_ok(key));
    bitset_ |= make_mask(key);
    return *this;
  }

  BitSet& set(Key key, bool value) {
    assert(key_is_ok(key));
    bitset_ |= static_cast<Unsigned>(value) << key;
    return *this;
  }

  BitSet& reset() {
    bitset_ = 0;
    return *this;
  }

  BitSet& reset(Key key) {
    assert(key_is_ok(key));
    bitset_ &= ~make_mask(key);
    return *this;
  }

  const_iterator begin() const {
    return const_iterator(bitset_);
  }

  const_iterator end() const {
    return const_iterator(0);
  }

  static constexpr Unsigned min() {
    return static_cast<Unsigned>(0);
  }

  static constexpr Unsigned max() {
    return kMaskAll;
  }

  constexpr bool is_ok() const {
    return bitset_ >= min() && bitset_ <= max();
  }

 private:
  static constexpr Unsigned kMaskAll = kSize == 8 * sizeof(Unsigned)
                                     ? ~static_cast<Unsigned>(0)
                                     : (static_cast<Unsigned>(1) << kSize)
                                       - static_cast<Unsigned>(1);
  static_assert(bitop::more_than_one_bit(kMaskAll), "");

  static constexpr bool key_is_ok(Key key) {
    return static_cast<size_t>(key) < kSize;
  }

  static constexpr Unsigned make_mask(Key key) {
    return static_cast<Unsigned>(1) << key;
  }

  template<typename ... Args>
  static constexpr Unsigned make_mask(Key key1, Args ... key2) {
    return make_mask(key1) | make_mask(key2...);
  }

  Unsigned bitset_;
};

#endif /* COMMON_BITSET_H_ */
