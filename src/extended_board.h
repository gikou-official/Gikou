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

#ifndef EXTENDED_BOARD_H_
#define EXTENDED_BOARD_H_

#include <cstring>
#include <tmmintrin.h> // SSSE 3 (Supplementary Streaming SIMD Extensions 3)
#include "common/number.h"
#include "bitboard.h"
#include "move.h"
#include "psq.h"

class Position;

/**
 * ８近傍の利き数や、８近傍の駒をまとめて扱うためのクラスです.
 *
 * 内部実装にSIMD演算を用いているため、８マス分の計算をいっぺんに行うことができます。
 * 格納するデータは、値型であれば何でも良いので、「８近傍の利き数」でも「８近傍の駒」でも、
 * 同一のクラスで扱うことができます。
 *
 * 内部データの何バイト目がどの方向に対応するかは、以下のとおりです。
 * <pre>
 * 5  3  0
 * 6     1
 * 7  4  2
 * </pre>
 *
 * @see Direction
 */
class EightNeighborhoods {
 public:
  /**
   * デフォルトコンストラクタでは、ゼロ初期化が行われます.
   */
  explicit EightNeighborhoods(__m128i xmm = _mm_setzero_si128())
      : xmm_(xmm) {
  }

  /**
   * 与えられた方向のマスに対応するビットを、すべて１にセットします.
   * @param d ８方向のどれか
   */
  void set(Direction d) {
    assert(0 <= d && d < 8);
    array_[d] = UINT8_MAX;
  }

  /**
   * 与えられた方向のマスに対応するビットを、すべて０にリセットします.
   * @param d ８方向のどれか
   */
  void reset(Direction d) {
    assert(0 <= d && d < 8);
    array_[d] = 0;
  }

  /**
   * 与えられた方向のマスに対応するデータを参照します.
   * @param d ８方向のどれか
   */
  uint8_t& at(Direction d) {
    assert(0 <= d && d < 8);
    return array_[d];
  }

  /**
   * 与えられた方向のマスに対応するデータを参照します.
   * @param d ８方向のどれか
   */
  const uint8_t& at(Direction d) const {
    assert(0 <= d && d < 8);
    return array_[d];
  }

  /**
   * nより大きい値が格納されている方向のビットセットを返します.
   * このメソッドを使うと、例えば、「利き数が１以上のマス」のビットセットを取得することができます。
   */
  DirectionSet more_than(uint8_t n) const {
    __m128i more_than_n = _mm_cmpgt_epi8(xmm_, _mm_set1_epi8(n));
    return DirectionSet(_mm_movemask_epi8(more_than_n));
  }

  /**
   * 各要素をn以下に限定したEightNeighborhoodsオブジェクトを返します.
   * このメソッドを使うと、各要素の値に上限を設けることができます。
   */
  EightNeighborhoods LimitTo(uint8_t n) const {
    return EightNeighborhoods(_mm_min_epu8(xmm_, _mm_set1_epi8(n)));
  }

  /**
   * 各要素からnを除したEgightNeighborhoodsオブジェクトを返します.
   */
  EightNeighborhoods Subtract(uint8_t n) const {
    return EightNeighborhoods(_mm_subs_epu8(xmm_, _mm_set1_epi8(n)));
  }

  /**
   * ８方向の値を画面に表示します（デバッグ用）.
   */
  void Print() {
    for (int i = 0; i < 8; ++i) {
      std::printf("[%d] %d\n", i, int(array_[i]));
    }
  }

 private:
  friend class ExtendedBoard;
  union {
    __m128i xmm_;
    uint8_t array_[16];
  };
};

/**
 * 15近傍（縦5マスx横3マス）の「利き数」や「駒」をまとめて扱うためのクラスです.
 *
 * 格納するデータは、値型であれば何でも良いので、「15近傍の利き数」でも「15近傍の駒」でも、
 * 同一のクラスで扱うことができます。
 *
 * 何バイト目がどのマスに対応するかは、以下の表のとおりです。
 * 例えば、中心のマスのインデックスは「7」、中心から見て左上のマスのインデックスは「11」です。
 * <pre>
 *  10  5  0
 *  11  6  1
 *  12  7  2
 *  13  8  3
 *  14  9  4
 * </pre>
 */
class FifteenNeighborhoods {
 public:
  /**
   * デフォルトコンストラクタでは、ゼロ初期化が行われます.
   */
  explicit FifteenNeighborhoods(__m128i xmm = _mm_setzero_si128())
      : xmm_(xmm) {
  }

  /**
   * 中心のマスからx軸方向にdelta_x、y軸方向にdelta_y移動した方向にあるマスのデータを取得します.
   */
  uint8_t get(int delta_x, int delta_y) const {
    assert(-1 <= delta_x && delta_x <= 1);
    assert(-2 <= delta_y && delta_y <= 2);
    int index = (delta_x + 1) * 5 + (delta_y + 2);
    return array_[index];
  }

  /**
   * 中心のマスからx軸方向にdelta_x、y軸方向にdelta_y移動した方向にあるマスのビットをセットします.
   */
  void set(int delta_x, int delta_y) {
    assert(-1 <= delta_x && delta_x <= 1);
    assert(-2 <= delta_y && delta_y <= 2);
    int index = (delta_x + 1) * 5 + (delta_y + 2);
    array_[index] = UINT8_MAX;
  }

  /**
   * 中心のマスからx軸方向にdelta_x、y軸方向にdelta_y移動した方向にあるマスのビットをリセットします.
   */
  void reset(int delta_x, int delta_y) {
    assert(-1 <= delta_x && delta_x <= 1);
    assert(-2 <= delta_y && delta_y <= 2);
    int index = (delta_x + 1) * 5 + (delta_y + 2);
    array_[index] = 0;
  }

  /**
   * nバイト目にあるデータを参照します.
   */
  uint8_t& at(size_t i) {
    assert(i < size());
    return array_[i];
  }

  /**
   * nバイト目にあるデータを取得します.
   */
  const uint8_t& at(size_t i) const {
    assert(i < size());;
    return array_[i];
  }

  /**
   * 要素数を取得します.
   * @return 常に15（周囲15マス分のデータを格納しているため）
   */
  size_t size() const {
    return 15;
  }

  /**
   * 各要素をn以下に限定したFifteenNeighborhoodsオブジェクトを返します.
   * このメソッドを使うと、各要素の値に上限を設けることができます。
   */
  FifteenNeighborhoods LimitTo(uint8_t n) const {
    return FifteenNeighborhoods(_mm_min_epu8(xmm_, _mm_set1_epi8(n)));
  }

  /**
   * 各要素からnを除したEgightNeighborhoodsオブジェクトを返します.
   */
  FifteenNeighborhoods Subtract(uint8_t n) const {
    return FifteenNeighborhoods(_mm_subs_epu8(xmm_, _mm_set1_epi8(n)));
  }

  /**
   * 15近傍分のデータを、180度回転します.
   *
   * 具体的には、各マスのデータは、以下のように並べ替えられます。
   * <pre>
   *   (before)          (after)
   *  10   5   0        4   9  14
   *  11   6   1        3   8  13
   *  12   7   2   =>   2   7  12
   *  13   8   3        1   6  11
   *  14   9   4        0   5  10
   * </pre>
   */
  FifteenNeighborhoods Rotate180() const {
    constexpr uint8_t r = 0x80;
    const __m128i kMask = _mm_set_epi8(r, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    return FifteenNeighborhoods(_mm_shuffle_epi8(xmm_, kMask));
  }

  /**
   * 各マスにある駒の先後を入れ替えます.
   * なお、空きマスについては、このメソッドを適用しても、空きマスのままです。
   */
  FifteenNeighborhoods FlipPieceColors() const {
    static_assert(int(kNoPiece) == 0, "Assume the value of an empty square is zero.");
    static_assert((int(kBlackPawn) ^ int(kWhitePawn)) == 16, "Assume the value of the color flag is 16.");
    // 空きマスor壁を特定する
    __m128i empty_squares = _mm_cmpeq_epi8(xmm_, _mm_setzero_si128());
    __m128i walls = _mm_cmpeq_epi8(xmm_, _mm_set1_epi8(int(kWall)));
    __m128i empties_or_walls = _mm_or_si128(empty_squares, walls);
    // 空きマスor壁以外のマスのみ、駒の手番を反転する
    const __m128i kColorFlags = _mm_set1_epi8(16);
    __m128i flags = _mm_andnot_si128(empties_or_walls, kColorFlags);
    __m128i flipped_pieces = _mm_xor_si128(xmm_, flags);
    return FifteenNeighborhoods(flipped_pieces);
  }

 private:
  friend class ExtendedBoard;
  union {
    __m128i xmm_;
    uint8_t array_[16];
  };
};

/**
 * 将棋盤の機能を拡張し、各マスの利き数等を高速に計算できるようにするためのクラスです.
 */
class ExtendedBoard {
 public:
  ExtendedBoard() {
    Clear(); // 将棋盤・利き数を初期化する
  }

  /**
   * 将棋盤を空にします.
   *
   * 具体的には、
   *   - 将棋盤の各マスは、すべて空きマスとなります
   *   - 各マスの利きは、すべてゼロになります
   */
  void Clear();

  /**
   * 将棋盤の特定のマスに駒を置きます.
   * このメソッドを呼ぶと、ExtendedBoard内部の利き数も更新されます。
   * @param piece  将棋盤に置きたい駒
   * @param square コマを置くマス
   */
  void PutPiece(Piece piece, Square square);

  /**
   * 将棋盤の特定のマスから駒を取り除きます.
   * このメソッドを呼ぶと、ExtendedBoard内部の利き数も更新されます。
   * @param square 駒を取り除きたいマス
   */
  void RemovePiece(Square square);

  /**
   * 指し手に沿って将棋盤を動かします（打つ手の場合）.
   */
  void MakeDropMove(Move move) {
    assert(move.is_drop());
    PutPiece(move.piece(), move.to());
  }

  /**
   * 指し手に沿って将棋盤を動かします（駒を取る手の場合）.
   */
  void MakeCaptureMove(Move);

  /**
   * 指し手に沿って将棋盤を動かします（駒を取らずに動かす手の場合）.
   */
  void MakeNonCaptureMove(Move);

  /**
   * 指し手に沿って将棋盤を２手分いっぺんに動かします.
   *
   * 具体的には、
   *   1. 駒を打つ手
   *   2. 玉でその駒を取り返す手
   * の２手分を１つのメソッドで動かします。
   *
   * このメソッドを使うと、MakeDropMove()とMakeNonCaptureMove()を１回ずつ使うよりも、
   * 高速に処理することができます。
   *
   * @param king_from  玉の移動元のマス
   * @param king_to    玉の移動先のマス（かつ、駒を打たれたマスでもあります）
   * @param king_color 玉を動かしたプレイヤーの手番
   */
  void MakeDropAndKingRecapture(Square king_from, Square king_to, Color king_color);

  /**
   * 与えられた局面の駒を、ExtendedBoardにもすべてセットします.
   * @params pos 局面
   */
  void SetAllPieces(const Position& pos);

  /**
   * 特定のマスの上にある駒を返します.
   */
  Piece piece_on(Square square) const {
    return Piece(board_[square]);
  }

  /**
   * 特定のマスの上に、駒をセットします.
   */
  void set_piece_on(Square square, Piece piece) {
    board_[square] = static_cast<uint8_t>(piece);
  }

  /**
   * 特定のマスにおける、利き数を返します.
   *
   * 例えば、２二のマスにある、先手番の利き数を取得したい場合は、次のようにします。
   * @code
   * int n = this->num_controls(kBlack, kSquare2B);
   * @endcode
   *
   * @param color  先手、後手どちらの利き数を取得するか
   * @param square 利き数を取得したいマス
   * @param 利き数
   */
  int num_controls(Color color, Square square) const {
    return controls_[color][square].u8.number;
  }

  /**
   * 特定のマスにある、長い利きの方向を返します.
   * @param color  先手、後手どちらの長い利きを取得するか
   * @param square 長い利きを取得したいマス
   * @return 長い利きの方向のビットセット
   */
  DirectionSet long_controls(Color color, Square square) const {
    return DirectionSet(controls_[color][square].u8.direction);
  }

  /**
   * ８近傍の利き数を取得します.
   * @param color  先手、後手どちらの利き数を取得するか
   * @param square ８近傍の中心となるマス
   * @return ８近傍の各マスの利き数
   */
  EightNeighborhoods GetEightNeighborhoodControls(Color color, Square square) const {
    // 1. 利き数を取得するマスのアドレスを求める
    const uint16_t* u16_ptr = &controls_[color][square].u16;
    const __m128i* ptr0 = reinterpret_cast<const __m128i*>(u16_ptr + AttackPattern::kOffset0);
    const __m128i* ptr1 = reinterpret_cast<const __m128i*>(u16_ptr + AttackPattern::kOffset1);
    const __m128i* ptr2 = reinterpret_cast<const __m128i*>(u16_ptr + AttackPattern::kOffset2);
    // 2. 主記憶からレジスタに読み込む
    //（注：ここでのポインタは16バイト境界にアライメントされていないため、MOVDQU命令を使う）
    __m128i xmm0 = _mm_loadu_si128(ptr0);
    __m128i xmm1 = _mm_loadu_si128(ptr1);
    __m128i xmm2 = _mm_loadu_si128(ptr2);
    // 3. シャッフル命令を使って、8近傍の利き数を取得する
    constexpr uint8_t r = 0x80;
    __m128i mask0 = _mm_set_epi8(r, r, r, r, r, r, r, r, r, r, r, r, r, 6, 4, 2);
    __m128i mask1 = _mm_set_epi8(r, r, r, r, r, r, r, r, r, r, r, 6, 2, r, r, r);
    __m128i mask2 = _mm_set_epi8(r, r, r, r, r, r, r, r, 6, 4, 2, r, r, r, r, r);
    __m128i sum = _mm_shuffle_epi8(xmm0, mask0);
    sum = _mm_or_si128(sum, _mm_shuffle_epi8(xmm1, mask1));
    sum = _mm_or_si128(sum, _mm_shuffle_epi8(xmm2, mask2));
    // 4. 端の補正を行う
    return EightNeighborhoods(_mm_and_si128(sum, edge_correction8_[square].xmm_));
  }

  /**
   * ８近傍にある駒を取得します.
   * @param square ８近傍の中心となるマス
   * @return ８近傍の各マスにある駒
   */
  EightNeighborhoods GetEightNeighborhoodPieces(Square square) const {
    // 1. アドレスを求める
    const uint8_t* u8_ptr = &board_[square];
    const __m128i* ptr = reinterpret_cast<const __m128i*>(u8_ptr + AttackPattern::kOffset0);
    // 2. 主記憶からレジスタに読み込む
    //（注：ここでのポインタは16バイト境界にアライメントされていないため、MOVDQU命令を使う）
    __m128i xmm0 = _mm_loadu_si128(ptr + 0);
    __m128i xmm1 = _mm_loadu_si128(ptr + 1);
    // 3. シャッフル命令を使って、８近傍の駒を取得する
    //    3 10  1
    //    4     2
    //    5 12  3
    constexpr uint8_t r = 0x80;
    __m128i mask0 = _mm_set_epi8(r, r, r, r, r, r, r, r, r, r, r,12,10, 3, 2, 1);
    __m128i mask1 = _mm_set_epi8(r, r, r, r, r, r, r, r, 5, 4, 3, r, r, r, r, r);
    __m128i sum = _mm_shuffle_epi8(xmm0, mask0);
    sum = _mm_or_si128(sum, _mm_shuffle_epi8(xmm1, mask1));
    // 4. 端の補正を行う
    __m128i inner = edge_correction8_[square].xmm_; // 盤の内側のみビットが立ったマスク
    __m128i inside_the_board = _mm_and_si128(sum, inner);
    __m128i outside_the_board = _mm_andnot_si128(inner, _mm_set1_epi8(kWall));
    return EightNeighborhoods(_mm_or_si128(inside_the_board, outside_the_board));
  }

  /**
   * 15近傍（縦5マスx横3マス）の利き数を取得します.
   * @param color  先手、後手どちらの利き数を取得するか
   * @param square 15近傍の中心となるマス
   * @return 15近傍の各マスの利き数
   */
  FifteenNeighborhoods GetFifteenNeighborhoodControls(Color color, Square square) const {
    // 1. 利き数を取得するマスのアドレスを求める
    const uint16_t* u16_ptr = &controls_[color][square].u16;
    const __m128i* ptr0 = reinterpret_cast<const __m128i*>(u16_ptr + AttackPattern::kOffset0);
    const __m128i* ptr1 = reinterpret_cast<const __m128i*>(u16_ptr + AttackPattern::kOffset1);
    const __m128i* ptr2 = reinterpret_cast<const __m128i*>(u16_ptr + AttackPattern::kOffset2);
    // 2. 主記憶からレジスタに読み込む
    //（注：ここでのポインタは16バイト境界にアライメントされていないため、MOVDQU命令を使う）
    __m128i xmm0 = _mm_loadu_si128(ptr0);
    __m128i xmm1 = _mm_loadu_si128(ptr1);
    __m128i xmm2 = _mm_loadu_si128(ptr2);
    // 3. シャッフル命令を使って、15近傍の利き数を取得する
    //  0  0  0
    //  2  2  2
    //  4  4  4
    //  6  6  6
    //  8  8  8
    constexpr uint8_t r = 0x80;
    __m128i mask0 = _mm_set_epi8(r, r, r, r, r, r, r, r, r, r, r, 8, 6, 4, 2, 0);
    __m128i mask1 = _mm_set_epi8(r, r, r, r, r, r, 8, 6, 4, 2, 0, r, r, r, r, r);
    __m128i mask2 = _mm_set_epi8(r, 8, 6, 4, 2, 0, r, r, r, r, r, r, r, r, r, r);
    __m128i sum = _mm_shuffle_epi8(xmm0, mask0);
    sum = _mm_or_si128(sum, _mm_shuffle_epi8(xmm1, mask1));
    sum = _mm_or_si128(sum, _mm_shuffle_epi8(xmm2, mask2));
    // 4. 端の補正を行う
    return FifteenNeighborhoods(_mm_and_si128(sum, edge_correction15_[square].xmm_));
  }

  /**
   * 15近傍（縦5マスx横3マス）にある駒を取得します.
   * @param square 15近傍の中心となるマス
   * @return 15近傍の各マスにある駒
   */
  FifteenNeighborhoods GetFifteenNeighborhoodPieces(Square square) const {
    // 1. アドレスを求める
    const uint8_t* u8_ptr = &board_[square];
    const __m128i* ptr = reinterpret_cast<const __m128i*>(u8_ptr + AttackPattern::kOffset0);
    // 2. 主記憶からレジスタに読み込む
    //（注：ここでのポインタは16バイト境界にアライメントされていないため、MOVDQU命令を使う）
    __m128i xmm0 = _mm_loadu_si128(ptr + 0);
    __m128i xmm1 = _mm_loadu_si128(ptr + 1);
    // 3. シャッフル命令を使って、15近傍の駒を取得する
    //    2  9  0
    //    3 10  1
    //    4 11  2
    //    5 12  3
    //    6 13  4
    constexpr uint8_t r = 0x80;
    __m128i mask0 = _mm_set_epi8(r, r, r, r, r, r,13,12,11,10, 9, 4, 3, 2, 1, 0);
    __m128i mask1 = _mm_set_epi8(r, 6, 5, 4, 3, 2, r, r, r, r, r, r, r, r, r, r);
    __m128i sum = _mm_shuffle_epi8(xmm0, mask0);
    sum = _mm_or_si128(sum, _mm_shuffle_epi8(xmm1, mask1));
    // 4. 端の補正を行う
    __m128i inner = edge_correction15_[square].xmm_; // 盤の内側のみビットが立ったマスク
    __m128i inside_the_board = _mm_and_si128(sum, inner);
    __m128i outside_the_board = _mm_andnot_si128(inner, _mm_set1_epi8(kWall));
    return FifteenNeighborhoods(_mm_or_si128(inside_the_board, outside_the_board));
  }

  /**
   * 指定された手番側の利きがついているマスをビットボードで取得します.
   */
  Bitboard GetControlledSquares(Color color) const;

  /**
   * PsqControlList（PsqControlIndexのリスト）を作成します.
   *
   * このメソッドにより生成されたPsqControlListは、評価関数において、評価値テーブル参照を参照する
   * 際に用いられます。
   */
  PsqControlList GetPsqControlList() const;

  /**
   * クラス内部のテーブルを初期化する処理を行います.
   */
  static void Init();

  /**
   * このクラスの内部状態が正しい場合はtrueを、正しくなければfalseを返します（デバッグ用）.
   */
  bool IsOk() const;

  /**
   * このクラスの状態（利き数等）を標準出力にプリントします（デバッグ用）.
   */
  void Print() const;

 private:

  void AddControls(Piece piece, Square square) {
    OperateShortControls(piece, square, [](__m128i lhs, __m128i rhs) {
      return _mm_add_epi8(lhs, rhs);
    });
    OperateLongControls(piece.color(), square, long_attack_patterns_[piece],
                        [](uint16_t& lhs, uint16_t rhs) { lhs += rhs; });
  }

  void RemoveControls(Piece piece, Square square) {
    OperateShortControls(piece, square, [](__m128i lhs, __m128i rhs) {
      return _mm_sub_epi8(lhs, rhs);
    });
    OperateLongControls(piece.color(), square, long_attack_patterns_[piece],
                        [](uint16_t& lhs, uint16_t rhs) { lhs -= rhs; });
  }

  void ExtendLongControls(Square square) {
    OperateLongControls(kBlack, square,
                        DirectionSet(controls_[kBlack][square].u8.direction),
                        [](uint16_t& lhs, uint16_t rhs) { lhs += rhs; });
    OperateLongControls(kWhite, square,
                        DirectionSet(controls_[kWhite][square].u8.direction),
                        [](uint16_t& lhs, uint16_t rhs) { lhs += rhs; });
  }

  void CutLongControls(Square square) {
    OperateLongControls(kBlack, square,
                        DirectionSet(controls_[kBlack][square].u8.direction),
                        [](uint16_t& lhs, uint16_t rhs) { lhs -= rhs; });
    OperateLongControls(kWhite, square,
                        DirectionSet(controls_[kWhite][square].u8.direction),
                        [](uint16_t& lhs, uint16_t rhs) { lhs -= rhs; });
  }

  /**
   * 短い利きを操作するためのメソッドです.
   */
  template<typename Operation>
  void OperateShortControls(Piece piece, Square square, Operation op) {
    // 1. 利き数を操作するマスのアドレスを求める
    uint16_t* u16_ptr = &controls_[piece.color()][square].u16;
    __m128i* ptr0 = reinterpret_cast<__m128i*>(u16_ptr + AttackPattern::kOffset0);
    __m128i* ptr1 = reinterpret_cast<__m128i*>(u16_ptr + AttackPattern::kOffset1);
    __m128i* ptr2 = reinterpret_cast<__m128i*>(u16_ptr + AttackPattern::kOffset2);
    // 2. 利き数をレジスタに読み込む
    //（注：ここでのポインタは16バイト境界にアライメントされていないため、MOVDQU命令を使う）
    __m128i xmm0 = _mm_loadu_si128(ptr0);
    __m128i xmm1 = _mm_loadu_si128(ptr1);
    __m128i xmm2 = _mm_loadu_si128(ptr2);
    // 3. 駒の利きをテーブルから取得する
    //（注：駒が将棋盤の端にある場合に、利きがはみ出さないように補正を行うためのマスクも取得する）
    AttackPattern attacks = short_attack_patterns_[piece]; // 利きのパターン
    AttackPattern mask = attack_correction_[square]; // 端補正用のマスク
    // 4. 利き数を加算 or 減算する（どちらを行うかは、opで指定する）
    xmm0 = op(xmm0, _mm_and_si128(attacks.xmm[0], mask.xmm[0]));
    xmm1 = op(xmm1, _mm_and_si128(attacks.xmm[1], mask.xmm[1]));
    xmm2 = op(xmm2, _mm_and_si128(attacks.xmm[2], mask.xmm[2]));
    // 5. レジスタから主記憶に書き戻す
    //（注：ここでのポインタは16バイト境界にアライメントされていないため、MOVDQU命令を使う）
    _mm_storeu_si128(ptr0, xmm0);
    _mm_storeu_si128(ptr1, xmm1);
    _mm_storeu_si128(ptr2, xmm2);
  }

  /**
   * 長い利きを操作するためのメソッドです.
   */
  template<typename Operation>
  void OperateLongControls(Color color, Square square,
                           DirectionSet directions, Operation op) {
    for (Direction dir : directions) {
      Square delta = Square::direction_to_delta(dir);
      Control increment = direction_to_increment_[dir];
      Square to = square;
      for (int i = max_number_of_long_attacks_[square][dir]; i > 0; --i) {
        // 次のマスに移動する
        to += delta;
        // 長い利きを追加 or 削除する
        op(controls_[color][to].u16, increment.u16);
        // 空白でないマスに到達したら、ループを抜ける
        if (board_[to] != kNoPiece) {
          break;
        }
      }
    }
  }

  /**
   * 各マスにおける、(1)利き数と、(2)長い利きの方向を保持するためのクラスです.
   */
  struct Control {
    union {
      // byte (8 bit) 単位での読み書き
      struct {
        uint8_t number; // 利きの数
        uint8_t direction; // 長い利きの方向
      } u8;

      // word (16 bit) 単位での読み書き（処理の高速化のために必要です）
      uint16_t u16;
    };
  };

  /**
   * 全81マスの利き数を保持するためのクラスです.
   */
  class ControlBoard {
   public:
    UNROLL_LOOPS ControlBoard& operator=(const ControlBoard& rhs) {
      for (int i = 0; i < 11; ++i) {
        xmm_[i] = rhs.xmm_[i];
      }
      return *this;
    }
    Control& operator[](Square s) {
      return controls_[s];
    }
    const Control& operator[](Square s) const {
      return controls_[s];
    }
    __m128i& xmm(size_t i) {
      assert(i < 11);
      return xmm_[i];
    }
    const __m128i& xmm(size_t i) const {
      assert(i < 11);
      return xmm_[i];
    }
   private:
    __m128i padding_head_[2];
    union {
      Control controls_[88];
      __m128i xmm_[11];
    };
    __m128i padding_tail_[2];
  };

  /**
   * 全81マスの盤上の駒を保持するためのクラスです.
   */
  struct PieceBoard {
   public:
    UNROLL_LOOPS PieceBoard& operator=(const PieceBoard& rhs) {
      for (int i = 0; i < 6; ++i) {
        xmm_[i] = rhs.xmm_[i];
      }
      return *this;
    }
    uint8_t& operator[](Square s) {
      return pieces_[s];
    }
    const uint8_t& operator[](Square s) const {
      return pieces_[s];
    }
    __m64& mm(size_t i) {
      assert(i < 12);
      return mm_[i];
    }
    const __m64& mm(size_t i) const {
      assert(i < 12);
      return mm_[i];
    }
   private:
    __m128i padding_head_;
    union {
      uint8_t pieces_[96];
      __m64 mm_[12];
      __m128i xmm_[6];
    };
    __m128i padding_tail_;
  };

  /**
   * 各駒の（短い）利きのパターンを表します.
   *
   * 短い利きのパターンは、各駒の周囲15マス（縦5マス * 横3マス）のパターンです。
   * 内部的には、XMMレジスタ（128 bit）1個で縦1列を表しています。そして、それが3列あるので、
   * 全体としてはXMMレジスタ3個分で表されています。
   */
  struct AttackPattern {
    static constexpr int kOffset0 = kDeltaN + kDeltaNE;
    static constexpr int kOffset1 = kDeltaN + kDeltaN;
    static constexpr int kOffset2 = kDeltaN + kDeltaNW;
    AttackPattern() {
      xmm[0] = _mm_setzero_si128();
      xmm[1] = _mm_setzero_si128();
      xmm[2] = _mm_setzero_si128();
    }
    Control& at(Square delta) {
      if      (delta == kDeltaNNE) return controls[0][0];
      else if (delta == kDeltaNE ) return controls[0][1];
      else if (delta == kDeltaE  ) return controls[0][2];
      else if (delta == kDeltaSE ) return controls[0][3];
      else if (delta == kDeltaSSE) return controls[0][4];
      else if (delta == kDeltaN  ) return controls[1][1];
      else if (delta == kDeltaS  ) return controls[1][3];
      else if (delta == kDeltaNNW) return controls[2][0];
      else if (delta == kDeltaNW ) return controls[2][1];
      else if (delta == kDeltaW  ) return controls[2][2];
      else if (delta == kDeltaSW ) return controls[2][3];
      else    /* kDeltaSSW      */ return controls[2][4];
    }
    union {
      __m128i xmm[3];
      Control controls[3][8];
    };
  };

  /** 将棋盤の「端」の利きを補正するためのマスク */
  static ArrayMap<AttackPattern, Square> attack_correction_;

  /** 各駒ごとの、短い利きのパターン */
  static ArrayMap<AttackPattern, Piece> short_attack_patterns_;

  /** 各駒ごとの、長い利きのパターン */
  static const ArrayMap<DirectionSet, Piece> long_attack_patterns_;

  /** 利きを一定方向に延長する際に用いる増分 */
  static ArrayMap<Control, Direction> direction_to_increment_;

  /** 特定のマスから、特定の方向に伸びる長い利きの、最大の長さ */
  static ArrayMap<uint8_t, Square, Direction> max_number_of_long_attacks_;

  /** 将棋盤の端の補正を行うためのマスク（８近傍用） */
  static ArrayMap<EightNeighborhoods, Square> edge_correction8_;

  /** 将棋盤の端の補正を行うためのマスク（15近傍用） */
  static ArrayMap<FifteenNeighborhoods, Square> edge_correction15_;

  /** 利き数 [手番][マスの位置] */
  ArrayMap<ControlBoard, Color> controls_;

  /** 置かれている駒 [マスの位置] */
  PieceBoard board_;
};

#endif /* EXTENDED_BOARD_H_ */
