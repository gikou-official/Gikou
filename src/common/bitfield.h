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

#ifndef COMMON_BITFIELD_H_
#define COMMON_BITFIELD_H_

#include <cassert>
#include <cstddef>
#include <type_traits>

/**
 * 符号なし整数型を内部実装とする、ビットフィールドです.
 *
 * ひとつの符号なし整数型に、複数の値を保存する場合に向いています。
 * 技巧では、Handクラス及びMoveクラスの内部実装に使用されています。
 *
 * なお、デバッグ・ビルド時には、値域チェック機能が付いているので、バグを発見しやすくなっています。
 */
template<typename Unsigned>
class BitField {
 public:
  static_assert(std::is_unsigned<Unsigned>::value, "");

  /**
   * ビットフィールドにアクセスするためのキーです.
   */
  class Key {
   public:
    /**
     * ビットフィールドのどの範囲に値を保存するかを指定して、キーを作成します.
     *
     * @param begin 何ビット目から値を保存するか（begin番目のビットを含む）
     * @param end   何ビット目まで値を保存するか（end番目のビットは含まない）
     *
     * 例えば、ビットフィールドのうち、3ビット目から5ビット目まで使用する場合は、
     * @code
     *   Key key(3, 6);
     * @endcode
     * のように、キーを作成します。
     */
    constexpr Key(size_t begin, size_t end)
        : mask(make_mask(begin, end)),
          shift(begin),
          first_bit(static_cast<Unsigned>(1) << begin),
          reset_mask(~make_mask(begin, end)) {
    }
    bool ValueIsOk(Unsigned value) const {
      return value >= min() && value <= max();
    }
    const Unsigned mask;
    const Unsigned shift;
    const Unsigned first_bit;
    const Unsigned reset_mask;
   private:
    constexpr Unsigned min() const {
      return 0;
    }
    constexpr Unsigned max() const {
      return mask >> shift;
    }
    static constexpr Unsigned make_mask(size_t begin, size_t end) {
      return make_mask(end) - make_mask(begin);
    }
    static constexpr Unsigned make_mask(size_t end) {
      return end == 8 * sizeof(Unsigned)
           ? ~static_cast<Unsigned>(0)
           : (static_cast<Unsigned>(1) << end) - static_cast<Unsigned>(1);
    }
  };

  constexpr BitField() {
    // 速度低下を防止するため、特にゼロ初期化等は行わない
  }

  explicit constexpr BitField(Unsigned b)
      : bitfield_(b) {
  }

  constexpr operator Unsigned() const {
    return bitfield_;
  }

  /**
   * ひとつでもビットが１になっていれば、trueを返します.
   */
  bool any() const {
    return bitfield_ != 0;
  }

  /**
   * すべてのビットがゼロになっていれば、trueを返します.
   */
  bool none() const {
    return !any();
  }

  /**
   * keyで指定された範囲のビットがひとつでも１になっていれば、trueを返します.
   */
  bool test(Key key) const {
    return bitfield_ & key.mask;
  }

  /**
   * keyで指定された範囲に保存された値を返します.
   */
  Unsigned operator[](Key key) const {
    return (bitfield_ & key.mask) >> key.shift;
  }

  /**
   * ビットフィールドの内部のビットをすべてゼロに初期化します.
   */
  BitField& reset() {
    bitfield_ = 0;
    return *this;
  }

  /**
   * keyで指定された範囲のビットをすべてゼロにリセットします.
   */
  BitField& reset(Key key) {
    bitfield_ &= key.reset_mask;
    return *this;
  }

  /**
   * keyで指定された範囲に、valueの値をセットします.
   */
  BitField& set(Key key, Unsigned value) {
    assert(key.ValueIsOk(value));
    reset(key);
    bitfield_ |= (value << key.shift);
    return *this;
  }

  /**
   * keyで指定された範囲の値に、１を加算します.
   */
  BitField& increment(Key key) {
    assert(key.ValueIsOk((*this)[key] + static_cast<Unsigned>(1)));
    bitfield_ += key.first_bit;
    return *this;
  }

  /**
   * keyで指定された範囲の値から、１を減算します.
   */
  BitField& decrement(Key key) {
    assert(key.ValueIsOk((*this)[key] - static_cast<Unsigned>(1)));
    bitfield_ -= key.first_bit;
    return *this;
  }

 private:
  Unsigned bitfield_;
};

#endif /* COMMON_BITFIELD_H_ */
