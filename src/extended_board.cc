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

#include "extended_board.h"

#include <vector>
#include <utility>
#include <smmintrin.h> // SSE 4.1
#include "position.h"

ArrayMap<ExtendedBoard::AttackPattern, Square> ExtendedBoard::attack_correction_;
ArrayMap<ExtendedBoard::AttackPattern, Piece> ExtendedBoard::short_attack_patterns_;
ArrayMap<ExtendedBoard::Control, Direction> ExtendedBoard::direction_to_increment_;
ArrayMap<uint8_t, Square, Direction> ExtendedBoard::max_number_of_long_attacks_;
ArrayMap<EightNeighborhoods, Square> ExtendedBoard::edge_correction8_;
ArrayMap<FifteenNeighborhoods, Square> ExtendedBoard::edge_correction15_;

const ArrayMap<DirectionSet, Piece> ExtendedBoard::long_attack_patterns_ = {
    {kBlackLance , DirectionSet{kDirN}},
    {kBlackBishop, DirectionSet{kDirNW, kDirNE, kDirSW, kDirSE}},
    {kBlackRook  , DirectionSet{kDirN, kDirW, kDirE, kDirS}},
    {kBlackHorse , DirectionSet{kDirNW, kDirNE, kDirSW, kDirSE}},
    {kBlackDragon, DirectionSet{kDirN, kDirW, kDirE, kDirS}},
    {kWhiteLance , DirectionSet{kDirS}},
    {kWhiteBishop, DirectionSet{kDirNW, kDirNE, kDirSW, kDirSE}},
    {kWhiteRook  , DirectionSet{kDirN, kDirW, kDirE, kDirS}},
    {kWhiteHorse , DirectionSet{kDirNW, kDirNE, kDirSW, kDirSE}},
    {kWhiteDragon, DirectionSet{kDirN, kDirW, kDirE, kDirS}}
};

void ExtendedBoard::Clear() {
  // ゼロ初期化により、利き数は全てゼロになり、盤上の駒は全てkNoPieceになる
  std::memset(this, 0, sizeof *this);
}

void ExtendedBoard::PutPiece(Piece piece, Square square) {
  assert(piece != kNoPiece);
  board_[square] = piece;
  CutLongControls(square);
  AddControls(piece, square);
}

void ExtendedBoard::RemovePiece(Square square) {
  Piece removed_piece = Piece(board_[square]);
  board_[square] = kNoPiece;
  RemoveControls(removed_piece, square);
  ExtendLongControls(square);
}

void ExtendedBoard::SetAllPieces(const Position& pos) {
  for (Square s : Square::all_squares()) {
    Piece p = pos.piece_on(s);
    if (p != kNoPiece) {
      PutPiece(p, s);
    }
  }
}

void ExtendedBoard::MakeCaptureMove(Move move) {
  assert(move.is_capture());

  Square from = move.from();
  Square to   = move.to();

  // 1. いったん駒を盤上から取り除く
  RemoveControls(move.piece(), from);
  RemoveControls(move.captured_piece(), to);
  board_[from] = kNoPiece;
  ExtendLongControls(from);

  // 2. 駒を移動先のマスに置く
  board_[to] = move.piece_after_move();
  AddControls(move.piece_after_move(), to);
}

void ExtendedBoard::MakeNonCaptureMove(Move move) {
  assert(!move.is_capture() && !move.is_drop());

  Square from = move.from();
  Square to   = move.to();

  // 1. 移動元から駒を取り除く
  RemoveControls(move.piece(), from);
  ExtendLongControls(from);
  board_[from] = kNoPiece;

  // 2. 移動先のマスに駒を置く
  board_[to] = move.piece_after_move();
  CutLongControls(to);
  AddControls(move.piece_after_move(), to);
}

void ExtendedBoard::MakeDropAndKingRecapture(Square king_from, Square king_to,
                                             Color king_color) {
  assert(num_controls(~king_color, king_from) == 0);
  assert(num_controls(~king_color, king_to  ) == 0);

  Piece piece(king_color, kKing);

  // 1. 移動元から駒を取り除く
  RemoveControls(piece, king_from);
  OperateLongControls(king_color, king_from,
                      DirectionSet(controls_[king_color][king_from].u8.direction),
                      [](uint16_t& lhs, uint16_t rhs) { lhs += rhs; });
  board_[king_from] = kNoPiece;

  // 2. 移動先のマスに駒を置く
  board_[king_to] = piece;
  OperateLongControls(king_color, king_to,
                      DirectionSet(controls_[king_color][king_to].u8.direction),
                      [](uint16_t& lhs, uint16_t rhs) { lhs -= rhs; });
  AddControls(piece, king_to);
}

UNROLL_LOOPS Bitboard ExtendedBoard::GetControlledSquares(const Color color) const {
  union ControlledSquares {
    ControlledSquares() : qword{0, 0} {}
    uint64_t qword[2];
    uint8_t byte[16];
  };

  ControlledSquares controlled_squares;

  constexpr uint8_t r = 0x80;
  const __m128i kMask = _mm_set_epi8(r, r, r, r, r, r, r, r, 14, 12, 10, 8, 6, 4, 2, 0);
  const __m128i kZero = _mm_setzero_si128();

  for (size_t i = 0; i < 11; ++i) {
    // 1. 利き数を下位64ビットに集約する
    __m128i shuffled = _mm_shuffle_epi8(controls_[color].xmm(i), kMask);
    // 2. 利き数がゼロ以上のマスを調べる
    __m128i non_zero = _mm_cmpgt_epi8(shuffled, kZero);
    // 3. 結果をビットセットに保存する
    controlled_squares.byte[i] = static_cast<uint8_t>(_mm_movemask_epi8(non_zero));
  }

  // ビットボードの作成
  uint64_t q0 = controlled_squares.qword[0] & (UINT64_MAX >> 1);
  uint64_t q1 = (controlled_squares.qword[1] << 1) | (controlled_squares.qword[0] >> 63);
  return Bitboard(q1, q0);
}

PsqControlList ExtendedBoard::GetPsqControlList() const {
  PsqControlList list;

  constexpr int kShiftBlackControls = PsqControlIndex::kKeyBlackControls.shift;
  constexpr int kShiftWhiteControls = PsqControlIndex::kKeyWhiteControls.shift;
  constexpr int kShiftSquare = PsqControlIndex::kKeySquare.shift;

  const __m128i kMaskNumControls = _mm_set1_epi16(0xff);
  const __m128i kMaxNumOfControls = _mm_set1_epi16(3);
  const __m128i kSquareIncrement = _mm_set1_epi16(8 << kShiftSquare);

  __m128i squares = _mm_set_epi16(7 << kShiftSquare, 6 << kShiftSquare,
                                  5 << kShiftSquare, 4 << kShiftSquare,
                                  3 << kShiftSquare, 2 << kShiftSquare,
                                  1 << kShiftSquare, 0 << kShiftSquare);

  for (size_t i = 0; i < 11; ++i) {
    // a. そのマスにある駒
    __m128i indices = _mm_cvtepu8_epi16(_mm_movpi64_epi64(board_.mm(i)));
    // b. 先手の利き数
    __m128i black_controls = _mm_and_si128(controls_[kBlack].xmm(i), kMaskNumControls);
    black_controls = _mm_min_epu16(black_controls, kMaxNumOfControls); // 利き数の最大値を３に制限する
    black_controls = _mm_slli_epi16(black_controls, kShiftBlackControls);
    indices = _mm_or_si128(indices, black_controls);
    // c. 後手の利き数
    __m128i white_controls = _mm_and_si128(controls_[kWhite].xmm(i), kMaskNumControls);
    white_controls = _mm_min_epu16(white_controls, kMaxNumOfControls); // 利き数の最大値を３に制限する
    white_controls = _mm_slli_epi16(white_controls, kShiftWhiteControls);
    indices = _mm_or_si128(indices, white_controls);
    // d. マスの位置
    indices = _mm_or_si128(indices, squares);
    squares = _mm_add_epi16(squares, kSquareIncrement); // 次の８マスへ
    // インデックスをリストに保存する
    list.xmm(i) = indices;
  }

  return list;
}

void ExtendedBoard::Init() {
  // 1. short_attack_patterns_を初期化する。
  for (Piece piece : Piece::all_pieces()) {
    const Square kCenterSquare = kSquare5E;
    step_attacks_bb(piece, kCenterSquare).ForEach([&](Square s) {
      Square delta = s - kCenterSquare;
      short_attack_patterns_[piece].at(delta).u8.number = 1;
    });
  }

  // 2. attack_correction_を初期化する。
  for (Square s : Square::all_squares()) {
    AttackPattern mask;
    // すべてのビットを１にする
    std::memset(&mask, UINT8_MAX, sizeof(mask));
    // 将棋盤の外に出てしまう場合は、そのマスのビットをゼロにする
    if (s.rank() == kRank1 || s.file() == kFile1) mask.at(kDeltaNE ).u16 = 0;
    if (                      s.file() == kFile1) mask.at(kDeltaE  ).u16 = 0;
    if (s.rank() == kRank9 || s.file() == kFile1) mask.at(kDeltaSE ).u16 = 0;
    if (s.rank() <= kRank2 || s.file() == kFile1) mask.at(kDeltaNNE).u16 = 0;
    if (s.rank() >= kRank8 || s.file() == kFile1) mask.at(kDeltaSSE).u16 = 0;
    if (s.rank() == kRank1                      ) mask.at(kDeltaN  ).u16 = 0;
    if (s.rank() == kRank9                      ) mask.at(kDeltaS  ).u16 = 0;
    if (s.rank() == kRank1 || s.file() == kFile9) mask.at(kDeltaNW ).u16 = 0;
    if (                      s.file() == kFile9) mask.at(kDeltaW  ).u16 = 0;
    if (s.rank() == kRank9 || s.file() == kFile9) mask.at(kDeltaSW ).u16 = 0;
    if (s.rank() <= kRank2 || s.file() == kFile9) mask.at(kDeltaNNW).u16 = 0;
    if (s.rank() >= kRank8 || s.file() == kFile9) mask.at(kDeltaSSW).u16 = 0;
    attack_correction_[s] = mask;
  }

  // 3. direction_to_increment_を初期化する。
  const DirectionSet kAllDirections = DirectionSet().set();
  for (Direction dir : kAllDirections) {
    direction_to_increment_[dir].u8.number = 1;
    direction_to_increment_[dir].u8.direction = DirectionSet(dir);
  }

  // 4. max_number_of_long_attacks_を初期化する。
  for (Square sq : Square::all_squares())
    for (Direction dir : kAllDirections) {
      int num_attacks = 0;
      Square delta = Square::direction_to_delta(dir);
      Square to = sq + delta;
      while (to.IsOk() && Square::distance(to - delta, to) == 1) {
        num_attacks += 1;
        to += delta;
      }
      max_number_of_long_attacks_[sq][dir] = num_attacks;
    }

  // 5. edge_correction8_を初期化する。
  for (Square sq : Square::all_squares()) {
    for (Direction dir : kAllDirections) {
      Square to = sq + Square::direction_to_delta(dir);
      if (to.IsOk() && Square::distance(sq, to) == 1) {
        edge_correction8_[sq].set(dir);
      } else {
        edge_correction8_[sq].reset(dir);
      }
    }
  }

  // 6. edge_correction15_を初期化する。
  for (Square sq : Square::all_squares()) {
    for (int delta_x = -1; delta_x <= 1; ++delta_x) {
      for (int delta_y = -2; delta_y <= 2; ++delta_y) {
        Square to(sq.file() + delta_x, sq.rank() + delta_y);
        if (to.IsOk() && Square::distance(sq, to) <= 2) {
          edge_correction15_[sq].set(delta_x, delta_y);
        } else {
          edge_correction15_[sq].reset(delta_x, delta_y);
        }
      }
    }
  }
}

bool ExtendedBoard::IsOk() const {
  // 利き数が10以内であること（影の利きを除けば、同一のマスに10以上の利きを足すことはできない）
  for (Square s : Square::all_squares()) {
    if (num_controls(kBlack, s) > 10 || num_controls(kWhite, s) > 10) {
      return false;
    }
  }
  return true;
}

#if !defined(MINIMUM)

void ExtendedBoard::Print() const {
  // 1. 盤上の駒を表示する
  std::printf("Pieces on the Board\n");
  for (Rank r = kRank1; r <= kRank9; ++r) {
    for (File f = kFile9; f >= kFile1; --f) {
      Piece p = piece_on(Square(f, r));
      if (p == kNoPiece) {
        std::printf("  .");
      } else {
        std::printf("%3s", p.ToSfen().c_str());
      }
    }
    std::printf("\n");
  }

  // 2. 各マスの利き数を表示する
  for (Color c : {kBlack, kWhite}) {
    std::printf("Number of Controls (%s)\n", c == kBlack ? "Black" : "White");
    for (Rank r = kRank1; r <= kRank9; ++r) {
      for (File f = kFile9; f >= kFile1; --f) {
        int n = num_controls(c, Square(f, r));
        if (n == 0) {
          std::printf("  .");
        } else {
          std::printf("%3d", n);
        }
      }
      std::printf("\n");
    }
  }
}

#endif // !defined(MINIMUM)
