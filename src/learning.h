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

#ifndef LEARNING_H_
#define LEARNING_H_

#include <cassert>
#include "common/arraymap.h"
#include "common/iterator.h"
#include "common/math.h"
#include "common/operators.h"
#include "common/pack.h"
#include "common/range.h"
#include "psq.h"
#include "position.h"

/**
 * 評価関数の学習を行うためのクラスです.
 */
class Learning {
 public:
  /**
   * 評価関数パラメータの学習を開始します.
   * @param use_rootstrap 学習時にRootStrapを併用する場合は、trueにする
   * @param use_logistic_regression 学習時にロジスティック回帰（「激指」方式）を併用する場合は、true
   */
  static void LearnEvaluationParameters(bool use_rootstrap,
                                        bool use_logistic_regression);
};

/**
 * マスの「相対位置」を表すクラスです.
 */
class RelativeSquare {
 public:
  explicit constexpr RelativeSquare(int rs = 0) : rs_(rs) {}
  RelativeSquare(Square from, Square to) : rs_(Square::relation(from, to)) {
    assert(min() <= rs_ && rs_ <= max());
  }
  constexpr operator int() const { return rs_; }
  static constexpr RelativeSquare min() { return RelativeSquare(  0); }
  static constexpr RelativeSquare max() { return RelativeSquare(153); }
  static Sequence<RelativeSquare> all_relative_squares() {
    return Sequence<RelativeSquare>(min(), max());
  }
 private:
  int rs_;
};
ENABLE_ADD_AND_SUBTRACT_OPERATORS(RelativeSquare)

/**
 * PsqIndexの相対位置版です.
 */
class RelativePsq {
 public:
  explicit constexpr RelativePsq(int rp = 0) : rp_(rp) {}
  RelativePsq(Square from, Square to) : rp_(Square::relation(from, to)) {
    assert(min() <= rp_ && rp_ <= max());
  }
  constexpr operator int() const { return rp_; }
  static constexpr RelativePsq min() { return RelativePsq(  0); }
  static constexpr RelativePsq max() { return RelativePsq(171); }
  static Sequence<RelativePsq> all_relative_psqs() {
    return Sequence<RelativePsq>(min(), max());
  }
  static RelativePsq OfBoard(Square from, Square to) {
    return RelativePsq(Square::relation(from, to));
  }
  static RelativePsq OfHand(int n) {
    assert(1 <= n && n <= 18);
    return RelativePsq(153 + n);
  }
  static RelativePsq FromAbsolute(Square ksq, PsqIndex psq) {
    if (psq.square() == kSquareNone) {
      Piece piece = psq.piece();
      int nth = psq - PsqIndex::OfHand(piece.color(), piece.type(), 1) + 1;
      return OfHand(nth);
    } else {
      return OfBoard(ksq, psq.square());
    }
  }
 private:
  int rp_;
};
ENABLE_ADD_AND_SUBTRACT_OPERATORS(RelativePsq)

/**
 * PsqIndexを180度回転します.
 *
 * 具体的は、
 *   - 先手の駒を後手の駒に、後手の駒を先手の駒にする
 *   - 駒の位置を、180度回転する
 * という操作が行われます。
 */
inline PsqIndex RotatePsqIndex(PsqIndex psq) {
  Piece piece = psq.piece();
  Square square = psq.square();
  if (square == kSquareNone) {
    int nth = psq - PsqIndex::OfHand(piece.color(), piece.type(), 1) + 1;
    return PsqIndex::OfHand(~piece.color(), piece.type(), nth);
  } else {
    return PsqIndex::OfBoard(piece.opponent_piece(), Square::rotate180(square));
  }
}

/**
 * 評価関数の学習において、個々のパラメータを保持するためのクラスです.
 * Packクラスでは、内部的にSSE演算を用いているので、単純な浮動小数点型よりも計算が高速化されています。
 */
typedef Pack<float, 4> PackedWeight;

/**
 * 次元下げを行うために拡張された評価パラメータです.
 */
struct ExtendedParams {
 public:
  /**
   * 各パラメータを更新する際に用いるファンクタです.
   */
  class Updater {
   public:
    Updater(PackedWeight inc) : increment_(inc) {}
    void apply(PackedWeight n, PackedWeight& item) {
      item += n * increment_;
    }
   private:
    PackedWeight increment_;
  };

  /**
   * 次元下げされた各パラメータの合計を計算する際のファンクタです.
   */
  struct Accumulator {
   public:
    Accumulator() : sum_(0.0f) {}
    void apply(PackedWeight n, const PackedWeight& item) {
      sum_ += n * item;
    }
    PackedWeight sum() const {
      return sum_;
    }
   private:
    PackedWeight sum_;
  };

  ExtendedParams() {}

  void Clear() {
    std::memset(this, 0, sizeof(*this));
  }

  PackedWeight* begin() {
    return reinterpret_cast<PackedWeight*>(this);
  }

  PackedWeight* end() {
    return reinterpret_cast<PackedWeight*>(this) + size();
  }

  PackedWeight& operator[](size_t i) {
    assert(i < size());
    return *(begin() + i);
  }

  size_t size() const {
    return sizeof(*this) / sizeof(PackedWeight);
  }

  /**
   * 駒割のパラメータ部分に使われている要素数です.
   */
  size_t size_of_material() const {
    return sizeof(material) / sizeof(PackedWeight);
  }

  /**
   * KPについて、次元下げを行います.
   */
  template<Color kKingColor, typename Operation>
  Operation EachKP(const Square king_sq, const PsqIndex psq, Operation op) {
    // 後手玉について評価するときは、符号を反転させる
    const PackedWeight sign = GetPackedSign3x1(kKingColor);

    // 駒の価値（２つのKPに分けて足し込むので、0.5ずつに分ける）
    PackedWeight flip = 0.5f * GetPackedSign3x1(psq.piece().color());
    op.apply(sign * flip, material[psq.piece().type()]);

    // 相対KP
    RelativePsq rp = RelativePsq::FromAbsolute(king_sq, psq);
    op.apply(sign, relative_kp[psq.piece()][rp]);

    // 絶対KP
    op.apply(sign, absolute_kp[king_sq][psq]);

    return op;
  }

  template<typename Operation>
  Operation EachPP(const PsqIndex idx1, const PsqIndex idx2, Operation op) {
    op = EachPP<kBlack>(idx1, idx2, op);
    op = EachPP<kWhite>(RotatePsqIndex(idx1), RotatePsqIndex(idx2), op);
    return op;
  }

  /**
   * PPについて、次元下げを行います.
   */
  template<Color kColor, typename Operation>
  Operation EachPP(const PsqIndex idx1, const PsqIndex idx2, Operation op) {
    // PPは、先手視点・後手視点で２回加えられるので、係数を半分にしておく
    const PackedWeight c = 0.5f * GetPackedSign2x2(kColor);

    // ２つのインデックスが同じ場合
    if (idx1 == idx2) {
      // 持ち駒
      // 注：持ち駒の次元下げはKPでも行っているので、重複して次元下げすることになるが、
      // 　　KPとは異なりPPでは手番が考慮されているので、別途PPでも次元下げする意味がある。
      if (idx1.square() == kSquareNone) {
        // 先手・後手の対称性を考慮し、駒１は常に先手の駒として評価する
        PackedWeight sign = GetPackedSign2x2(idx1.piece().color());
        PsqIndex idx = idx1.piece().color() == kBlack ? idx1 : RotatePsqIndex(idx1);
        op.apply(sign * c, hand_value[idx]);
      }
      return op;
    }

    // インデックスを入れ替えても同じ値を参照できるようにする
    PsqIndex i(std::max(int(idx1), int(idx2)));
    PsqIndex j(std::min(int(idx1), int(idx2)));

    // 先手・後手の対称性を考慮し、駒１は常に先手の駒として評価する
    const PackedWeight sign = GetPackedSign2x2(i.piece().color());
    if (i.piece().color() == kWhite) {
      i = RotatePsqIndex(i);
      j = RotatePsqIndex(j);
    }
    assert(i.piece().color() == kBlack);

    // 相対PP（盤上の駒同士の関係のみ）
    if (i.square() != kSquareNone && j.square() != kSquareNone) {
      PieceType i_pt = i.piece().type();
      Piece j_piece = j.piece();
      RelativeSquare rs(i.square(), j.square());
      // 相対PP
      op.apply(sign * c, relative_pp[i_pt][j_piece][rs]);
      // 相対PP（Y座標固定）
      op.apply(sign * c, relative_ppy[i_pt][j_piece][rs][i.square().rank()]);
    }

    // 絶対PP
    op.apply(sign * c, absolute_pp[i][j]);

    return op;
  }

  /**
   * 利き数のパラメータについて、次元下げを行います.
   */
  template<Color kKingColor, typename Operation>
  Operation EachControl(Square king_square, PsqControlIndex index, Operation op) {
    Square ksq = king_square;
    Square s = index.square();
    Piece p = index.piece();
    int own_controls = index.num_controls(kKingColor);
    int opp_controls = index.num_controls(~kKingColor);

    // 後手玉を基準として評価する場合は、マスの位置や駒の手番の反転処理を行う
    const PackedWeight sign = GetPackedSign2x2(kKingColor);
    if (kKingColor == kWhite) {
      ksq = Square::rotate180(ksq);
      s = Square::rotate180(s);
      p = p == kNoPiece ? kNoPiece : p.opponent_piece();
    }

    // 玉との相対位置を求める
    RelativeSquare relative_ksq(ksq, s);

    // a. 各マスの利き数 x そのマスの駒（絶対座標）　[玉のマス][マス][駒][味方の利き数][敵の利き数]
    op.apply(sign, psqc_absolute[ksq][s][p][own_controls][opp_controls]);

    // b. 各マスの利き数 x そのマスの駒（相対座標）　[玉との相対位置][駒][味方の利き数][敵の利き数]
    op.apply(sign, psqc_relative[relative_ksq][p][own_controls][opp_controls]);

    // c. 浮き駒・質駒の評価（絶対座標）　[玉のマス][マス][味方利きの有無][敵利きの有無]
    op.apply(sign, floating_or_hostage_absolute[ksq][s][p][own_controls][opp_controls]);

    // d. 浮き駒・質駒の評価（相対座標）　[玉との相対位置][駒][味方利きの有無][敵利きの有無]
    op.apply(sign, floating_or_hostage_relative[relative_ksq][p][own_controls][opp_controls]);

    // e. 味方の利き数（絶対座標）　[玉のマス][マス][味方の利き数]
    op.apply(sign, own_controls_absolute[ksq][s][own_controls]);

    // f. 味方の利き数（相対座標）　[玉との相対位置][味方の利き数]
    op.apply(sign, own_controls_relative[relative_ksq][own_controls]);

    // g. 敵の利き数（絶対座標）　[玉のマス][マス][敵の利き数]
    op.apply(sign, opp_controls_absolute[ksq][s][opp_controls]);

    // h. 敵の利き数（相対座標）　[玉との相対位置][敵の利き数]
    op.apply(sign, opp_controls_relative[relative_ksq][opp_controls]);

    // i. 利き数の勝ち負け　[玉のマス][マス][sign(味方の利き数 - 敵の利き数)]
    op.apply(sign, diff_controls_absolute[ksq][s][math::sign(own_controls - opp_controls)]);

    // j. 敵・味方の利き数の組み合わせ　[玉との相対位置][味方の利き数][敵の利き数]
    op.apply(sign, controls_relative[relative_ksq][own_controls][opp_controls]);

    // k. 駒取りの脅威の評価　[駒の種類][味方の利きの数][敵の利き数]
    if (p != kNoPiece && p.color() == kBlack) {
      op.apply(sign, capture_threat[p.type()][own_controls][opp_controls]);
    }

    return op;
  }

  /**
   * 玉の安全度パラメータについて、次元下げを行います.
   */
  template<Color kKingColor, typename Operation>
  Operation EachKingSafety(HandSet hs, Direction dir, Piece piece,
                           int attacks, int defenses, Operation op) {
    assert(0 <= attacks && attacks <= 3);
    assert(0 <= defenses && defenses <= 3);

    // 後手玉について評価するときは、符号を反転させる
    const PackedWeight sign = GetPackedSign2x2(kKingColor);

    // 穴熊か否かのフラグを取得する
    const bool anaguma = hs.test(kNoPieceType);

    // 玉の隣のマスが盤外になってしまう場合は利き数等を評価できないので、玉が端にいるという事実だけ評価する
    if (piece == kWall) {
      // a. 玉が将棋盤の端にいる
      // [穴熊か否か][方向]
      op.apply(sign, king_on_the_edge[anaguma][dir]);

      return op;
    }

    // b. 玉の弱点（玉以外の機器がない空間）かどうか
    // [穴熊か否か][方向][空きマスか否か][受け方の利きの有無]
    op.apply(sign, weak_points_of_king[anaguma][dir][piece == kNoPiece][defenses > 0]);

    // c. 攻め方の利き数
    // [穴熊か否か][方向][攻め方の利き数]
    op.apply(sign, attacker_controls[anaguma][dir][attacks]);

    // d. 利き数の差
    // [穴熊か否か][方向][攻め方の利き数 - 受け方の利き数]
    op.apply(sign, diff_controls[anaguma][dir][attacks - defenses]);

    // e. 利きの勝ち負け（そのマスにどちらかの駒があるか、空白かで場合分け）
    // [穴熊か否か][方向][攻め方の駒の有無][受け方の駒の有無][利きの勝ち・負け・引き分け]
    {
      bool attacker = piece != kNoPiece && piece.color() != kKingColor;
      bool defender = piece != kNoPiece && piece.color() == kKingColor;
      int advantage = math::sign(static_cast<int>(attacks - defenses));
      op.apply(sign, controls_advantage[anaguma][dir][attacker][defender][advantage]);
    }

    // f. 周囲の駒の評価（質駒や、浮き駒を評価する）
    // [穴熊か否か][方向][盤上の駒][攻め方の利き数][受け方の利き数]
    op.apply(sign, neighborhood_pieces[anaguma][dir][piece][attacks][defenses]);

    // g. 駒打ちによる王手の脅威
    // [穴熊か否か][攻め方の持ち駒の種類][方向][空きマスか否か][攻め方の利きの有無][受け方の利きの有無]
    {
      bool e = piece == kNoPiece;
      bool a1 = attacks >= 1;
      bool d1 = defenses >= 1;
      if (hs.test(kPawn  )) op.apply(sign, drop_check_threat[anaguma][kPawn  ][dir][e][a1][d1]);
      if (hs.test(kLance )) op.apply(sign, drop_check_threat[anaguma][kLance ][dir][e][a1][d1]);
      if (hs.test(kKnight)) op.apply(sign, drop_check_threat[anaguma][kKnight][dir][e][a1][d1]);
      if (hs.test(kSilver)) op.apply(sign, drop_check_threat[anaguma][kSilver][dir][e][a1][d1]);
      if (hs.test(kGold  )) op.apply(sign, drop_check_threat[anaguma][kGold  ][dir][e][a1][d1]);
      if (hs.test(kBishop)) op.apply(sign, drop_check_threat[anaguma][kBishop][dir][e][a1][d1]);
      if (hs.test(kRook  )) op.apply(sign, drop_check_threat[anaguma][kRook  ][dir][e][a1][d1]);
    }

    // h. 持ち駒についてだけ、次元下げしたもの
    // [穴熊か否か][攻め方の持ち駒の種類][方向][盤上にある駒][攻め方の利き数][受け方の利き数]
    {
      int a = attacks, d = defenses;
      if (hs.test(kPawn  )) op.apply(sign, king_safety[anaguma][kPawn  ][dir][piece][a][d]);
      if (hs.test(kLance )) op.apply(sign, king_safety[anaguma][kLance ][dir][piece][a][d]);
      if (hs.test(kKnight)) op.apply(sign, king_safety[anaguma][kKnight][dir][piece][a][d]);
      if (hs.test(kSilver)) op.apply(sign, king_safety[anaguma][kSilver][dir][piece][a][d]);
      if (hs.test(kGold  )) op.apply(sign, king_safety[anaguma][kGold  ][dir][piece][a][d]);
      if (hs.test(kBishop)) op.apply(sign, king_safety[anaguma][kBishop][dir][piece][a][d]);
      if (hs.test(kRook  )) op.apply(sign, king_safety[anaguma][kRook  ][dir][piece][a][d]);
    }

    return op;
  }

  /**
   * 飛び駒の利きパラメータについて、次元下げを行います.
   */
  template<Color kColor, PieceType kPt, typename Operation>
  Operation EachSliderControl(Color king_color, Square king_square, Square from,
                              Square to, Operation op) {
    static_assert(kPt == kLance || kPt == kBishop || kPt == kRook, "");

    // 後手番の場合は、符号を反転させる
    const PackedWeight sign = GetPackedSign2x2(kColor);

    Bitboard controls = between_bb(from, to) | square_bb(to);
    controls.ForEach([&](Square s) {
      if (kPt == kRook) {
        op.apply(sign, rook_control[king_color][RelativeSquare(king_square, s)]);
      } else if (kPt == kBishop) {
        op.apply(sign, bishop_control[king_color][RelativeSquare(king_square, s)]);
      } else if (kPt == kLance) {
        op.apply(sign, lance_control[king_color][RelativeSquare(king_square, s)]);
      }
    });

    return op;
  }

  /**
   * 飛び利きが当たっている駒のパラメータについて、次元下げを行います.
   */
  template<Color kColor, PieceType kPt, typename Operation>
  Operation EachThreat(Square king_square, Square square, Piece threatened,
                       Operation op) {
    static_assert(kPt == kLance || kPt == kBishop || kPt == kRook, "");

    const PackedWeight sign = GetPackedSign2x2(kColor);

    RelativeSquare rs(king_square, square);
    if (kPt == kRook) {
      op.apply(sign, rook_threatened_piece[threatened]);
      op.apply(sign, rook_threat[rs][threatened]);
    } else if (kPt == kBishop) {
      op.apply(sign, bishop_threatened_piece[threatened]);
      op.apply(sign, bishop_threat[rs][threatened]);
    } else if (kPt == kLance) {
      op.apply(sign, lance_threatened_piece[threatened]);
      op.apply(sign, lance_threat[rs][threatened]);
    }

    return op;
  }

  // 駒割り
  ArrayMap<PackedWeight, PieceType> material;

  // 玉と玉以外の駒の位置関係
  ArrayMap<PackedWeight, Piece, RelativePsq> relative_kp;
  ArrayMap<PackedWeight, Square, PsqIndex> absolute_kp;

  // ２駒の位置関係（玉を除く）
  ArrayMap<PackedWeight, PsqIndex> hand_value;
  ArrayMap<PackedWeight, PieceType, Piece, RelativeSquare> relative_pp;
  ArrayMap<PackedWeight, PieceType, Piece, RelativeSquare, Rank> relative_ppy;
  ArrayMap<PackedWeight, PsqIndex, PsqIndex> absolute_pp;

  // 各マスの利き
  // a. 各マスの利き数 x そのマスの駒（絶対座標）　[玉のマス][マス][駒][味方の利き数][敵の利き数]
  ArrayMap<PackedWeight, Square, Square, Piece, Number<0, 3>, Number<0, 3>> psqc_absolute;
  // b. 各マスの利き数 x そのマスの駒（相対座標）　[玉との相対位置][駒][味方の利き数][敵の利き数]
  ArrayMap<PackedWeight, RelativeSquare, Piece, Number<0, 3>, Number<0, 3>> psqc_relative;
  // c. 浮き駒・質駒の評価（絶対座標）　[玉のマス][マス][味方利きの有無][敵利きの有無]
  ArrayMap<PackedWeight, Square, Square, Piece, bool, bool> floating_or_hostage_absolute;
  // d. 浮き駒・質駒の評価（相対座標）　[玉との相対位置][駒][味方利きの有無][敵利きの有無]
  ArrayMap<PackedWeight, RelativeSquare, Piece, bool, bool> floating_or_hostage_relative;
  // e. 味方の利き数（絶対座標）　[玉のマス][マス][味方の利き数]
  ArrayMap<PackedWeight, Square, Square, Number<0, 3>> own_controls_absolute;
  // f. 味方の利き数（相対座標）　[玉との相対位置][味方の利き数]
  ArrayMap<PackedWeight, RelativeSquare, Number<0, 3>> own_controls_relative;
  // g. 敵の利き数（絶対座標）　[玉のマス][マス][敵の利き数]
  ArrayMap<PackedWeight, Square, Square, Number<0, 3>> opp_controls_absolute;
  // h. 敵の利き数（相対座標）　[玉との相対位置][敵の利き数]
  ArrayMap<PackedWeight, RelativeSquare, Number<0, 3>> opp_controls_relative;
  // i. 利き数の勝ち負け　[玉のマス][マス][sign(味方の利き数 - 敵の利き数)]
  ArrayMap<PackedWeight, Square, Square, Number<-1, 1>> diff_controls_absolute;
  // j. 敵・味方の利き数の組み合わせ　[玉との相対位置][味方の利き数][敵の利き数]
  ArrayMap<PackedWeight, RelativeSquare, Number<0, 3>, Number<0, 3>> controls_relative;
  // k. 駒取りの脅威の評価　[駒の種類][味方の利きの数][敵の利き数]
  ArrayMap<PackedWeight, PieceType, Number<0, 3>, Number<0, 3>> capture_threat;

  // 玉の安全度
  // a. 玉が将棋盤の端にいる
  // [穴熊か否か][方向]
  ArrayMap<PackedWeight, bool, Direction> king_on_the_edge;
  // b. 玉の弱点（玉以外の利きがない空間）かどうか
  // [穴熊か否か][方向][空きマスか否か][受け方の利きの有無]
  ArrayMap<PackedWeight, bool, Direction, bool, bool> weak_points_of_king;
  // c. 攻め方の利き数
  // [穴熊か否か][方向][攻め方の利き数]
  ArrayMap<Array<PackedWeight, 4>, bool, Direction> attacker_controls;
  // d. 利き数の差
  // [穴熊か否か][方向][攻め方の利き数 - 受け方の利き数]
  ArrayMap<PackedWeight, bool, Direction, Number<-3, 3>> diff_controls;
  // e. 利きの勝ち負け（そのマスにどちらかの駒があるか、空白かで場合分け）
  // [穴熊か否か][方向][攻め方の駒の有無][受け方の駒の有無][利きの勝ち・負け・引き分け]
  ArrayMap<PackedWeight, bool, Direction, bool, bool, Number<-1, 1>> controls_advantage;
  // f. 周囲の駒の評価（質駒や、浮き駒を評価する）
  // [穴熊か否か][方向][盤上の駒][攻め方の利き数][受け方の利き数]
  ArrayMap<Array<PackedWeight, 4, 4>, bool, Direction, Piece> neighborhood_pieces;
  // g. 駒打ちによる王手の脅威
  // [穴熊か否か][攻め方の持ち駒の種類][方向][空きマスか否か][攻め方の利きの有無][受け方の利きの有無]
  ArrayMap<PackedWeight, bool, PieceType, Direction, bool, bool, bool> drop_check_threat;
  // h. 持ち駒についてだけ、次元下げしたもの
  // [穴熊か否か][攻め方の持ち駒の種類][方向][盤上にある駒][攻め方の利き数][受け方の利き数]
  ArrayMap<Array<PackedWeight, 4, 4>, bool, PieceType, Direction, Piece> king_safety;

  // 飛車・角・香車の利き
  ArrayMap<PackedWeight, Color, RelativeSquare> rook_control;
  ArrayMap<PackedWeight, Color, RelativeSquare> bishop_control;
  ArrayMap<PackedWeight, Color, RelativeSquare> lance_control;
  ArrayMap<PackedWeight, Piece> rook_threatened_piece;
  ArrayMap<PackedWeight, Piece> bishop_threatened_piece;
  ArrayMap<PackedWeight, Piece> lance_threatened_piece;
  ArrayMap<PackedWeight, RelativeSquare, Piece> rook_threat;
  ArrayMap<PackedWeight, RelativeSquare, Piece> bishop_threat;
  ArrayMap<PackedWeight, RelativeSquare, Piece> lance_threat;

  // 手番
  PackedWeight tempo;

 private:
  /**
   * 重みを、先後反転します（序盤・中盤・終盤と３つに分かれている場合）.
   *
   * 進行度計算用の重みについては、先後反転されないことに注意してください。
   */
  static PackedWeight FlipWeights3x1(PackedWeight s) {
    float opening     = -s[0]; // 序盤
    float middle_game = -s[1]; // 中盤
    float end_game    = -s[2]; // 終盤
    float progress    = s[3];  // 進行度計算用の重み
    return PackedWeight(opening, middle_game, end_game, progress);
  }

  /**
   * 重みを先後反転します（序盤・終盤*手番に分かれていて、2*2=4通りある場合）.
   *
   * 手番用の重みについては、先後反転されないことに注意してください。
   */
  static PackedWeight FlipWeights2x2(PackedWeight s) {
    float opening        = -s[0]; // 序盤
    float opening_tempo  = s[1];  // 手番（序盤用）
    float end_game       = -s[2]; // 終盤
    float end_game_tempo = s[3];  // 手番（終盤用）
    return PackedWeight(opening, opening_tempo, end_game, end_game_tempo);
  }

  /**
   * 手番によって、符号を反転させます（序盤・中盤・終盤と３つに分かれている場合）.
   */
  static PackedWeight GetPackedSign3x1(Color c) {
    return c == kBlack ? PackedWeight(1.0f) : FlipWeights3x1(PackedWeight(1.0f));
  }

  /**
   * 手番によって、符号を反転させます（序盤・終盤*手番に分かれていて、2*2=4通りある場合）.
   */
  static PackedWeight GetPackedSign2x2(Color c) {
    return c == kBlack ? PackedWeight(1.0f) : FlipWeights2x2(PackedWeight(1.0f));
  }
};

/**
 * 損失関数の勾配を集計するためのクラスです.
 */
struct Gradient {
 public:
  void Clear() {
    std::memset(this, 0, sizeof(*this));
  }

  PackedWeight* begin() {
    return reinterpret_cast<PackedWeight*>(this);
  }

  PackedWeight* end() {
    return reinterpret_cast<PackedWeight*>(this) + size();
  }

  PackedWeight& operator[](size_t i) {
    assert(i < size());
    return *(begin() + i);
  }

  size_t size() const {
    return sizeof(*this) / sizeof(PackedWeight);
  }

  void UpdateControl(const Position& pos, const PackedWeight& delta) {
    const Square bk = pos.king_square(kBlack);
    const Square wk = pos.king_square(kWhite);
    const ExtendedBoard& extended_board = pos.extended_board();

    PsqControlList list = extended_board.GetPsqControlList();

    for (const Square s : Square::all_squares()) {
      PsqControlIndex index = list[s];
      // 1. 先手玉との関係
      controls[kBlack][bk][index] += delta;
      // 2. 後手玉との関係
      // 注：KPとは異なり、インデックスの反転処理に時間がかかるため、インデックスと符号の反転処理は行わず、
      // 先手玉用・後手玉用の２つのテーブルを用意することで対応している。
      // その代わり、次元下げを行う段階で、インデックスと符号の反転処理を行っている。
      controls[kWhite][wk][index] += delta;
    }
  }

  template<Color kKingColor, bool kMirrorHorizontally>
  void UpdateKingSafety(const Position& pos, const PackedWeight& delta) {
    const Square ksq = pos.king_square(kKingColor);

    // 1. 相手の持ち駒のbit setを取得する
    HandSet hs = pos.hand(~kKingColor).GetHandSet();

    // 2. 穴熊かどうかを調べる
    Square rksq = ksq.relative_square(kKingColor);
    bool is_anaguma = (rksq == kSquare9I) || (rksq == kSquare1I);
    // Hack: 持ち駒のbit setの1ビット目は未使用なので、穴熊か否かのフラグをbit setの1ビット目に立てておく
    hs.set(kNoPieceType, is_anaguma);

    // 3. 玉の周囲8マスの利き数と駒を取得する
    const ExtendedBoard& eb = pos.extended_board();
    EightNeighborhoods attacks = eb.GetEightNeighborhoodControls(~kKingColor, ksq);
    EightNeighborhoods defenses = eb.GetEightNeighborhoodControls(kKingColor, ksq);
    EightNeighborhoods pieces = eb.GetEightNeighborhoodPieces(ksq);
    // 攻め方の利き数については、利き数の最大値を3に制限する
    attacks = attacks.LimitTo(3);
    // 受け方の利き数については、玉の利きを除外したうえで、最大値を3に制限する
    defenses = defenses.Subtract(1).LimitTo(3);

    // 4. 評価値テーブルを参照するためのラムダ関数を準備する
    auto update_gradient = [&](Direction dir) {
      // 後手玉の場合は、将棋盤を180度反転させて、先手番の玉として評価する
      Direction dir_i = kKingColor == kBlack ? dir : inverse_direction(dir);
      // 玉が右側にいる場合は、左右反転させて、将棋盤の左側にあるものとして評価する
      Direction dir_m = kMirrorHorizontally ? mirror_horizontally(dir_i) : dir_i;
      // 盤上にある駒を取得する
      Piece piece(pieces.at(dir_m));
      assert(piece.IsOk());
      // 後手玉を評価する場合は、周囲８マスの駒の先後を入れ替える
      if (kKingColor == kWhite && !piece.is(kNoPieceType)) {
        piece = piece.opponent_piece();
      }
      // 利き数を取得する
      int attackers = attacks.at(dir_m);
      int defenders = defenses.at(dir_m);
      // 勾配を更新する
      king_safety[hs][dir][piece][attackers][defenders] += delta;
    };

    // 5. 玉の周囲8マスについて、それぞれ勾配を更新する
    update_gradient(kDirNE);
    update_gradient(kDirE );
    update_gradient(kDirSE);
    update_gradient(kDirN );
    update_gradient(kDirS );
    update_gradient(kDirNW);
    update_gradient(kDirW );
    update_gradient(kDirSW);
  }

  template<Color kKingColor>
  void UpdateKingSafety(const Position& pos, const PackedWeight& delta) {
    // 玉が右側にいる場合は、左右反転させて、将棋盤の左側にあるものとして評価する
    // これにより、「玉の左か右か」という観点でなく、「盤の端か中央か」という観点での評価を行うことができる
    if (pos.king_square(kKingColor).relative_square(kKingColor).file() <= kFile4) {
      UpdateKingSafety<kKingColor, true>(pos, delta);
    } else {
      UpdateKingSafety<kKingColor, false>(pos, delta);
    }
  }

  template<Color kColor>
  void UpdateSlidingPieces(const Position& pos, const PackedWeight& delta) {
    Square own_ksq = pos.king_square(kColor);
    Square opp_ksq = pos.king_square(~kColor);
    if (kColor == kWhite) {
      own_ksq = Square::rotate180(own_ksq);
      opp_ksq = Square::rotate180(opp_ksq);
    }

    // 飛車の利きを評価
    pos.pieces(kColor, kRook, kDragon).ForEach([&](Square from) {
      Bitboard rook_target = pos.pieces() | ~rook_mask_bb(from);
      Bitboard attacks = rook_attacks_bb(from, pos.pieces()) & rook_target;
      assert(2 <= attacks.count() && attacks.count() <= 4);
      if (kColor == kWhite) {
        from = Square::rotate180(from);
      }
      while (attacks.any()) {
        Square to = attacks.pop_first_one();
        Piece threatened = pos.piece_on(to);
        if (kColor == kWhite) {
          to = Square::rotate180(to);
          if (threatened != kNoPiece) threatened = threatened.opponent_piece();
        }
        rook_control[kBlack][own_ksq][from][to] += delta;
        rook_control[kWhite][opp_ksq][from][to] += delta;
        rook_threat[opp_ksq][to][threatened] += delta;
      }
    });

    // 角の利きを評価
    Bitboard edge = file_bb(kFile1) | file_bb(kFile9) | rank_bb(kRank1) | rank_bb(kRank9);
    Bitboard bishop_target = pos.pieces() | edge;
    pos.pieces(kColor, kBishop, kHorse).ForEach([&](Square from) {
      Bitboard attacks = bishop_attacks_bb(from, pos.pieces()) & bishop_target;
      assert(1 <= attacks.count() && attacks.count() <= 4);
      if (kColor == kWhite) {
        from = Square::rotate180(from);
      }
      while (attacks.any()) {
        Square to = attacks.pop_first_one();
        Piece threatened = pos.piece_on(to);
        if (kColor == kWhite) {
          to = Square::rotate180(to);
          if (threatened != kNoPiece) threatened = threatened.opponent_piece();
        }
        bishop_control[kBlack][own_ksq][from][to] += delta;
        bishop_control[kWhite][opp_ksq][from][to] += delta;
        bishop_threat[opp_ksq][to][threatened] += delta;
      }
    });

    // 香車の利きを評価
    Bitboard lance_target = pos.pieces() | rank_bb(relative_rank(kColor, kRank1));
    pos.pieces(kColor, kLance).ForEach([&](Square from) {
      Bitboard attacks = lance_attacks_bb(from, pos.pieces(), kColor) & lance_target;
      if (attacks.any()) {
        assert(attacks.count() == 1);
        Square to = attacks.first_one();
        Piece threatened = pos.piece_on(to);
        if (kColor == kWhite) {
          from = Square::rotate180(from);
          to = Square::rotate180(to);
          if (threatened != kNoPiece) threatened = threatened.opponent_piece();
        }
        lance_control[kBlack][own_ksq][from][to] += delta;
        lance_control[kWhite][opp_ksq][from][to] += delta;
        lance_threat[opp_ksq][to][threatened] += delta;
      }
    });
  }

  /**
   * 勾配ベクトルをdelta分だけ更新します.
   */
  void Update(const Position& pos, const PsqList& list, const float delta,
              const float progress) {
    const Color stm = pos.side_to_move();
    const Square bk = pos.king_square(kBlack);
    const Square wk = Square::rotate180(pos.king_square(kWhite));

    PackedWeight coefficient3x1 = GetProgressCoefficient3x1(progress);
    PackedWeight coefficient2x2 = GetProgressCoefficient2x2(progress, stm);
    PackedWeight delta3x1 = coefficient3x1 * delta;
    PackedWeight delta2x2 = coefficient2x2 * delta;

    // 1. ２駒の関係
    for (const PsqPair* i = list.begin(); i != list.end(); ++i) {
      // a. KP（KPは、序盤・中盤・終盤と３通りに分かれている）
      king_piece[bk][i->black()] += delta3x1; // 加算
      king_piece[wk][i->white()] -= delta3x1; // 減算
      // b. PP（PPは、序盤・終盤 * 手番で、2*2=4通りに分かれている）
      for (const PsqPair* j = list.begin(); j <= i; ++j) {
        two_pieces[i->black()][j->black()] += delta2x2;
      }
    }

    // 2. 各マスの利き
    UpdateControl(pos, delta2x2);

    // 3. 玉の安全度
    UpdateKingSafety<kBlack>(pos, delta2x2);
    UpdateKingSafety<kWhite>(pos, FlipWeights2x2(delta2x2));

    // 4. 飛車・角の利き
    UpdateSlidingPieces<kBlack>(pos, delta2x2);
    UpdateSlidingPieces<kWhite>(pos, FlipWeights2x2(delta2x2));

    // 5. 手番
    // 注：手番は、EvalDetail::ComputeFinalScore()に合わせるため、0.5を掛ける。
    tempo += (stm == kBlack ? 0.5f : -0.5f) * delta3x1;
  }

  //
  // 1. ２駒の位置関係
  //
  /** 玉と玉以外の駒の位置関係 [玉の位置][玉以外の駒の種類及び位置] */
  ArrayMap<PackedWeight, Square, PsqIndex> king_piece;
  /** 玉を除く２駒の位置関係 [駒１の種類及び位置][駒２の種類及び位置] */
  ArrayMap<PackedWeight, PsqIndex, PsqIndex> two_pieces;

  //
  // 2. 各マスごとの利き
  //
  /** 各マスの利き [先手玉か後手玉か][玉の位置][マスの位置、駒の種類、先手の利き数、後手の利き数] */
  ArrayMap<PackedWeight, Color, Square, PsqControlIndex> controls;

  //
  // 3. 玉の安全度
  //
  /** 玉の安全度 [相手の持ち駒][玉から見た方向][そのマスにある駒][攻め方の利き数][受け方の利き数] */
  ArrayMap<Array<PackedWeight, 4, 4>, HandSet, Direction, Piece> king_safety;

  //
  // 4. 飛車・角・香車の利き
  //
  /** 飛車の利き [先手玉か後手玉か][玉の位置][飛車の位置][飛車の利きが届いている位置] */
  ArrayMap<PackedWeight, Color, Square, Square, Square> rook_control;
  /** 角の利き [先手玉か後手玉か][玉の位置][角の位置][角の利きが届いている位置] */
  ArrayMap<PackedWeight, Color, Square, Square, Square> bishop_control;
  /** 香車の利き [先手玉か後手玉か][玉の位置][香車の位置][香車の利きが届いている位置] */
  ArrayMap<PackedWeight, Color, Square, Square, Square> lance_control;
  /** 飛車の利きが付いている駒　[相手玉の位置][飛車が利きをつけている駒の位置][飛車が利きをつけている駒] */
  ArrayMap<PackedWeight, Square, Square, Piece> rook_threat;
  /** 角の利きが付いている駒　[相手玉の位置][角が利きをつけている駒の位置][角が利きをつけている駒] */
  ArrayMap<PackedWeight, Square, Square, Piece> bishop_threat;
  /** 香車の利きが付いている駒　[相手玉の位置][香車が利きをつけている駒の位置][香車が利きをつけている駒] */
  ArrayMap<PackedWeight, Square, Square, Piece> lance_threat;

  //
  // 5. 手番
  //
  /** 手番 */
  PackedWeight tempo;

 private:
  /**
   * 重みを、先後反転します（序盤・中盤・終盤と３つに分かれている場合）.
   *
   * 進行度計算用の重みについては、先後反転されないことに注意してください。
   */
  static PackedWeight FlipWeights3x1(PackedWeight s) {
    float opening     = -s[0]; // 序盤
    float middle_game = -s[1]; // 中盤
    float end_game    = -s[2]; // 終盤
    float progress    = s[3];  // 進行度計算用の重み
    return PackedWeight(opening, middle_game, end_game, progress);
  }

  /**
   * 重みを先後反転します（序盤・終盤*手番に分かれていて、2*2=4通りある場合）.
   *
   * 手番用の重みについては、先後反転されないことに注意してください。
   */
  static PackedWeight FlipWeights2x2(PackedWeight s) {
    float opening        = -s[0]; // 序盤
    float opening_tempo  = s[1];  // 手番（序盤用）
    float end_game       = -s[2]; // 終盤
    float end_game_tempo = s[3];  // 手番（終盤用）
    return PackedWeight(opening, opening_tempo, end_game, end_game_tempo);
  }

  /**
   * 進行度に応じて変化する係数を返します（重みが序盤・中盤・終盤と３つに分かれている場合）.
   */
  static PackedWeight GetProgressCoefficient3x1(float progress) {
    float opening, middle_game, end_game;
    if (progress < 0.5f) {
      opening     = -2.0f * progress + 1.0f; // 序盤
      middle_game = +2.0f * progress;        // 中盤
      end_game    =  0.0f;                   // 終盤
    } else {
      opening     =  0.0f;                   // 序盤
      middle_game = -2.0f * progress + 2.0f; // 中盤
      end_game    = +2.0f * progress - 1.0f; // 終盤
    }
    // 注：４番目がゼロになっているのは、進行度を保存するために空けておく必要があるため
    return PackedWeight(opening, middle_game, end_game, 0.0f);
  }

  /**
   * 進行度に応じて変化する係数を返します（重みが序盤・終盤*手番に分かれていて、2*2=4通りある場合）.
   */
  static PackedWeight GetProgressCoefficient2x2(float progress,
                                                Color side_to_move) {
    float opening  = 1.0f - progress; // 序盤
    float end_game = progress;        // 終盤
    float tempo = side_to_move == kBlack ? 0.1f : -0.1f;
    return PackedWeight(opening, opening * tempo, end_game, end_game * tempo);
  }
};

#endif /* LEARNING_H_ */
