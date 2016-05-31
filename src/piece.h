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

#ifndef PIECE_H_
#define PIECE_H_

#include <string>
#include "common/arraymap.h"
#include "types.h"

/**
 * 駒の種類（手番で区別しない）を表します.
 */
enum PieceType {
  kNoPieceType = 0,       /**< 駒がないことを意味します */
  kPawn    = 1,           /**< 歩 */
  kLance   = 2,           /**< 香 */
  kKnight  = 3,           /**< 桂 */
  kSilver  = 4,           /**< 銀 */
  kGold    = 5,           /**< 金 */
  kBishop  = 6,           /**< 角 */
  kRook    = 7,           /**< 飛 */
  kKing    = 8,           /**< 玉 */
  kPPawn   = kPawn   + 8, /**< と */
  kPLance  = kLance  + 8, /**< 成香 */
  kPKnight = kKnight + 8, /**< 成桂 */
  kPSilver = kSilver + 8, /**< 成銀 */
  kHorse   = kBishop + 8, /**< 馬 */
  kDragon  = kRook   + 8  /**< 龍 */
};

DEFINE_SPECIALIZED_LIMITS_CLASS_FOR_ENUM(PieceType, 0, 15)

/**
 * 打つことができる駒種であれば、trueを返します.
 */
constexpr bool IsDroppablePieceType(PieceType pt) {
  return kPawn <= pt && pt <= kRook;
}

/**
 * 成ることができる駒種であれば、trueを返します.
 */
constexpr bool IsPromotablePieceType(PieceType pt) {
  return kPawn <= pt && pt <= kRook && pt != kGold;
}

/**
 * 既に成っている駒種であれば、trueを返します.
 */
constexpr bool IsPromotedPieceType(PieceType pt) {
  return kPPawn <= pt && pt <= kDragon && pt != (kGold + 8);
}

/**
 * 成った後の駒種を返します.
 * なお、成ることができない駒であれば、kNoPieceTypeを返します.
 */
constexpr PieceType GetPromotedType(PieceType pt) {
  return IsPromotablePieceType(pt)
       ? static_cast<PieceType>(pt | 8)
       : kNoPieceType;
}

/**
 * 成る前の駒種を返します.
 */
constexpr PieceType GetOriginalType(PieceType pt) {
  return pt == kKing ? kKing : static_cast<PieceType>(pt & 7);
}

/**
 * 与えられた駒種の、最大の枚数を返します.
 */
constexpr int GetMaxNumber(PieceType pt) {
  return pt == kPawn   || pt == kPPawn   ? 18
       : pt >= kLance  && pt <= kGold    ?  4
       : pt >= kBishop && pt <= kKing    ?  2
       : pt >= kPLance && pt <= kPSilver ?  4
       : pt >= kHorse  && pt <= kDragon  ?  2
       :                                    0;
}

/**
 * 駒を表します.
 */
class Piece {
 public:
  explicit constexpr Piece(int i = 0)
      : piece_(i) {
  }

  explicit constexpr Piece(Color c, PieceType pt)
      : piece_(16 * static_cast<int>(c) + static_cast<int>(pt)) {
  }

  constexpr operator int() const {
    return piece_;
  }

  /**
   * この駒を所有している対局者の手番を返します.
   * 例えば、この駒が「▲歩」であれば、「先手（kBlack）」を返します。
   */
  Color color() const;

  /**
   * この駒の種類を返します.
   * 例えば、この駒が「▲金」であれば、「金（kGold）」を返します。
   */
  PieceType type() const;

  /**
   * この駒を持ち駒にしたときの、駒種を返します.
   * 例えば、この駒が「▲桂」であれば、「桂（kKnight）」を返します。
   */
  PieceType hand_type() const;

  /**
   * この駒が成る前の駒種を返します.
   * 例えば、この駒が「▲馬」であれば、「角（kBishop）」を返します。
   */
  PieceType original_type() const;

  /**
   * 手番を逆にした駒を返します.
   * 例えば、この駒が「▲銀」であれば、「△銀（kWhiteSilver）」を返します。
   */
  Piece opponent_piece() const;

  /**
   * 成った駒を返します.
   * 例えば、この駒が「▲歩」であれば、「▲と（kBlackPPawn）」を返します。
   */
  Piece promoted_piece() const;

  /**
   * 動かした後の駒の種類を返します.
   * すなわち、
   *   - 成る手の場合は、成った後の駒を返します
   *   - 成る手以外の場合は、動かした駒をそのまま返します
   */
  Piece piece_after_move(bool move_is_promotion) const;

  /**
   * この駒が取られた場合の、相手側が取得する持ち駒を返します.
   * 例えば、この駒が「▲龍」であれば、「△飛（kWhiteRook）」を返します。
   */
  Piece opponent_hand_piece() const;

  /**
   * この駒が、与えられた手番側の駒であれば、trueを返します.
   */
  bool is(Color c) const;

  /**
   * この駒が、与えられた駒種の駒であれば、trueを返します.
   */
  bool is(PieceType pt) const;

  /**
   * この駒が、飛び駒（香・角・飛・馬・龍）であれば、trueを返します.
   */
  bool is_slider() const;

  /**
   * この駒が、既に成った駒であれば、trueを返します.
   */
  bool is_promoted() const;

  /**
   * この駒が、打つことができる駒であれば、trueを返します.
   */
  bool is_droppable() const;

  /**
   * この駒が、成ることができる駒であれば、trueを返します.
   */
  bool can_promote() const;

  /**
   * この駒を与えられた段に置くことができない場合（「行きどころのない駒」になる場合）は、trueを返します.
   *
   * @code
   * kBlackPawn.may_not_be_placed_on(kRank1);  // true
   * kBlackPawn.may_not_be_placed_on(kRank2);  // false
   * kWhiteLance.may_not_be_placed_on(kRank1); // false
   * kWhiteLance.may_not_be_placed_on(kRank9); // true
   * @endcode
   */
  bool may_not_be_placed_on(Rank r) const;

  /**
   * この駒のSFEN表記を返します.
   */
  std::string ToSfen() const;

  /**
   * SFEN表記から、Piece型に変換します.
   */
  static Piece FromSfen(const std::string& sfen);

  /**
   * Pieceクラスが取りうる値の、最小値を返します.
   */
  static constexpr Piece min() {
    return Piece(0);
  }

  /**
   * Pieceクラスが取りうる値の、最大値を返します.
   */
  static constexpr Piece max() {
    return Piece(31);
  }

  /**
   * 全ての駒を要素に含むビットセットを返します.
   * 範囲for文に渡すと便利です。
   */
  static constexpr BitSet<Piece, 32> all_pieces();

  /**
   * 全ての駒種を要素に含むビットセットを返します.
   * 範囲for文に渡すと便利です。
   */
  static constexpr BitSet<PieceType, 16> all_piece_types();

  /**
   * 全ての持ち駒の種類を要素に含むビットセットを返します.
   * 範囲for文に渡すと便利です。
   */
  static constexpr BitSet<PieceType, 16> all_hand_types();

  /**
   * 内部状態が正しければ、trueを返します（デバッグ用）.
   */
  constexpr bool IsOk() const;

 private:
  int piece_;
};

constexpr Piece kNoPiece     (               0); /**< 駒がないことを表します */
constexpr Piece kWall        (              16); /**< 壁（将棋盤の外側を表します）*/
constexpr Piece kBlackPawn   (kBlack, kPawn   ); /**< ▲歩 */
constexpr Piece kBlackLance  (kBlack, kLance  ); /**< ▲香 */
constexpr Piece kBlackKnight (kBlack, kKnight ); /**< ▲桂 */
constexpr Piece kBlackSilver (kBlack, kSilver ); /**< ▲銀 */
constexpr Piece kBlackGold   (kBlack, kGold   ); /**< ▲金 */
constexpr Piece kBlackBishop (kBlack, kBishop ); /**< ▲角 */
constexpr Piece kBlackRook   (kBlack, kRook   ); /**< ▲飛 */
constexpr Piece kBlackKing   (kBlack, kKing   ); /**< ▲玉 */
constexpr Piece kBlackPPawn  (kBlack, kPPawn  ); /**< ▲と */
constexpr Piece kBlackPLance (kBlack, kPLance ); /**< ▲成香 */
constexpr Piece kBlackPKnight(kBlack, kPKnight); /**< ▲成桂 */
constexpr Piece kBlackPSilver(kBlack, kPSilver); /**< ▲成銀 */
constexpr Piece kBlackHorse  (kBlack, kHorse  ); /**< ▲馬 */
constexpr Piece kBlackDragon (kBlack, kDragon ); /**< ▲龍 */
constexpr Piece kWhitePawn   (kWhite, kPawn   ); /**< △歩 */
constexpr Piece kWhiteLance  (kWhite, kLance  ); /**< △香 */
constexpr Piece kWhiteKnight (kWhite, kKnight ); /**< △桂 */
constexpr Piece kWhiteSilver (kWhite, kSilver ); /**< △銀 */
constexpr Piece kWhiteGold   (kWhite, kGold   ); /**< △金 */
constexpr Piece kWhiteBishop (kWhite, kBishop ); /**< △角 */
constexpr Piece kWhiteRook   (kWhite, kRook   ); /**< △飛 */
constexpr Piece kWhiteKing   (kWhite, kKing   ); /**< △玉 */
constexpr Piece kWhitePPawn  (kWhite, kPPawn  ); /**< △と */
constexpr Piece kWhitePLance (kWhite, kPLance ); /**< △成香 */
constexpr Piece kWhitePKnight(kWhite, kPKnight); /**< △成桂 */
constexpr Piece kWhitePSilver(kWhite, kPSilver); /**< △成銀 */
constexpr Piece kWhiteHorse  (kWhite, kHorse  ); /**< △馬 */
constexpr Piece kWhiteDragon (kWhite, kDragon ); /**< △龍 */

inline Color Piece::color() const {
  assert(IsOk());
  assert(piece_ != kNoPiece && piece_ != kWall);
  return static_cast<Color>(piece_ / 16);
}

inline PieceType Piece::type() const {
  assert(IsOk());
  return static_cast<PieceType>(piece_ & 0xf);
}

inline PieceType Piece::hand_type() const {
  assert(IsOk());
  assert(piece_ != kNoPiece && piece_ != kWall);
  return static_cast<PieceType>(piece_ & 0x7);
}

inline PieceType Piece::original_type() const {
  assert(IsOk());
  assert(piece_ != kNoPiece && piece_ != kWall);
  return is(kKing) ? kKing : static_cast<PieceType>(piece_ & 0x7);
}

inline Piece Piece::opponent_piece() const {
  assert(IsOk());
  assert(piece_ != kNoPiece && piece_ != kWall);
  return Piece(piece_ ^ 0x10);
}

inline Piece Piece::promoted_piece() const {
  assert(IsOk());
  assert(piece_ != kNoPiece && piece_ != kWall);
  assert(piece_ != kBlackGold && piece_ != kWhiteGold);
  assert(piece_ != kBlackKing && piece_ != kWhiteKing);
  return Piece(piece_ | 0x8);
}

inline Piece Piece::piece_after_move(bool move_is_promotion) const {
  assert(IsOk());
  assert(piece_ != kNoPiece && piece_ != kWall);
  assert(can_promote() || !move_is_promotion);
  return Piece(piece_ + (0x8 * static_cast<int>(move_is_promotion)));
}

inline Piece Piece::opponent_hand_piece() const {
  assert(IsOk());
  assert(piece_ != kWall);
  assert(piece_ != kBlackKing && piece_ != kWhiteKing);
  return Piece((piece_ & 0x17) ^ 0x10);
}

inline bool Piece::is(Color c) const {
  return c == color();
}

inline bool Piece::is(PieceType pt) const {
  return pt == type();
}

inline bool Piece::is_slider() const {
  return BitSet<Piece, 32>{
    kBlackLance, kBlackBishop, kBlackRook, kBlackHorse, kBlackDragon,
    kWhiteLance, kWhiteBishop, kWhiteRook, kWhiteHorse, kWhiteDragon
  }.test(*this);
}

inline bool Piece::is_promoted() const {
  return BitSet<Piece, 32>{
    kBlackPPawn, kBlackPLance, kBlackPKnight, kBlackPSilver, kBlackHorse, kBlackDragon,
    kWhitePPawn, kWhitePLance, kWhitePKnight, kWhitePSilver, kWhiteHorse, kWhiteDragon
  }.test(*this);
}

inline bool Piece::is_droppable() const {
  return BitSet<Piece, 32>{
    kBlackPawn, kBlackLance, kBlackKnight, kBlackSilver, kBlackGold,
    kBlackBishop, kBlackRook,
    kWhitePawn, kWhiteLance, kWhiteKnight, kWhiteSilver, kWhiteGold,
    kWhiteBishop, kWhiteRook,
  }.test(*this);
}

inline bool Piece::can_promote() const {
  return BitSet<Piece, 32>{
    kBlackPawn, kBlackLance, kBlackKnight, kBlackSilver, kBlackBishop, kBlackRook,
    kWhitePawn, kWhiteLance, kWhiteKnight, kWhiteSilver, kWhiteBishop, kWhiteRook
  }.test(*this);
}

inline bool Piece::may_not_be_placed_on(Rank r) const {
  // TODO lookup table
  if (is(kPawn) || is(kLance)) {
    return relative_rank(color(), r) == kRank1;
  } else if (is(kKnight)) {
    return relative_rank(color(), r) <= kRank2;
  } else {
    return false;
  }
}

inline std::string Piece::ToSfen() const {
  assert(IsOk());
  static const char symbols[] = " PLNSGBRKPLNS BR#plnsgbrkplns br";
  if (is_promoted()) {
    return std::string{'+', symbols[piece_]};
  } else {
    return std::string{symbols[piece_]};
  }
}

inline Piece Piece::FromSfen(const std::string& sfen) {
  assert(sfen.size() == 1 || sfen.size() == 2);
  static const std::string symbols = " PLNSGBRKPLNS BR#plnsgbrkplns br";
  if (sfen.size() == 2) {
    assert(sfen[0] == '+');
    assert(symbols.find(sfen[1]) != std::string::npos);
    return Piece(symbols.find(sfen[1])).promoted_piece();
  } else {
    assert(symbols.find(sfen[0]) != std::string::npos);
    return Piece(symbols.find(sfen[0]));
  }
}

constexpr BitSet<Piece, 32> Piece::all_pieces() {
  return BitSet<Piece, 32>{
    kBlackPawn, kBlackLance, kBlackKnight, kBlackSilver, kBlackGold,
    kBlackBishop, kBlackRook, kBlackKing, kBlackPPawn, kBlackPLance,
    kBlackPKnight, kBlackPSilver, kBlackHorse, kBlackDragon,
    kWhitePawn, kWhiteLance, kWhiteKnight, kWhiteSilver, kWhiteGold,
    kWhiteBishop, kWhiteRook, kWhiteKing, kWhitePPawn, kWhitePLance,
    kWhitePKnight, kWhitePSilver, kWhiteHorse, kWhiteDragon
  };
}

constexpr BitSet<PieceType, 16> Piece::all_piece_types() {
  return BitSet<PieceType, 16>{
    kPawn, kLance, kKnight, kSilver, kGold, kBishop, kRook, kKing,
    kPPawn, kPLance, kPKnight, kPSilver, kHorse, kDragon
  };
}

constexpr BitSet<PieceType, 16> Piece::all_hand_types() {
  return BitSet<PieceType, 16> {
    kPawn, kLance, kKnight, kSilver, kGold, kBishop, kRook
  };
}

constexpr bool Piece::IsOk() const {
  return piece_ >= min() && piece_ <= max()
      && piece_ != kBlackGold + 8
      && piece_ != kWhiteGold + 8;
}

#endif /* PIECE_H_ */
