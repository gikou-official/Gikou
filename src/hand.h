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

#ifndef HAND_H_
#define HAND_H_

#include <string>
#include "common/arraymap.h"
#include "common/bitfield.h"
#include "common/bitset.h"
#include "piece.h"

/**
 * 各持ち駒の有無を表すためのビットセットです.
 */
typedef BitSet<PieceType, 8> HandSet;

/**
 * 持ち駒の枚数を管理するためのクラスです.
 *
 * 各駒の保存に使われるビットの範囲は以下のとおりです。
 * <pre>
 * 歩: ........ ........ ........ ...11111
 * 香: ........ ........ .......1 11......
 * 桂: ........ ........ ...111.. ........
 * 銀: ........ .......1 11...... ........
 * 金: ........ ...111.. ........ ........
 * 角: .......1 11...... ........ ........
 * 飛: ...111.. ........ ........ ........
 * </pre>
 */
class Hand {
 public:
  typedef BitField<uint32_t>::Key Key;

  /**
   * デフォルトコンストラクタでは、持ち駒の枚数が0枚で初期化されます.
   */
  constexpr Hand() : hand_(0) {}

  /**
   * 両辺の持ち駒の枚数が完全に一致する場合に、trueを返します.
   */
  bool operator==(Hand rhs) const;

  bool operator!=(Hand rhs) const;

  /**
   * 持ち駒を１枚以上持っていれば、trueを返します.
   */
  bool any() const;

  /**
   * 持ち駒を１枚も持っていなければ、trueを返します.
   */
  bool none() const;

  /**
   * 駒種ptの持ち駒を１枚以上持っていれば、trueを返します.
   */
  bool has(PieceType pt) const;

  /**
   * 駒種がpt以外の持ち駒を１枚以上持っていれば、trueを返します.
   */
  bool has_any_piece_except(PieceType pt) const;

  /**
   * 駒種がptである持ち駒の枚数を返します.
   */
  int count(PieceType pt) const;

  /**
   * 左辺の持ち駒の枚数に、右辺の持ち駒の枚数を加えます.
   */
  Hand& operator+=(Hand rhs);

  /**
   * 左辺の持ち駒の枚数から、右辺の持ち駒の枚数を引きます.
   */
  Hand& operator-=(Hand rhs);

  /**
   * 左辺の持ち駒と、右辺の持ち駒の和集合を返します.
   *
   * ここで、持ち駒の和集合sは、
   *   sの歩の枚数 = max(lhsの歩の枚数, rhsの歩の枚数)
   *   sの香の枚数 = max(lhsの香の枚数, rhsの香の枚数)
   *    ...
   *   sの飛の枚数 = max(lhsの飛の枚数, rhsの飛の枚数)
   * と定義されるものです.
   */
  Hand& operator|=(Hand rhs);

  Hand operator+(Hand rhs) const { return Hand(*this) += rhs; }
  Hand operator-(Hand rhs) const { return Hand(*this) -= rhs; }
  Hand operator|(Hand rhs) const { return Hand(*this) |= rhs; }

  /**
   * 駒種 pt の持ち駒の枚数を num 枚にセットします.
   * @param pt  枚数を変更したい持ち駒の種類
   * @param num 駒の枚数
   */
  Hand& set(PieceType pt, int num);

  /**
   * 駒種 pt の持ち駒の枚数をゼロ枚にリセットします.
   */
  Hand& reset(PieceType pt);

  /**
   * 駒種 pt の持ち駒を１枚増やします.
   * @param pt １枚増やす持ち駒の種類
   */
  Hand& add_one(PieceType pt);

  /**
   * 駒種 pt の持ち駒を１枚減らします.
   * @param pt １枚減らす持ち駒の種類
   */
  Hand& remove_one(PieceType pt);

  /**
   * Hand型から、uint32_t型に変換します.
   */
  uint32_t ToUint32() const;

  /**
   * uint32_t型整数から、Hand型に変換します.
   */
  static Hand FromUint32(uint32_t u32);

  /**
   * 左辺の持ち駒が、右辺の持ち駒に対して、優越関係を有していれば、trueを返します.
   *
   * ここでいう、「左辺の持ち駒が、右辺の持ち駒に優越する」とは、以下の条件を全て満たしていることを
   * いいます。
   *   - 左辺の歩の枚数 >= 右辺の歩の枚数
   *   - 左辺の香の枚数 >= 右辺の香の枚数
   *   - 左辺の桂の枚数 >= 右辺の桂の枚数
   *   - 左辺の銀の枚数 >= 右辺の銀の枚数
   *   - 左辺の金の枚数 >= 右辺の金の枚数
   *   - 左辺の角の枚数 >= 右辺の角の枚数
   *   - 左辺の飛の枚数 >= 右辺の飛の枚数
   *
   * @param rhs 右辺（right hand side）の持ち駒
   * @return 左辺の持ち駒が、右辺の持ち駒に優越する場合は、true
   */
  bool Dominates(Hand rhs) const;

  /**
   * 左辺の持ち駒にはあるが、右辺の持ち駒にはない駒を返します.
   *
   * 例えば、左辺の持ち駒が「歩２、銀３」で、右辺の持ち駒が「銀２」の場合には、
   * 　　「歩２」の持ち駒
   * を返します。
   *
   * @param rhs 右辺（right hand side）の持ち駒
   * @return 左辺の持ち駒にはあるが、右辺の持ち駒にはない駒
   */
  Hand GetMonopolizedPieces(Hand rhs) const;

  /**
   * 各持ち駒の有無を、ビットセットにして返します.
   */
  HandSet GetHandSet() const;

  /**
   * 持ち駒のSFEN表記を文字列で返します.
   * @param side_to_move 手番
   * @return 持ち駒のSFEN表記
   */
  std::string ToSfen(Color side_to_move) const;

  /**
   * SFEN表記を解析してから、Handクラスを作成します.
   * @param sfen         持ち駒のSFEN表記
   * @param side_to_move 手番
   * @return SFEN表記に対応する持ち駒
   */
  static Hand FromSfen(const std::string& sfen, Color side_to_move);

  /**
   * 持ち駒の内部状態が正しい場合は、trueを返します（デバッグ用）.
   */
  bool IsOk() const;

 private:
  Hand(uint32_t h) : hand_(h) {
    assert(IsOk());
  }

  /** 各駒の枚数を保存するのに使うビットの範囲 */
  static constexpr Key keys_[8] = {
      Key( 0, 32), // ダミー
      Key( 0,  5), // 歩
      Key( 6,  9), // 香
      Key(10, 13), // 桂
      Key(14, 17), // 銀
      Key(18, 21), // 金
      Key(22, 25), // 角
      Key(26, 29), // 飛
  };
  static_assert(keys_[kPawn  ].mask == 0x0000001f, "");
  static_assert(keys_[kLance ].mask == 0x000001c0, "");
  static_assert(keys_[kKnight].mask == 0x00001c00, "");
  static_assert(keys_[kSilver].mask == 0x0001c000, "");
  static_assert(keys_[kGold  ].mask == 0x001c0000, "");
  static_assert(keys_[kBishop].mask == 0x01c00000, "");
  static_assert(keys_[kRook  ].mask == 0x1c000000, "");

  enum BorrowMask : uint32_t {
    kBorrowPawn   = keys_[kPawn  ].mask + keys_[kPawn  ].first_bit,
    kBorrowLance  = keys_[kLance ].mask + keys_[kLance ].first_bit,
    kBorrowKnight = keys_[kKnight].mask + keys_[kKnight].first_bit,
    kBorrowSilver = keys_[kSilver].mask + keys_[kSilver].first_bit,
    kBorrowGold   = keys_[kGold  ].mask + keys_[kGold  ].first_bit,
    kBorrowBishop = keys_[kBishop].mask + keys_[kBishop].first_bit,
    kBorrowRook   = keys_[kRook  ].mask + keys_[kRook  ].first_bit,
    kBorrowMask   = kBorrowPawn | kBorrowLance | kBorrowKnight | kBorrowSilver
                  | kBorrowGold | kBorrowBishop | kBorrowRook
  };

  BitField<uint32_t> hand_;
};

inline bool Hand::operator==(Hand rhs) const {
  return hand_ == rhs.hand_;
}

inline bool Hand::operator!=(Hand rhs) const {
  return !(*this == rhs);
}

inline bool Hand::any() const {
  return hand_.any();
}

inline bool Hand::none() const {
  return !any();
}

inline bool Hand::has(PieceType pt) const {
  assert(IsDroppablePieceType(pt));
  return hand_.test(keys_[pt]);
}

inline bool Hand::has_any_piece_except(PieceType pt) const {
  assert(IsDroppablePieceType(pt));
  return hand_ & keys_[pt].reset_mask;
}

inline int Hand::count(PieceType pt) const {
  assert(IsDroppablePieceType(pt));
  return hand_[keys_[pt]];
}

inline Hand& Hand::operator+=(Hand rhs) {
  assert(rhs.IsOk());
  hand_ = BitField<uint32_t>(hand_ + rhs.hand_);
  assert(IsOk());
  return *this;
}

inline Hand& Hand::operator-=(Hand rhs) {
  assert(rhs.IsOk());
  hand_ = BitField<uint32_t>(hand_ - rhs.hand_);
  assert(IsOk());
  return *this;
}

inline Hand& Hand::set(PieceType pt, int num) {
  assert(IsDroppablePieceType(pt));
  hand_.set(keys_[pt], num);
  assert(IsOk());
  return *this;
}

inline Hand& Hand::reset(PieceType pt) {
  assert(IsDroppablePieceType(pt));
  hand_.reset(keys_[pt]);
  assert(IsOk());
  return *this;
}

inline Hand& Hand::add_one(PieceType pt) {
  assert(IsDroppablePieceType(pt));
  hand_.increment(keys_[pt]);
  assert(IsOk());
  return *this;
}

inline Hand& Hand::remove_one(PieceType pt) {
  assert(IsDroppablePieceType(pt));
  assert(has(pt));
  hand_.decrement(keys_[pt]);
  assert(IsOk());
  return *this;
}

inline uint32_t Hand::ToUint32() const {
  return hand_;
}

inline Hand Hand::FromUint32(uint32_t u32) {
  return Hand(u32);
}

inline bool Hand::Dominates(Hand rhs) const {
  assert(rhs.IsOk());
  return ((hand_ - rhs.hand_) & kBorrowMask) == 0;
}

#endif /* HAND_H_ */
