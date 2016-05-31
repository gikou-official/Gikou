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

#ifndef BITBOARD_H_
#define BITBOARD_H_

#include <string>
#include <smmintrin.h> // SSE 4.1
#include "common/array.h"
#include "common/arraymap.h"
#include "piece.h"
#include "square.h"

/**
 * ビットボードを実装したクラスです.
 *
 * Bitboardクラスは、内部的にはMagic Bitboardを用いて実装されています。
 *
 * なお、Bitboardクラス内部のビットの位置と、実際の将棋盤との対応は、以下のとおりです。
 * <pre>
 * （下位64ビット）
 *    9  8  7  6  5  4  3  2  1
 * +----------------------------+
 * |  -  - 54 45 36 27 18  9  0 | 一
 * |  -  - 55 46 37 28 19 10  1 | 二
 * |  -  - 56 47 38 29 20 11  2 | 三
 * |  -  - 57 48 39 30 21 12  3 | 四
 * |  -  - 58 49 40 31 22 13  4 | 五
 * |  -  - 59 50 41 32 23 14  5 | 六
 * |  -  - 60 51 42 33 24 15  6 | 七
 * |  -  - 61 52 43 34 25 16  7 | 八
 * |  -  - 62 53 44 35 26 17  8 | 九
 * +----------------------------+
 *
 * （上位64ビット）
 *    9  8  7  6  5  4  3  2  1
 * +----------------------------+
 * |  9  0 　-  -  -  -  -  -  -| 一
 * | 10  1 　-  -  -  -  -  -  -| 二
 * | 11  2 　-  -  -  -  -  -  -| 三
 * | 12  3 　-  -  -  -  -  -  -| 四
 * | 13  4 　-  -  -  -  -  -  -| 五
 * | 14  5 　-  -  -  -  -  -  -| 六
 * | 15  6 　-  -  -  -  -  -  -| 七
 * | 16  7 　-  -  -  -  -  -  -| 八
 * | 17  8 　-  -  -  -  -  -  -| 九
 * +----------------------------+
 * </pre>
 *
 * （参考文献）
 *   - 山本一成, et al.: コンピュータ将棋における Magic Bitboard の提案と実装,
 *     第15回ゲームプログラミングワークショップ, pp.42-48, 2010.
 *   - Chess Programming Wiki, http://chessprogramming.wikispaces.com/Bitboards.
 */
class Bitboard {
 public:

  // コンストラクタ
  explicit Bitboard(__m128i xmm = _mm_setzero_si128())
      : xmm_(xmm) {
  }
  Bitboard(uint64_t q1, uint64_t q0)
      : xmm_(_mm_set_epi64x(q1, q0)) {
  }

  // 比較演算子
  bool operator==(Bitboard rhs) const;
  bool operator!=(Bitboard rhs) const { return !operator==(rhs); }

  /**
   * "(lhs & rhs).any()"と書くのと同等の結果を得つつ、高速化を図ったメソッドです.
   */
  bool test(Bitboard rhs) const;

  // 要素へのアクセス
  bool operator[](Square) const;
  bool test(Square) const;
  bool any() const;
  bool none() const;
  int count() const;
  Square first_one() const;
  Square pop_first_one();
  template<size_t kN> uint64_t extract64() const;
  template<size_t kN> Bitboard& insert64(uint64_t);
  template<typename Consumer> void ForEach(Consumer) const;
  template<typename Consumer> void Serialize(Consumer) const;
  DirectionSet neighborhood8(Square) const;

  // 各種演算子
  Bitboard& operator&=(Bitboard rhs);
  Bitboard& operator|=(Bitboard rhs);
  Bitboard& operator^=(Bitboard rhs);
  Bitboard& operator<<=(size_t n);
  Bitboard& operator>>=(size_t n);
  Bitboard operator&(Bitboard rhs) const { return Bitboard(*this) &= rhs; }
  Bitboard operator|(Bitboard rhs) const { return Bitboard(*this) |= rhs; }
  Bitboard operator^(Bitboard rhs) const { return Bitboard(*this) ^= rhs; }
  Bitboard operator<<(size_t n) const    { return Bitboard(*this) <<= n;  }
  Bitboard operator>>(size_t n) const    { return Bitboard(*this) >>= n;  }
  Bitboard operator~() const;
  Bitboard andnot(Bitboard rhs) const; // SSEのPANDN命令を使っています

  // set/reset
  Bitboard& set()           { xmm_ = _mm_cmpeq_epi8(xmm_, xmm_);  return *this; }
  Bitboard& set(Square s)   { *this |= square_bb(s);              return *this; }
  Bitboard& reset()         { xmm_ = _mm_setzero_si128();         return *this; }
  Bitboard& reset(Square s) { *this = this->andnot(square_bb(s)); return *this; }

  // デバッグ用
  bool HasExcessBits() const;
  std::string ToString() const;
  void Print() const;

  // 各種テーブルの初期化用
  static void Init();

  /**
   * ビットが立っている段をすべて1で埋めるための関数です.
   */
  static Bitboard FileFill(Bitboard bb);

  // 各種マスクの参照用メソッド
  static Bitboard board_bb() { return Bitboard(0x3ffff, 0x7fffffffffffffff); }
  static Bitboard square_bb(Square s) { return square_bb_[s]; }
  static Bitboard file_bb(File f) { return file_bb_[f]; }
  static Bitboard rank_bb(Rank r) { return rank_bb_[r]; }
  static Bitboard promotion_zone_bb(Color c) { return promotion_zone_bb_[c]; }
  static Bitboard line_bb(Square i, Square j) { return line_bb_[i][j]; }
  static Bitboard between_bb(Square i, Square j) { return between_bb_[i][j]; }
  static Bitboard direction_bb(Square s, DirectionSet d) { return direction_bb_[s][d]; }
  static Bitboard neighborhood24_bb(Square s) { return neighborhood24_bb_[s]; }
  static Bitboard checker_candidates_bb(Piece p, Square s) {
    return checker_candidates_bb_[s][p];
  }
  static Bitboard adjacent_check_candidates_bb(Piece p, Square s) {
    return adjacent_check_candidates_bb_[s][p];
  }
  static Bitboard rook_mask_bb(Square s) {
    return magic_numbers_[s].rook_mask;
  }

  // 予め計算しておいた利きのテーブルを参照するための関数
  static Bitboard step_attacks_bb(Piece p, Square s) { return step_attacks_bb_[s][p]; }
  static Bitboard min_attacks_bb(Piece p, Square s) { return min_attacks_bb_[s][p]; }
  static Bitboard max_attacks_bb(Piece p, Square s) { return max_attacks_bb_[s][p]; }
  static Bitboard pawns_attacks_bb(Bitboard pawns, Color c);
  static Bitboard lance_attacks_bb(Square s, Bitboard occ, Color c);
  static Bitboard bishop_attacks_bb(Square s, Bitboard occ);
  static Bitboard rook_attacks_bb(Square s, Bitboard occ);
  static Bitboard queen_attacks_bb(Square s, Bitboard occ);

 private:

  template<typename T>
  struct MagicNumber {
    ArrayMap<T, Color> lance_postmask;
    T lance_premask;
    T bishop_mask;
    T rook_mask;
    uint64_t bishop_magic;
    uint64_t rook_magic;
    T* bishop_ptr;
    T* rook_ptr;
    int lance_shift;
    int bishop_shift;
    int rook_shift;
  };

  uint64_t uint64() const;

  // マスク
  static ArrayMap<Bitboard, Square> square_bb_;
  static ArrayMap<Bitboard, File> file_bb_;
  static ArrayMap<Bitboard, Rank> rank_bb_;
  static ArrayMap<Bitboard, Color> promotion_zone_bb_;
  static ArrayMap<Bitboard, Square, Square> line_bb_;
  static ArrayMap<Bitboard, Square, Square> between_bb_;
  static ArrayMap<Bitboard, Square, DirectionSet> direction_bb_;
  static ArrayMap<Bitboard, Square> neighborhood24_bb_;
  static ArrayMap<Bitboard, Square, Piece> checker_candidates_bb_;
  static ArrayMap<Bitboard, Square, Piece> adjacent_check_candidates_bb_;

  // 利きのテーブル
  static ArrayMap<Bitboard, Square, Piece> step_attacks_bb_;
  static ArrayMap<Bitboard, Square, Piece> max_attacks_bb_;
  static ArrayMap<Bitboard, Square, Piece> min_attacks_bb_;

  // マジックナンバーのテーブル
  static ArrayMap<MagicNumber<Bitboard>, Square> magic_numbers_;
  static ArrayMap<Array<Bitboard, 128>, Square> lance_attacks_bb_;
  static Array<Bitboard, 20224> bishop_attacks_bb_;
  static Array<Bitboard, 512000> rook_attacks_bb_;
  static ArrayMap<uint64_t, Square> eight_neighborhoods_magics_;

  // メンバ変数はXMMレジスタ１個分
  __m128i xmm_;
};

inline bool Bitboard::operator==(Bitboard rhs) const {
  __m128i cmp = _mm_cmpeq_epi8(xmm_, rhs.xmm_);
  return _mm_testc_si128(cmp, _mm_cmpeq_epi8(xmm_, xmm_));
}

inline bool Bitboard::test(Bitboard rhs) const {
  return !_mm_testz_si128(xmm_, rhs.xmm_);
}

inline bool Bitboard::operator[](Square s) const {
  return test(square_bb(s));
}

inline bool Bitboard::test(Square s) const {
  return operator[](s);
}

inline bool Bitboard::any() const {
  return !_mm_testz_si128(xmm_, xmm_);
}

inline bool Bitboard::none() const {
  return !any();
}

inline int Bitboard::count() const {
  assert(!HasExcessBits());
  return bitop::popcnt64(extract64<0>()) + bitop::popcnt64(extract64<1>());
}

inline Square Bitboard::first_one() const {
  assert(any());
  assert(!HasExcessBits());
  uint64_t q0 = extract64<0>();
  if (q0)
    return Square(bitop::bsf64(q0));
  else
    return Square(bitop::bsf64(extract64<1>()) + 63);
}

inline Square Bitboard::pop_first_one() {
  Square s = first_one();
  reset(s);
  return s;
}

template<size_t kN>
inline uint64_t Bitboard::extract64() const {
  static_assert(kN == 0 || kN == 1, "");
  return _mm_extract_epi64(xmm_, kN);
}

template<size_t kN>
inline Bitboard& Bitboard::insert64(uint64_t q) {
  static_assert(kN == 0 || kN == 1, "");
  xmm_ = _mm_insert_epi64(xmm_, q, kN);
  return *this;
}

template<typename Consumer>
inline void Bitboard::ForEach(Consumer function) const {
  Bitboard clone = *this;
  while (clone.any()) {
    Square s = clone.pop_first_one();
    function(s);
  }
}

template<typename Consumer>
inline void Bitboard::Serialize(Consumer function) const {
  for (uint64_t q0 = extract64<0>(); q0; q0 = bitop::reset_first_bit(q0)) {
    function(Square(bitop::bsf64(q0)));
  }
  for (uint64_t q1 = extract64<1>(); q1; q1 = bitop::reset_first_bit(q1)) {
    function(Square(bitop::bsf64(q1) + 63));
  }
}

inline DirectionSet Bitboard::neighborhood8(Square s) const {
  Bitboard neighborhoods_bb = *this & step_attacks_bb(kBlackKing, s);
  Bitboard duplicated = neighborhoods_bb | (neighborhoods_bb >> 36);
  uint64_t bits = (duplicated.extract64<0>() << 1) * eight_neighborhoods_magics_[s];
  uint64_t east_neighborhoods = (bits >> 55) & UINT64_C(0x0f);
  uint64_t west_neighborhoods = (bits >> 56) & UINT64_C(0xf0);
  return DirectionSet(east_neighborhoods | west_neighborhoods);
}

inline Bitboard& Bitboard::operator&=(Bitboard rhs) {
  xmm_ = _mm_and_si128(xmm_, rhs.xmm_);
  return *this;
}

inline Bitboard& Bitboard::operator|=(Bitboard rhs) {
  xmm_ = _mm_or_si128(xmm_, rhs.xmm_);
  return *this;
}

inline Bitboard& Bitboard::operator^=(Bitboard rhs) {
  xmm_ = _mm_xor_si128(xmm_, rhs.xmm_);
  return *this;
}

inline Bitboard& Bitboard::operator<<=(size_t n) {
  assert(n <= 81);
  __m128i shift64 = _mm_slli_si128(xmm_, 8);
  if (n >= 63) {
    xmm_ = _mm_slli_epi64(shift64, (n - 63));
  } else {
    __m128i base  = _mm_slli_epi64(xmm_, n);
    __m128i carry = _mm_srli_epi64(shift64, 63 - n);
    xmm_ = _mm_or_si128(base, carry);
  }
  *this &= board_bb(); // 将棋盤の外の余計なビットを取り除く
  return *this;
}

inline Bitboard& Bitboard::operator>>=(size_t n) {
  assert(n <= 81);
  __m128i shift64 = _mm_srli_si128(xmm_, 8);
  if (n >= 63) {
    xmm_ = _mm_srli_epi64(shift64, (n - 63));
  } else {
    __m128i base  = _mm_srli_epi64(xmm_, n);
    __m128i carry = _mm_slli_epi64(shift64, 63 - n);
    xmm_ = _mm_or_si128(base, carry);
  }
  *this &= board_bb(); // 将棋盤の外の余計なビットを取り除く
  return *this;
}

inline Bitboard Bitboard::operator~() const {
  return Bitboard(_mm_xor_si128(xmm_, _mm_cmpeq_epi8(xmm_, xmm_)));
}

inline Bitboard Bitboard::andnot(Bitboard rhs) const {
  return Bitboard(_mm_andnot_si128(rhs.xmm_, xmm_));
}

inline bool Bitboard::HasExcessBits() const {
  return test(Bitboard(0xfffffffffffc0000, 0x8000000000000000));
}

inline Bitboard Bitboard::pawns_attacks_bb(Bitboard pawns, Color c) {
  // 歩が最上段に存在することはありえない（反則になってしまうため）
  assert((pawns & rank_bb(relative_rank(c, kRank1))).none());
  // 最大９個の歩の利きを同時に計算するため、論理シフト演算を用いる
  return c == kBlack
       ? Bitboard(_mm_srli_epi64(pawns.xmm_, 1))
       : Bitboard(_mm_slli_epi64(pawns.xmm_, 1));
}

inline Bitboard Bitboard::lance_attacks_bb(Square s, Bitboard occ, Color c) {
  occ &= magic_numbers_[s].lance_premask;
  uint64_t index = occ.uint64() >> magic_numbers_[s].lance_shift;
  return lance_attacks_bb_[s][index] & magic_numbers_[s].lance_postmask[c];
}

inline Bitboard Bitboard::bishop_attacks_bb(Square s, Bitboard occ) {
  uint64_t index = (occ & magic_numbers_[s].bishop_mask).uint64();
  index  *= magic_numbers_[s].bishop_magic;
  index >>= magic_numbers_[s].bishop_shift;
  return magic_numbers_[s].bishop_ptr[index];
}

inline Bitboard Bitboard::rook_attacks_bb(Square s, Bitboard occ) {
  uint64_t index = (occ & magic_numbers_[s].rook_mask).uint64();
  index  *= magic_numbers_[s].rook_magic;
  index >>= magic_numbers_[s].rook_shift;
  return magic_numbers_[s].rook_ptr[index];
}

inline Bitboard Bitboard::queen_attacks_bb(Square s, Bitboard occ) {
  return bishop_attacks_bb(s, occ) | rook_attacks_bb(s, occ);
}

inline uint64_t Bitboard::uint64() const {
  return extract64<0>() | extract64<1>();
}

// 以下は、ビットボードのテーブルの短い別名（alias）
inline Bitboard square_bb(Square s) {
  return Bitboard::square_bb(s);
}
inline Bitboard file_bb(File f) {
  return Bitboard::file_bb(f);
}
inline Bitboard file_bb(Square s) {
  return Bitboard::file_bb(s.file());
}
inline Bitboard rank_bb(Rank r) {
  return Bitboard::rank_bb(r);
}
inline Bitboard rank_bb(Square s) {
  return Bitboard::rank_bb(s.rank());
}
inline Bitboard promotion_zone_bb(Color c) {
  return Bitboard::promotion_zone_bb(c);
}
inline Bitboard line_bb(Square i, Square j) {
  return Bitboard::line_bb(i, j);
}
inline Bitboard between_bb(Square i, Square j) {
  return Bitboard::between_bb(i, j);
}
inline Bitboard direction_bb(Square s, DirectionSet d) {
  return Bitboard::direction_bb(s, d);
}
inline Bitboard neighborhood8_bb(Square center_square) {
  return Bitboard::step_attacks_bb(kBlackKing, center_square);
}
inline Bitboard neighborhood24_bb(Square center_square) {
  return Bitboard::neighborhood24_bb(center_square);
}
inline Bitboard step_attacks_bb(Piece p, Square s) {
  return Bitboard::step_attacks_bb(p, s);
}
inline Bitboard min_attacks_bb(Piece p, Square s) {
  return Bitboard::min_attacks_bb(p, s);
}
inline Bitboard max_attacks_bb(Piece p, Square s) {
  return Bitboard::max_attacks_bb(p, s);
}
inline Bitboard pawns_attacks_bb(Bitboard pawns, Color c) {
  return Bitboard::pawns_attacks_bb(pawns, c);
}
inline Bitboard lance_attacks_bb(Square s, Bitboard occ, Color c) {
  return Bitboard::lance_attacks_bb(s, occ, c);
}
inline Bitboard bishop_attacks_bb(Square s, Bitboard occ) {
  return Bitboard::bishop_attacks_bb(s, occ);
}
inline Bitboard rook_attacks_bb(Square s, Bitboard occ) {
  return Bitboard::rook_attacks_bb(s, occ);
}
inline Bitboard queen_attacks_bb(Square s, Bitboard occ) {
  return Bitboard::queen_attacks_bb(s, occ);
}
inline Bitboard checker_candidates_bb(Piece p, Square s) {
  return Bitboard::checker_candidates_bb(p, s);
}
inline Bitboard adjacent_check_candidates_bb(Piece p, Square s) {
  return Bitboard::adjacent_check_candidates_bb(p, s);
}
inline Bitboard rook_mask_bb(Square s) {
  return Bitboard::rook_mask_bb(s);
}

/**
 * kFrom段からkTo段までのビットが１になっているマスクを作成して返します.
 */
template<Color kC, int kFrom, int kTo>
inline Bitboard rank_bb() {
  static_assert(kFrom <= kTo, "");
  static_assert(1 <= kFrom && kFrom <= 9, "");
  static_assert(1 <= kTo   && kTo   <= 9, "");
  constexpr int from  = kC == kBlack ? kFrom : 10 - kTo;
  constexpr int to    = kC == kBlack ? kTo : 10 - kFrom;
  constexpr int shift = from - 1;
  constexpr uint64_t mul  = (UINT64_C(1) << (to - from + 1)) - UINT64_C(1);
  constexpr uint64_t high = (UINT64_C(0x201) << shift) * mul;
  constexpr uint64_t low  = (UINT64_C(0x40201008040201) << shift) * mul;
  return Bitboard(high, low);
}

template<int kFrom, int kTo>
inline Bitboard rank_bb() {
  return rank_bb<kBlack, kFrom, kTo>();
}

template<int kFrom, int kTo>
inline Bitboard rank_bb(Color c) {
  return c == kBlack ? rank_bb<kFrom, kTo>() : rank_bb<kWhite, kFrom, kTo>();
}

template<Color kC, PieceType kPt>
inline Bitboard attacks_from(Square from, Bitboard occ) {
  switch (kPt) {
    case kLance:
      return lance_attacks_bb(from, occ, kC);
    case kBishop:
      return bishop_attacks_bb(from, occ);
    case kRook:
      return rook_attacks_bb(from, occ);
    case kHorse:
      return bishop_attacks_bb(from, occ) | step_attacks_bb(kBlackKing, from);
    case kDragon:
      return rook_attacks_bb(from, occ) | step_attacks_bb(kBlackKing, from);
    default:
      return step_attacks_bb(Piece(kC, kPt), from);
  }
}

template<Color kC, PieceType kPt>
inline Bitboard attackers_to(Square to, Bitboard occ) {
  return attacks_from<~kC, kPt>(to, occ);
}

Bitboard AttacksFrom(Piece piece, Square from, Bitboard occ);

inline Bitboard AttackersTo(Piece piece, Square from, Bitboard occ) {
  return AttacksFrom(piece.opponent_piece(), from, occ);
}

#endif /* BITBOARD_H_ */
