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

#ifndef PSQ_H_
#define PSQ_H_

#include <cstdlib>
#include <tmmintrin.h> // SSSE 3
#include "common/array.h"
#include "common/arraymap.h"
#include "common/sequence.h"
#include "hand.h"
#include "move.h"
class Position;

/**
 * 「駒の種類」と「駒の位置」により一意に決定されるインデックスです.
 *
 * このインデックスは、評価関数において、駒の位置評価を計算する際に使われます。
 * 最小値は0、最大値は2109で、全部で2110通りに場合分けされています。
 *
 * (1)「駒の種類」について
 *   - 駒の種類は、歩〜飛およびその成駒（計13種類）を敵・味方で区別したもので、合計26種類あります。
 *   - Bonanzaでは、「と〜成銀」を「金」と同様に扱うのに対し、この実装においては、「と〜成銀」は「金」と区別しています。
 *     これは、「と〜成銀」による攻撃の方が威力が高いことが多いため、金とは区別すべきだろうという仮説に基づいています。
 *
 * (2)「駒の位置」について
 *   - 「駒の位置」は、(a)盤上の駒は81通りに区別され、(b)持ち駒は何枚目の持ち駒かにより区別されます。
 *   - 盤上の歩・香・桂については、Bonanzaと同様に、「行きどころのない駒」となるマスは除外されています。
 *     これにより、少しだけメモリ使用量を節約することができます。
 */
class PsqIndex {
 public:
  explicit constexpr PsqIndex(int i = 0)
      : index_(i) {
  }

  constexpr operator int() const {
    return index_;
  }

  /**
   * インデックスに対応する駒を返します（主にデバッグ用）.
   */
  Piece piece() const {
    return index_to_piece_[index_];
  }

  /**
   * インデックスに対応する、将棋盤のマスを返します（主にデバッグ用）.
   * @return 将棋盤のマス（ただし、持ち駒の場合は、kSquareNoneを返します)
   */
  Square square() const {
    return index_to_square_[index_];
  }

  /**
   * 持ち駒のインデックスを返します.
   * @param color 持ち駒の持ち主
   * @param pt    持ち駒の種類
   * @param num   何枚目の持ち駒か
   * @return 持ち駒のインデックス（0から75までの値をとります）
   */
  static PsqIndex OfHand(Color color, PieceType pt, int num) {
    PsqIndex idx = PsqIndex(hand_[color][pt] + num);
    assert(0 <= idx && idx <= 75);
    return idx;
  }

  /**
   * 盤上の駒のインデックスを返します.
   * @param piece  駒の種類
   * @param square どのマスにいる駒か
   * @return 盤上の駒のインデックス（76から2109までの値をとります）
   */
  static PsqIndex OfBoard(Piece piece, Square square) {
    PsqIndex idx = psq_[square][piece];
    assert(76 <= idx && idx <= max());
    return idx;
  }

  static void Init();

  static constexpr PsqIndex min() { return PsqIndex(   0); }
  static constexpr PsqIndex max() { return PsqIndex(2109); }
  static Sequence<PsqIndex> all_indices() {
    return Sequence<PsqIndex>(min(), max());
  }

 private:
  int index_;
  static ArrayMap<PsqIndex, Color, PieceType> hand_;
  static ArrayMap<PsqIndex, Square, Piece> psq_;
  static Array<Piece, 2110> index_to_piece_; // For Debug
  static Array<Square, 2110> index_to_square_; // For Debug
};

/*
 * PsqIndexクラスについて、+演算子と-演算子をオーバーロード。
 * このオーバーロードによって、例えば、
 *   PsqIndex a = PsqIndex(1), b = PsqIndex(2);
 *   PsqIndex c = a + b; // c == PsqIndex(3)となる。
 * のように書くことができる。
 */
ENABLE_ADD_AND_SUBTRACT_OPERATORS(PsqIndex)

/**
 * 2つのPsqIndexを1つにまとめて、ペアにしたものです.
 *
 * 格納されるPsqIndexは、
 *   - (1) 先手視点からの駒の位置
 *   - (2) 後手視点からの駒の位置（(1)について、将棋盤を180度回転させたもの）
 * の2つです。
 *
 * 2つのインデックスを1つのクラスにまとめて格納する理由は、高速化のためです。
 * このようにまとめて格納しておくことにより、1回のテーブル参照によって2つのインデックスをいっぺんに
 * 取得することができます。
 * そのため、2つのインデックスを2回に分けて計算するよりも高速化が期待できます。
 *
 * 先手視点と後手視点に分けられているのは、KPの計算の際、後手の評価値はいったん先手視点に変換されてから
 * 計算が行われているためです。
 */
class PsqPair {
 public:
  /**
   * デフォルトコンストラクタ.
   * 注意：高速化のため、ゼロ初期化を省略しているので、初期値は未定義です。
   */
  PsqPair() {}

  /**
   * 先手視点または後手視点のインデックスを返します.
   * @param c 先手視点のインデックスか、後手視点のインデックスか
   * @return 先手視点または後手視点のインデックス
   */
  PsqIndex get(Color c) const {
    return pair_[c];
  }

  /**
   * 先手視点のインデックスを返します.
   * @return 先手視点のインデックス
   */
  PsqIndex black() const {
    return pair_[kBlack];
  }

  /**
   * 後手視点のインデックスを返します.
   * @return 後手視点のインデックス
   */
  PsqIndex white() const {
    return pair_[kWhite];
  }

  /**
   * インデックスに対応する駒を返します（主にデバッグ用）.
   */
  Piece piece() const {
    return pair_[kBlack].piece();
  }

  /**
   * インデックスに対応する、将棋盤のマスを返します（主にデバッグ用）.
   * @return 将棋盤のマス（ただし、持ち駒の場合は、kSquareNoneを返します)
   */
  Square square() const {
    return pair_[kBlack].square();
  }

  /**
   * 持ち駒のインデックスが格納されたPsqPairを返します.
   */
  static PsqPair OfHand(Color c, PieceType pt, int num) {
    assert(1 <= num && num <= GetMaxNumber(pt));
    return hand_[c][pt][num];
  }

  /**
   * 盤上の駒のインデックスが格納されたPsqPairを返します.
   */
  static PsqPair OfBoard(Piece p, Square s) {
    return psq_[s][p];
  }

  static const ArrayMap<PsqPair, PsqIndex>& all_pairs() {
    return all_pairs_;
  }

  /**
   * OfHand()関数およびOfBoard()関数を動作させるのに必要な内部テーブルの初期化を行います.
   */
  static void Init();

 private:

  PsqPair(PsqIndex index_black, PsqIndex index_white) {
    pair_[kBlack] = index_black;
    pair_[kWhite] = index_white;
  }

  ArrayMap<PsqIndex, Color> pair_;

  // 歩は最大18枚持ち駒にできるので、配列は19要素確保する。
  static ArrayMap<Array<PsqPair, 19>, Color, PieceType> hand_;
  static ArrayMap<PsqPair, Square, Piece> psq_;
  static ArrayMap<PsqPair, PsqIndex> all_pairs_;
};

/**
 * PsqPairを格納するためのコンテナです.
 *
 * MakeMove(), UnmakeMove()メソッドは、直前の局面からインデックスの差分を計算するので、
 * 局面ごとにすべてのインデックスを再計算する場合よりも高速に動作します。
 *
 * 要素数は最大38です。将棋の駒は全部で40枚ですが、そこから玉2枚を除いているためです。
 *
 * （差分計算についての参考文献）
 *   - 金澤裕治: NineDayFeverアピール文書,
 *     http://www.computer-shogi.org/wcsc23/appeal/NineDayFever/NineDayFever.txt, 2013.
 */
class PsqList {
 public:
  /**
   * 空のインデックスリストを作成します.
   */
  PsqList() : size_(0) {}

  /**
   * 特定の局面における、インデックスリストを作成します.
   */
  explicit PsqList(const Position& pos);

  const PsqPair* begin() const {
    return list_.begin();
  }

  const PsqPair* end() const {
    return list_.begin() + size_;
  }

  size_t size() const {
    assert(size_ <= kMaxSize);
    return size_;
  }

  PsqPair operator[](size_t i) const {
    assert(i < size_ && size_ <= kMaxSize);
    return list_[i];
  }

  /**
   * 特定の指し手に沿って局面を進めた場合に、インデックスリストを差分計算します.
   */
  void MakeMove(Move move);

  /**
   * 特定の指し手を取り消して局面を戻す場合に、インデックスリストを差分計算します.
   */
  void UnmakeMove(Move move);

  /**
   * 内部状態に矛盾がないかを確認します（デバッグ用）.
   */
  bool IsOk() const;

  /**
   * ２つのリストが、同じ要素を含んでいる場合にtrueを返します（デバッグ用）.
   * リストに含まれている要素の順番が異なっていても、同じ要素さえ含んでいればtrueを返します。
   * 内部の処理が重たいので、処理速度が要求される場所では使わないでください。
   */
  static bool TwoListsHaveSameItems(const PsqList& list1, const PsqList& list2);

 private:
  static constexpr int kMaxSize = 38;
  size_t size_;
  ArrayMap<Hand, Color> hand_;
  Array<PsqPair, kMaxSize> list_;
  ArrayMap<int, Square> index_;
  ArrayMap<Array<int, 19>, Color, PieceType> hand_index_;
};

/**
 * 「マスの位置」、「そのマスにある駒」、「先手の利き数」、「後手の利き数」からなるインデックスです.
 *
 * PsqControlIndexは、内部的にはuint16_tで実装されており、そのビットの使用領域は以下のとおりです。
 *
 * <pre>
 * 0000 0000 0001 1111 そのマスにある駒
 * 0000 0000 0110 0000 そのマスにおける先手の利き数（0から3まで）
 * 0000 0001 1000 0000 そのマスにおける後手の利き数（0から3まで）
 * 1111 1110 0000 0000 そのマスの位置
 * </pre>
 */
class PsqControlIndex {
 public:
  typedef BitField<uint16_t>::Key Key;
  static constexpr Key kKeyPiece         = Key( 0,  5);
  static constexpr Key kKeyBlackControls = Key( 5,  7);
  static constexpr Key kKeyWhiteControls = Key( 7,  9);
  static constexpr Key kKeySquare        = Key( 9, 16);
  static constexpr Key kKeyAll           = Key( 0, 16);

  explicit PsqControlIndex(uint16_t bitfield) : bitfield_(bitfield) {}

  PsqControlIndex(Piece p, Square s, int black_controls, int white_controls) {
    assert(0 <= black_controls && black_controls <= 3);
    assert(0 <= white_controls && white_controls <= 3);
    bitfield_.set(kKeyPiece, p);
    bitfield_.set(kKeySquare, s);
    bitfield_.set(kKeyBlackControls, black_controls);
    bitfield_.set(kKeyWhiteControls, white_controls);
  }

  operator uint16_t() const {
    return bitfield_;
  }

  Piece piece() const {
    return Piece(bitfield_[kKeyPiece]);
  }

  Square square() const {
    return Square(bitfield_[kKeySquare]);
  }

  int num_controls(Color color) const {
    return bitfield_[color == kBlack ? kKeyBlackControls : kKeyWhiteControls];
  }

  bool IsOk() const {
    return piece().IsOk() && piece() != kWall && square().IsOk();
  }

  static constexpr uint16_t min() {
    return 0;
  }

  static constexpr uint16_t max() {
    return static_cast<uint16_t>(Square::max()) * kKeySquare.first_bit
         | kKeyWhiteControls.mask | kKeyBlackControls.mask | kKeyPiece.mask;
  }

 private:
  BitField<uint16_t> bitfield_;
};

/**
 * PsqControlIndexを81マス分格納するためのコンテナです.
 */
class PsqControlList {
 public:
  /**
   * 128ビット長のビットセットです.
   * 将棋盤の特定のマスのみをループするのに用います。
   */
  struct BitSet128 {
    BitSet128() : qword{0, 0} {}
    template<typename Consumer>
    void ForEach(Consumer function) const {
      for (uint64_t q0 = qword[0]; q0; q0 = bitop::reset_first_bit(q0)) {
        function(Square(bitop::bsf64(q0)));
      }
      for (uint64_t q1 = qword[1]; q1; q1 = bitop::reset_first_bit(q1)) {
        function(Square(bitop::bsf64(q1) + 64));
      }
    }
    int count() const {
      return bitop::popcnt64(qword[0]) + bitop::popcnt64(qword[1]);
    }
    union {
      uint8_t byte[16];
      uint64_t qword[2];
    };
  };

  PsqControlIndex operator[](Square s) const {
    assert(s.IsOk());
    return PsqControlIndex(word_[s]);
  }

  size_t size() const {
    return Square::max() + 1;
  }

  __m128i& xmm(size_t i) {
    assert(i < 11);
    return xmm_[i];
  }

  const __m128i& xmm(size_t i) const {
    assert(i < 11);
    return xmm_[i];
  }

  /**
   * 2つのPsqControlListを比較して、違いが生じているマスのビットを立てたビットセットを返します.
   * 評価関数の差分計算に利用されます。
   */
  static BitSet128 ComputeDifference(const PsqControlList& lhs, const PsqControlList& rhs);

 private:
  union {
    __m128i xmm_[11];
    uint16_t word_[88];
  };
};

#endif /* PSQ_H_ */
