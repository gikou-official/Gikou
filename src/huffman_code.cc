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

#include "huffman_code.h"

namespace {

/**
 * ハフマン符号化を補助するための、ビットストリームです.
 */
struct BitStream {
 public:
  BitStream() {
    array_.clear();
  }

  BitStream(const Array<uint64_t, 4>& array)
      : array_(array) {
  }

  uint64_t get() {
    uint64_t value = (array_[pos_ / 64] >> (pos_ % 64)) & UINT64_C(1);
    ++pos_;
    assert(value == 0 || value == 1);
    return value;
  }

  uint64_t read(size_t count) {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
      value |= get() << i;
    }
    return value;
  }

  void put(uint64_t value) {
    assert(value == 0 || value == 1);
    array_[pos_ / 64] |= value << (pos_ % 64);
    ++pos_;
  }

  void write(uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      put((value >> i) & UINT64_C(1));
    }
  }

  size_t size() const {
    return 256;
  }

  bool eof() const {
    return pos_ == size();
  }

  const Array<uint64_t, 4>& array() {
    return array_;
  }

 private:
  Array<uint64_t, 4> array_;
  size_t pos_ = 0;
};

/**
 * ハフマン符号化で用いるビット列です.
 */
struct HuffmanBitString {
  int bits;   // その駒を表すビット列
  int length; // そのビット列の長さ（単位はバイト数ではなく、ビット数）
};

/**
 * 各駒のハフマン符号の定義です.
 */
const ArrayMap<HuffmanBitString, PieceType> g_huffman_code_table = {
    {kNoPieceType, {0x00, 1}},
    {kPawn       , {0x01, 2}},
    {kLance      , {0x03, 4}},
    {kKnight     , {0x0b, 4}},
    {kSilver     , {0x07, 4}},
    {kGold       , {0x0f, 5}},
    {kBishop     , {0x1f, 6}},
    {kRook       , {0x3f, 6}},
};

/**
 * ハフマン符号を高速に復号化するための参照テーブルです.
 */
Array<PieceType, 6, 64> g_huffman_decoder_table; // [ビット長][ハフマン符号]

/**
 * 与えられたハフマン符号に該当する駒が存在しない場合、g_huffman_decoder_tableに
 * この値をセットしておきます.
 */
const PieceType kHuffmanCodeNotFound = static_cast<PieceType>(-1);

/**
 * 駒をハフマン符号で符号化します.
 */
template<bool kIsHandPiece>
inline HuffmanBitString EncodePiece(Piece piece) {
  assert(piece.type() != kKing);

  if (piece == kNoPiece) {
    return g_huffman_code_table[kNoPieceType];
  }

  // 1. 駒の種類
  HuffmanBitString huffman = g_huffman_code_table[piece.original_type()];

  if (kIsHandPiece) {
    // 持ち駒の場合は、最初のビットを削る
    huffman.bits >>= 1;
    huffman.length -= 1;
  }

  // 2. 駒の持ち主
  huffman.bits |= static_cast<int>(piece.color()) << huffman.length++;

  // 3. 成り駒か否か（金以外の駒のみ）
  if (piece.type() != kGold) {
    bool is_promoted = !kIsHandPiece && piece.is_promoted();
    huffman.bits |= static_cast<int>(is_promoted) << huffman.length++;
  }

  return huffman;
}

/**
 * 駒のハフマン符号を復号化します.
 */
template<bool kIsHandPiece>
inline Piece DecodePiece(BitStream& bit_stream) {
  // 1. 駒の種類
  uint64_t code = kIsHandPiece ? 1 : 0;
  PieceType pt = kHuffmanCodeNotFound;
  for (int i = kIsHandPiece ? 1 : 0; pt == kHuffmanCodeNotFound; ++i) {
    assert(i < 6);
    code |= bit_stream.get() << i;
    pt = g_huffman_decoder_table[i][code];
  }

  if (pt == kNoPieceType) {
    return kNoPiece;
  }

  // 2. 駒の持ち主
  Color color = static_cast<Color>(bit_stream.get());
  Piece piece(color, pt);

  // 3. 成り駒か否か
  return (pt != kGold && bit_stream.get()) ? piece.promoted_piece() : piece;
}

} // namespace

HuffmanCode HuffmanCode::EncodePosition(const Position& pos) {
  // 前提条件: 駒落ちの局面ではないこと
  assert(pos.num_unused_pieces(kPawn  ) == 0);
  assert(pos.num_unused_pieces(kLance ) == 0);
  assert(pos.num_unused_pieces(kKnight) == 0);
  assert(pos.num_unused_pieces(kSilver) == 0);
  assert(pos.num_unused_pieces(kGold  ) == 0);
  assert(pos.num_unused_pieces(kBishop) == 0);
  assert(pos.num_unused_pieces(kRook  ) == 0);
  assert(pos.num_unused_pieces(kKing  ) == 0);

  BitStream bit_stream;

  // 1. 手番
  bit_stream.put(static_cast<int>(pos.side_to_move()));

  // 2. 玉の位置
  bit_stream.write(static_cast<int>(pos.king_square(kBlack)), 7);
  bit_stream.write(static_cast<int>(pos.king_square(kWhite)), 7);

  // 3. 盤上の駒
  for (Square s : Square::all_squares()) {
    Piece piece = pos.piece_on(s);
    if (piece.type() != kKing) {
      HuffmanBitString huffman = EncodePiece<false>(piece);
      bit_stream.write(huffman.bits, huffman.length);
    }
  }

  // 4. 持ち駒
  for (Color c : {kBlack, kWhite}) {
    Hand hand = pos.hand(c);
    for (PieceType pt : Piece::all_hand_types()) {
      int count = hand.count(pt);
      if (count == 0) continue;
      HuffmanBitString huffman = EncodePiece<true>(Piece(c, pt));
      do {
        bit_stream.write(huffman.bits, huffman.length);
      } while (--count > 0);
    }
  }

  assert(bit_stream.eof());

  return HuffmanCode(bit_stream.array());
}

Position HuffmanCode::DecodePosition(const HuffmanCode& huffman_code) {
  Position pos;
  BitStream bit_stream(huffman_code.array());

  // 1. 手番
  Color side_to_move = static_cast<Color>(bit_stream.get());
  pos.set_side_to_move(side_to_move);

  // 2. 玉の位置
  Square black_king_square(bit_stream.read(7));
  Square white_king_square(bit_stream.read(7));
  pos.PutPiece(kBlackKing, black_king_square);
  pos.PutPiece(kWhiteKing, white_king_square);

  // 3. 盤上の駒
  for (Square s : Square::all_squares()) {
    if (pos.is_empty(s)) {
      Piece piece = DecodePiece<false>(bit_stream);
      if (piece != kNoPiece) {
        pos.PutPiece(piece, s);
      }
    }
  }

  pos.InitStateInfo(); // 将棋盤に駒を配置し終えたところで、StateInfoを初期化する

  // 4. 持ち駒
  while (!bit_stream.eof()) {
    Piece piece = DecodePiece<true>(bit_stream);
    pos.AddOneToHand(piece.color(), piece.type());
  }

  return pos;
}

void HuffmanCode::Init() {
  // 1. g_huffman_decoder_tableを初期化する
  {
    // 初期値として、kHuffmanCodeNotFoundをセットする
    for (int length = 0; length < 6; ++length)
      for (int bits = 0; bits < 64; ++bits) {
        g_huffman_decoder_table[length][bits] = kHuffmanCodeNotFound;
      }

    // 各駒のハフマン符号をセットする
    for (int i = kNoPieceType; i <= kRook; ++i) {
      PieceType piece_type = static_cast<PieceType>(i);
      HuffmanBitString huffman = g_huffman_code_table[piece_type];
      g_huffman_decoder_table[huffman.length - 1][huffman.bits] = piece_type;
    }
  }
}
