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

#ifndef SQUARE_H_
#define SQUARE_H_

#include <cassert>
#include <string>
#include "common/arraymap.h"
#include "common/sequence.h"
#include "types.h"

/**
 * 将棋盤における「マス」を表すクラスです.
 *
 * 各マスには、整数値が割り当てられています。
 * 割り当てられている値は、将棋盤の右上（１一）が0で、左下（９九）が80です。
 * 具体的には、以下のように割り当てられています。
 *
 * <pre>
 *    9  8  7  6  5  4  3  2  1
 * +----------------------------+
 * | 72 63 54 45 36 27 18  9  0 | 一
 * | 73 64 55 46 37 28 19 10  1 | 二
 * | 74 65 56 47 38 29 20 11  2 | 三
 * | 75 66 57 48 39 30 21 12  3 | 四
 * | 76 67 58 49 40 31 22 13  4 | 五
 * | 77 68 59 50 41 32 23 14  5 | 六
 * | 78 69 60 51 42 33 24 15  6 | 七
 * | 79 70 61 52 43 34 25 16  7 | 八
 * | 80 71 62 53 44 35 26 17  8 | 九
 * +----------------------------+
 * </pre>
 */
class Square {
 public:
  /**
   * int型からのコンストラクタ.
   */
  explicit constexpr Square(int s = 0)
      : square_(s) {
  }

  /**
   * 筋と段からのコンストラクタ.
   */
  constexpr Square(File f, Rank r)
      : square_(static_cast<int>(f) * 9 + static_cast<int>(r)) {
  }

  /**
   * int型への変換を行います.
   */
  constexpr operator int() const {
    return square_;
  }

  /**
   * そのマスがある筋を返します.
   */
  constexpr File file() const {
    return static_cast<File>(square_ / 9);
  }

  /**
   * そのマスがある段を返します.
   */
  constexpr Rank rank() const {
    return static_cast<Rank>(square_ % 9);
  }

  /**
   * 指定された手番の駒が成ることのできるマスであれば、trueを返します.
   */
  constexpr bool is_promotion_zone_of(Color c) const {
    return c == kBlack ? rank() <= kRank3 : rank() >= kRank7;
  }

  /**
   * @deprecated 新規にコードを書くときは、rotate180()関数を利用してください。
   */
  constexpr Square inverse_square() const {
    return Square(Square::max() - square_);
  }

  /**
   * 指定された手番から見た場合の、相対的なマスの位置を返します.
   *
   * @code
   * kSquare7G.relative_square(kBlack); // => kSquare7G
   * kSquare7G.relative_square(kWhite); // => kSquare3C
   * @endcode
   */
  constexpr Square relative_square(Color c) const {
    return c == kBlack ? *this : inverse_square();
  }

  /**
   * このマスのSFEN表記を返します.
   */
  std::string ToSfen() const;

  /**
   * 指定されたSFEN表記に対応するマスを返します.
   */
  static Square FromSfen(const std::string& sfen);

  /**
   * 左右反転したマスを返します.
   * 例えば、２四のマスにこのメソッドを適用すると、８四のマスが返されます。
   */
  static constexpr Square mirror_horizontal(Square s) {
    return Square(kFile9 - s.file(), s.rank());
  }

  /**
   * 上下反転したマスを返します.
   * 例えば、２四のマスにこのメソッドを適用すると、２六のマスが返されます。
   */
  static constexpr Square flip_vertical(Square s) {
    return Square(s.file(), kRankI - s.rank());
  }

  /**
   * 将棋盤を180度回転させたときのマスを返します.
   * 例えば、２四のマスにこのメソッドを適用すると、８六のマスが返されます。
   */
  static constexpr Square rotate180(Square s) {
    return Square(80 - static_cast<int>(s));
  }

  // 最小値、最大値
  static constexpr Square min() { return Square( 0); }
  static constexpr Square max() { return Square(80); }

  /**
   * ２つのマスの間の距離（チェビシェフ距離）を返します.
   */
  static int distance(Square i, Square j) {
    return distance_[i][j];
  }

  /**
   * ２マスの相対的な位置関係を返します.
   */
  static int relation(Square from, Square to) {
    return relation_[from][to];
  }

  /**
   * 指定された方向に進むために必要な、差分を返します.
   */
  static Square direction_to_delta(Direction d) {
    return direction_to_delta_[d];
  }

  /**
   * すべてのマスを要素とする、仮想コンテナを返します.
   * 範囲for文で使うと便利です.
   */
  static Sequence<Square> all_squares();

  /**
   * 内部状態が正しい場合には、trueを返します.
   */
  constexpr bool IsOk() const {
    return *this >= min() && *this <= max();
  }

  /**
   * テーブルを初期化します.
   */
  static void Init();

 private:
  static ArrayMap<int, Square, Square> distance_;
  static ArrayMap<int, Square, Square> relation_;
  static ArrayMap<Square, Direction> direction_to_delta_;
  int square_;
};

ENABLE_ARITHMETIC_OPERATORS(Square)

inline std::string Square::ToSfen() const {
  assert(IsOk());
  return std::string{file_to_char(file()), rank_to_char(rank())};
}

inline Square Square::FromSfen(const std::string& sfen) {
  assert(sfen.length() == 2);
  return Square(file_from_char(sfen[0]), rank_from_char(sfen[1]));
}

inline Sequence<Square> Square::all_squares() {
  return Sequence<Square>(min(), max());
}

constexpr Square kDeltaN(-1);
constexpr Square kDeltaS(+1);
constexpr Square kDeltaE(-9);
constexpr Square kDeltaW(+9);
constexpr Square kDeltaNE(kDeltaN + kDeltaE);
constexpr Square kDeltaNW(kDeltaN + kDeltaW);
constexpr Square kDeltaSE(kDeltaS + kDeltaE);
constexpr Square kDeltaSW(kDeltaS + kDeltaW);
constexpr Square kDeltaNNE(kDeltaN + kDeltaNE);
constexpr Square kDeltaNNW(kDeltaN + kDeltaNW);
constexpr Square kDeltaSSE(kDeltaS + kDeltaSE);
constexpr Square kDeltaSSW(kDeltaS + kDeltaSW);

constexpr Square kSquareNone(127);

#define DEFINE_SQUARE_CONSTANTS_ON_FILE(f) \
  constexpr Square kSquare ## f ## A(kFile ## f, kRankA); \
  constexpr Square kSquare ## f ## B(kFile ## f, kRankB); \
  constexpr Square kSquare ## f ## C(kFile ## f, kRankC); \
  constexpr Square kSquare ## f ## D(kFile ## f, kRankD); \
  constexpr Square kSquare ## f ## E(kFile ## f, kRankE); \
  constexpr Square kSquare ## f ## F(kFile ## f, kRankF); \
  constexpr Square kSquare ## f ## G(kFile ## f, kRankG); \
  constexpr Square kSquare ## f ## H(kFile ## f, kRankH); \
  constexpr Square kSquare ## f ## I(kFile ## f, kRankI);

DEFINE_SQUARE_CONSTANTS_ON_FILE(1)
DEFINE_SQUARE_CONSTANTS_ON_FILE(2)
DEFINE_SQUARE_CONSTANTS_ON_FILE(3)
DEFINE_SQUARE_CONSTANTS_ON_FILE(4)
DEFINE_SQUARE_CONSTANTS_ON_FILE(5)
DEFINE_SQUARE_CONSTANTS_ON_FILE(6)
DEFINE_SQUARE_CONSTANTS_ON_FILE(7)
DEFINE_SQUARE_CONSTANTS_ON_FILE(8)
DEFINE_SQUARE_CONSTANTS_ON_FILE(9)

#undef DEFINE_SQUARE_CONSTANTS_ON_FILE

#endif /* SQUARE_H_ */
