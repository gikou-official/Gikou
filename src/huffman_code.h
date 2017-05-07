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

#ifndef HUFFMAN_CODE_H_
#define HUFFMAN_CODE_H_

#include "common/array.h"
#include "position.h"

/**
 * 局面情報をハフマン符号化するためのクラスです.
 *
 * ハフマン符号化することにより、任意の将棋の局面を256ビットに圧縮することができます。
 *
 * （参考文献）
 *   - 磯崎元洋: 将棋の局面を256bitに圧縮するには？, やねうら王公式サイト,
 *     http://yaneuraou.yaneu.com/2016/07/02/将棋の局面を256bitに圧縮するには？/, 2016.
 *   - Wikipedia: ハフマン符号, https://ja.wikipedia.org/wiki/ハフマン符号.
 */
class HuffmanCode {
 public:
  HuffmanCode() {}

  explicit HuffmanCode(const Array<uint64_t, 4>& array)
      : array_(array) {
  }

  /**
   * ハフマン符号のデータに直接アクセスするためのメソッドです.
   */
  const Array<uint64_t, 4>& array() const {
    return array_;
  }

  /**
   * 局面をハフマン符号を用いて符号化します.
   */
  static HuffmanCode EncodePosition(const Position& pos);

  /**
   * 局面のハフマン符号を復号化して、局面を作成します.
   */
  static Position DecodePosition(const HuffmanCode& huffman_code);

  static void Init();

 private:
  Array<uint64_t, 4> array_;
};

static_assert(sizeof(HuffmanCode) == 32, "");

#endif /* HUFFMAN_CODE_H_ */
