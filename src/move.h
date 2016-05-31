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

#ifndef MOVE_H_
#define MOVE_H_

#include <string>
#include "common/array.h"
#include "common/bitfield.h"
#include "piece.h"
#include "square.h"
#include "types.h"

class Position;

/**
 * 指し手を表すクラスです.
 */
class Move {
 public:
  typedef BitField<uint32_t>::Key Key;

  enum {
    /** 指し手の完全ハッシュのサイズ */
    kPerfectHashSize  = 0x4000, // 16384

    /** 最大合法手数 */
    kMaxLegalMoves    = 700,

    /** 最大合法王手回避手数 */
    kMaxLegalEvasions = 256,
  };

  /**
   * デフォルトコンストラクタでは、速度低下防止のため、値の初期化は行われません.
   */
  Move() noexcept {
  }

  /**
   * 「打つ手」を作成します.
   */
  Move(Piece dropped, Square dst);

  /**
   * 「打つ手」を作成します.
   */
  Move(Color c, PieceType dropped, Square dst);

  /**
   * 「動かす手」を作成します.
   */
  Move(Piece moved, Square src, Square dst,
       bool promotion = false, Piece captured = kNoPiece);

  /**
   * 「動かす手」を作成します.
   */
  Move(Color c, PieceType moved, Square src, Square dst,
       bool promotion = false, Piece captured = kNoPiece);

  /**
   * 32ビット整数から指し手のデータを作るための、ファクトリです.
   * コンストラクタとは異なり、validationが行われません。
   */
  static constexpr Move Create(uint32_t u32) {
    return Move(u32);
  }

  /**
   * 「打つ手」のファクトリです.
   * コンストラクタとは異なり、validationが行われません。
   */
  static Move Create(Piece dropped, Square dst);

  /**
   * 「動かす手」のファクトリです.
   * コンストラクタとは異なり、validationが行われません。
   */
  static Move Create(Piece moved, Square src, Square dst,
                     bool promotion = false, Piece captured = kNoPiece);

  /**
   * 両辺の指し手が等しい場合に、trueを返します.
   */
  bool operator==(Move rhs) const;

  /**
   * 両辺の指し手が異なる場合に、trueを返します.
   */
  bool operator!=(Move rhs) const;

  /**
   * 動かす前の駒を返します.
   */
  Piece piece() const;

  /**
   * 動かした後の駒を返します.
   *
   * 成る手の場合は、
   *   - piece() => 成る前の駒を返す
   *   - piece_after_move() => 成った後の駒を返す
   * という違いがあります。
   *
   * 成る手以外の場合は、piece()とpiece_after_move()で返り値に違いはありません。
   */
  Piece piece_after_move() const;

  /**
   * 取った駒を返します.
   */
  Piece captured_piece() const;

  /**
   * 動かす前の駒の種類を返します.
   */
  PieceType piece_type() const;

  /**
   * 動かした後の駒の種類を返します.
   *
   * 成る手の場合は、
   *   - piece_type() => 成る前の駒の種類を返す
   *   - piece_type_after_move() => 成った後の駒の種類を返す
   * という違いがあります。
   *
   * 成る手以外の場合は、piece_type()とpiece_type_after_move()で返り値に違いはありません。
   */
  PieceType piece_type_after_move() const;

  /**
   * 取った駒の種類を返します.
   */
  PieceType captured_piece_type() const;

  /**
   * 移動元のマスを返します.
   * 注意: 打つ手の場合、このメソッドの動作は未定義です.
   */
  Square from() const;

  /**
   * 移動先のマスを返します.
   */
  Square to() const;

  /**
   * 打つ手であれば、trueを返します.
   */
  bool is_drop() const;

  /**
   * 取る手であれば、trueを返します.
   */
  bool is_capture() const;

  /**
   * 成る手であれば、trueを返します.
   */
  bool is_promotion() const;

  /**
   * 取る手または成る手であれば、trueを返します.
   */
  bool is_capture_or_promotion() const;

  /**
   * 静かな手であれば、trueを返します.
   *
   * ここでいう「静かな手」とは、以下の２つの条件をみたす手のことです。
   *   - 不成の手 または 銀が成る手 であること
   *   - 取る手ではないこと
   */
  bool is_quiet() const;

  /**
   * 歩を打つ手であれば、trueを返します.
   * 打ち歩詰めの判定に便利です。
   */
  bool is_pawn_drop() const;

  /**
   * 実在する指し手であれば、trueを返します.
   * 他方、kMoveNoneやkMoveNullの場合は、実在しない仮想の手なので、falseを返します.
   */
  bool is_real_move() const;

  /**
   * 明らかに損な指し手の場合は、trueを返します.
   *
   * ここでいう「損な指し手」とは、具体的には、
   *   - 歩・角・飛の不成
   *   - 2段目・8段目の香の不成
   * を意味しています。
   *
   * （参考文献）
   *   - 保木邦仁: コンピュータ将棋における全幅探索とfutility pruningの応用, 情報処理,
   *     Vol. 47, p. 889, 2006.
   */
  bool IsInferior() const;

  /**
   * 取った駒をセットします.
   *
   * 取った駒をセットする方法としては、
   *   - コンストラクタ段階でセットする
   *   - コンストラクタ段階ではセットせず、set_captured_piece()メソッドでセットする
   * の２通りがあることになります。
   *
   * このように２通りの方法を用意している理由は、指し手生成部の高速化のためです。
   */
  Move& set_captured_piece(Piece p);

  /**
   * Move型から、32ビット整数型に変換します.
   */
  uint32_t ToUint32() const;

  /**
   * Move型から、16ビット整数型に変換します.
   */
  uint16_t ToUint16() const;

  /**
   * 32ビット整数型から、Move型に変換します.
   */
  static Move FromUint32(uint32_t u32);

  /**
   * 16ビット整数型から、Move型に変換します.
   * TODO 現状では未実装です。
   */
  static Move FromUint16(uint16_t, const Position&);

  /**
   * 完全ハッシュ関数を用いて、指し手のハッシュ値を求めます.
   * なお、このメソッドが利用可能なのは、現在の実装では「静かな手」に限られます。
   *
   * （完全ハッシュ関数についての参考文献）
   *   - Bob Jenkins: Minimal Perfect Hashing, http://burtleburtle.net/bob/hash/perfect.html.
   */
  uint32_t PerfectHash() const;

  /**
   * 指し手のSFEN表記を返します.
   */
  std::string ToSfen() const;

  /**
   * SFEN表記から、Moveオブジェクトを作ります.
   *
   * SFEN表記自体からは、「動かす駒の種類」と「取った駒の種類」が分からないので、
   * その情報を補う必要があります。
   *
   * @param sfen     指し手のSFEN表記
   * @param moved    動かす駒
   * @param captured 取った駒
   * @param SFENに対応するMoveオブジェクト
   */
  static Move FromSfen(const std::string& sfen, Piece moved, Piece captured = kNoPiece);

  /**
   * SFEN表記から、Moveオブジェクトを作ります.
   *
   * SFEN表記自体からは、「動かす駒の種類」と「取った駒の種類」が分からないので、
   * その情報を補うためにPositionオブジェクトを利用しています。
   *
   * @param sfen 指し手のSFEN表記
   * @param pos  その指し手を指す局面
   * @param SFENに対応するMoveオブジェクト
   */
  static Move FromSfen(const std::string& sfen, const Position& pos);

  /**
   * 指し手を180度回転させます.
   *
   * 具体的には、
   *   - 指し手の先後を入れ替える
   *   - 移動元や移動先を180度回転させる
   * といった変換を行います。
   *
   * 例えば、「▲２六歩」にこのメソッドを適用すると、「△８四歩」が得られます。
   */
  Move& Flip();

  /**
   * 指し手の内部状態が正しければ、trueを返します（デバッグ用）.
   */
  bool IsOk() const;

 private:
  /*
   * Moveオブジェクトでは、32ビットの領域を以下のように割り当てています。
   *
   * <pre>
   * xxxxxxxx xxxxxxxx xxxxxxxx x1111111 移動先のマス
   * xxxxxxxx xxxxxxxx xxxxxxxx 1xxxxxxx 成る手のフラグ
   * xxxxxxxx xxxxxxxx x1111111 xxxxxxxx 移動元のマス
   * xxxxxxxx xxxxxxxx 1xxxxxxx xxxxxxxx 打つ手のフラグ
   * xxxxxxxx xxx11111 xxxxxxxx xxxxxxxx 動かす駒
   * xxxxxx11 111xxxxx xxxxxxxx xxxxxxxx 取る駒
   * </pre>
   */
  static constexpr Key kKeyDestination = Key( 0,  7);
  static constexpr Key kKeyPromotion   = Key( 7,  8);
  static constexpr Key kKeySource      = Key( 8, 15);
  static constexpr Key kKeyDrop        = Key(15, 16);
  static constexpr Key kKeyPieceType   = Key(16, 20);
  static constexpr Key kKeyPiece       = Key(16, 21);
  static constexpr Key kKeyCaptured    = Key(21, 26);
  static constexpr Key kKeyPerfectHash = Key( 0, 21);
  static constexpr Key kKeyAll         = Key( 0, 26);

  /** ファクトリメソッドから呼び出される、コンストラクタです. */
  constexpr explicit Move(uint32_t u32) : move_(u32) {}

  BitField<uint32_t> move_;
  static const Array<uint16_t, 2048> phash_table_;
};

/**
 * 「指し手が何もない」ことを意味する定数です.
 */
constexpr Move kMoveNone = Move::Create(0x0000);

/**
 * 「パス」を表す定数です.
 *
 * 将棋のルール上は、本来パスは存在しませんが、null move pruning等の実装に必要なので、
 * このような定数を用意しています。
 */
constexpr Move kMoveNull = Move::Create(0x8080);

/**
 * 「指し手」と「その指し手の得点」のペアです.
 * 指し手を並べ替える（ソートする）際に、指し手に得点を付与する必要があるため、このようなペアを用います。
 */
struct ExtMove {
  /** 指し手 */
  Move move;

  /** 指し手の得点（指し手の並べ替え用） */
  int32_t score;
};

inline Move::Move(Piece dropped, Square dst)
    : Move(Create(dropped, dst)) {
  assert(IsOk());
}

inline Move::Move(Color c, PieceType dropped, Square dst)
    : Move(Create(Piece(c, dropped), dst)) {
  assert(IsOk());
}

inline Move::Move(Piece moved, Square src, Square dst, bool promotion,
                  Piece captured)
    : Move(Create(moved, src, dst, promotion, captured)) {
  assert(IsOk());
}

inline Move::Move(Color c, PieceType moved, Square src, Square dst,
                  bool promotion, Piece captured)
    : Move(Create(Piece(c, moved), src, dst, promotion, captured)) {
  assert(IsOk());
}

inline Move Move::Create(Piece dropped, Square dst) {
  assert(dropped.IsOk());
  assert(dst.IsOk());
  uint32_t u32 = kKeyDrop.mask
               | static_cast<uint32_t>(dropped) << kKeyPiece.shift
               | static_cast<uint32_t>(dst    ) << kKeyDestination.shift;
  return Move(u32);
}

inline Move Move::Create(Piece moved, Square src, Square dst, bool promotion,
                         Piece captured) {
  assert(moved.IsOk());
  assert(src.IsOk());
  assert(dst.IsOk());
  uint32_t u32 = static_cast<uint32_t>(moved    ) << kKeyPiece.shift
               | static_cast<uint32_t>(src      ) << kKeySource.shift
               | static_cast<uint32_t>(dst      ) << kKeyDestination.shift
               | static_cast<uint32_t>(promotion) << kKeyPromotion.shift
               | static_cast<uint32_t>(captured ) << kKeyCaptured.shift;
  return Move(u32);
}

inline bool Move::operator==(Move rhs) const {
  return move_ == rhs.move_;
}

inline bool Move::operator!=(Move rhs) const {
  return !(*this == rhs);
}

inline Piece Move::piece() const {
  return Piece(move_[kKeyPiece]);
}

inline Piece Move::piece_after_move() const {
  return piece().piece_after_move(is_promotion());
}

inline Piece Move::captured_piece() const {
  return Piece(move_[kKeyCaptured]);
}

inline PieceType Move::piece_type() const {
  return piece().type();
}

inline PieceType Move::piece_type_after_move() const {
  return piece_after_move().type();
}

inline PieceType Move::captured_piece_type() const {
  return captured_piece().type();
}

inline Square Move::from() const {
  assert(!is_drop());
  return Square(move_[kKeySource]);
}

inline Square Move::to() const {
  return Square(move_[kKeyDestination]);
}

inline bool Move::is_drop() const {
  return move_.test(kKeyDrop);
}

inline bool Move::is_capture() const {
  return move_.test(kKeyCaptured);
}

inline bool Move::is_promotion() const {
  return move_.test(kKeyPromotion);
}

inline bool Move::is_capture_or_promotion() const {
  return move_ & (kKeyCaptured.mask | kKeyPromotion.mask);
}

inline bool Move::is_quiet() const {
  const uint32_t kMask = kKeyPieceType.mask
                       | kKeyPromotion.mask
                       | kKeyCaptured.mask;
  const uint32_t kNonCaptureSilverPromotion = (kSilver << kKeyPieceType.shift)
                                            | kKeyPromotion.mask;
  return !is_capture_or_promotion()
      || (move_ & kMask) == kNonCaptureSilverPromotion;
}

inline bool Move::is_pawn_drop() const {
  const uint32_t kMask = kKeyDrop.mask | kKeyPieceType.mask;
  const uint32_t kPawnDrop = (kPawn << kKeyPieceType.shift) | kKeyDrop.mask;
  return (move_ & kMask) == kPawnDrop;
}

inline bool Move::is_real_move() const {
  return move_.test(kKeyPiece);
}

inline Move& Move::set_captured_piece(Piece p) {
  assert(captured_piece() == kNoPiece);
  uint32_t captured = static_cast<uint32_t>(p) << kKeyCaptured.shift;
  move_ = BitField<uint32_t>(move_ | captured);
  return *this;
}

inline uint32_t Move::ToUint32() const {
  return static_cast<uint32_t>(move_);
}

inline Move Move::FromUint32(uint32_t u32) {
  return Move(u32);
}

#endif /* MOVE_H_ */
