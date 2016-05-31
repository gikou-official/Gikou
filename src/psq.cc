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

#include "psq.h"

#include "position.h"

ArrayMap<PsqIndex, Color, PieceType> PsqIndex::hand_;
ArrayMap<PsqIndex, Square, Piece> PsqIndex::psq_;
Array<Piece, 2110> PsqIndex::index_to_piece_;
Array<Square, 2110> PsqIndex::index_to_square_;

ArrayMap<Array<PsqPair, 19>, Color, PieceType> PsqPair::hand_;
ArrayMap<PsqPair, Square, Piece> PsqPair::psq_;
ArrayMap<PsqPair, PsqIndex> PsqPair::all_pairs_;

void PsqIndex::Init() {
  // 1. 持ち駒のインデックスを計算する
  PsqIndex hand_index(0);
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : {kPawn, kLance, kKnight, kSilver, kGold, kBishop, kRook}) {
      // a. 持ち駒のインデックスのオフセットを計算する
      // ここで-1しているのは、持ち駒0枚目はカウントしないので、持ち駒が1枚目のときに、
      // インデックスが0から始まるようにするため。
      // このオフセットに持ち駒の枚数を足したものが、実際のインデックスとなる。
      // 例えば、先手の歩の3枚目の持ち駒であれば、そのインデックスは、-1+3=2となる。
      hand_[c][pt] = hand_index - 1;

      // b. 逆引き用の表も用意する
      for (int num = 1; num <= GetMaxNumber(pt); ++num) {
        index_to_square_[hand_index] = kSquareNone;
        index_to_piece_[hand_index] = Piece(c, pt);
        ++hand_index;
      }
    }
  }
  assert(hand_index == 76);

  // 2. 盤上の駒のインデックスを計算する
  PsqIndex board_index(76);
  for (Piece piece : Piece::all_pieces()) {
    if (piece.is(kKing)) {
      continue;
    }
    for (Square square : Square::all_squares()) {
      if (!piece.may_not_be_placed_on(square.rank())) {
        index_to_piece_[board_index] = piece;
        index_to_square_[board_index] = square;
        psq_[square][piece] = board_index++;
      }
    }
  }
  assert(board_index == PsqIndex::max() + 1);
}

void PsqPair::Init() {
  // PsqIndexの初期化を先に行う。
  PsqIndex::Init();

  // 1. 持ち駒
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      for (int num_pieces = 1; num_pieces <= GetMaxNumber(pt); ++num_pieces) {
        PsqIndex index_black = PsqIndex::OfHand( c, pt, num_pieces);
        PsqIndex index_white = PsqIndex::OfHand(~c, pt, num_pieces); // 先後反転
        PsqPair psq_pair(index_black, index_white);
        hand_[c][pt][num_pieces] = psq_pair;
        all_pairs_[psq_pair.black()] = psq_pair;
      }
    }
  }

  // 2. 盤上の駒
  for (Square s : Square::all_squares()) {
    for (Piece p : Piece::all_pieces()) {
      if (p.is(kKing) || p.may_not_be_placed_on(s.rank())) {
        continue;
      }
      PsqIndex index_black = PsqIndex::OfBoard(p, s);
      PsqIndex index_white = PsqIndex::OfBoard(p.opponent_piece(), Square::rotate180(s)); // 先後反転
      PsqPair psq_pair(index_black, index_white);
      psq_[s][p] = psq_pair;
      all_pairs_[psq_pair.black()] = psq_pair;
    }
  }
}

PsqList::PsqList(const Position& pos) : size_(0) {
  // 1. 持ち駒をコピーする
  // 持ち駒の枚数の情報をリスト自身が保持することにより、MakeMove(), UnmakeMove()の両関数に
  // Positionクラスを引数として渡す必要がなくなる。
  hand_[kBlack] = pos.hand(kBlack);
  hand_[kWhite] = pos.hand(kWhite);

  // 2. 持ち駒のインデックスを追加
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      for (int i = 1, n = pos.hand(c).count(pt); i <= n; ++i) {
        list_[size_] = PsqPair::OfHand(c, pt, i);
        hand_index_[c][pt][i] = size_;
        ++size_;
      }
    }
  }

  // 3. 盤上の駒のインデックスを追加
  pos.pieces().andnot(pos.pieces(kKing)).Serialize([&](Square s) {
    Piece piece = pos.piece_on(s);
    list_[size_] = PsqPair::OfBoard(piece, s);
    index_[s] = size_;
    ++size_;
  });

  assert(IsOk());
}

void PsqList::MakeMove(const Move move) {
  assert(move.IsOk());
  assert(move.is_real_move());

  Square to = move.to();
  Piece piece = move.piece();
  Color side_to_move = piece.color();

  if (move.is_drop()) {
    // 打つ手の場合：移動元となった駒台のインデックスを、移動先のマスのインデックスで上書きする
    PieceType pt = piece.type();
    int num = hand_[side_to_move].count(pt);
    int idx = hand_index_[side_to_move][pt][num];
    list_[idx] = PsqPair::OfBoard(piece, to);
    index_[to] = idx;
    hand_[side_to_move].remove_one(pt);
  } else {
    Square from = move.from();

    // 取る手の場合：取った駒は、盤上から駒台に移動するので、新たなインデックスで上書きする
    if (move.is_capture()) {
      PieceType hand_type = move.captured_piece().hand_type();
      int num = hand_[side_to_move].count(hand_type) + 1;
      int idx = index_[to];
      list_[idx] = PsqPair::OfHand(side_to_move, hand_type, num);
      hand_index_[side_to_move][hand_type][num] = idx;
      hand_[side_to_move].add_one(hand_type);
    }

    // 動かす手の場合：移動元のインデックスを、移動先のインデックスで上書きする
    if (!piece.is(kKing)) { // 玉を動かす手の場合は、インデックスに変化はない
      int idx = index_[from];
      list_[idx] = PsqPair::OfBoard(move.piece_after_move(), to);
      index_[to] = idx;
    }
  }

  assert(IsOk());
}

void PsqList::UnmakeMove(const Move move) {
  assert(move.IsOk());
  assert(move.is_real_move());

  Square to = move.to();
  Piece piece = move.piece();
  Color side_to_move = piece.color();

  if (move.is_drop()) {
    // 打つ手の場合：移動先（盤上）のインデックスを、移動元（駒台）のインデックスに戻す
    PieceType pt = piece.type();
    int num = hand_[side_to_move].count(pt) + 1;
    int idx = index_[to];
    list_[idx] = PsqPair::OfHand(side_to_move, pt, num);
    hand_index_[side_to_move][pt][num] = idx;
    hand_[side_to_move].add_one(pt);
  } else {
    Square from = move.from();

    // 動かす手の場合：移動先のマスのインデックスを、移動元のインデックスに戻す
    if (!piece.is(kKing)) { // 玉を動かす手の場合は、インデックスに変化はない
      int idx = index_[to];
      list_[idx] = PsqPair::OfBoard(piece, from);
      index_[from] = idx;
    }

    // 取る手の場合：取った駒について、駒台のインデックスを盤上の元あった位置のインデックスに戻す
    if (move.is_capture()) {
      Piece captured = move.captured_piece();
      PieceType hand_type = captured.hand_type();
      int num = hand_[side_to_move].count(hand_type);
      int idx = hand_index_[side_to_move][hand_type][num];
      list_[idx] = PsqPair::OfBoard(captured, to);
      index_[to] = idx;
      hand_[side_to_move].remove_one(hand_type);
    }
  }

  assert(IsOk());
}

bool PsqList::IsOk() const {
  // 1. 要素数のチェック
  if (size_ > kMaxSize) {
    return false;
  }

  // 2. 持ち駒の要素がリストに含まれているかをチェックする
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      for (int num = 1; num <= hand_[c].count(pt); ++num) {
        // 持ち駒の要素がリストに含まれているか探す
        auto iter = std::find_if(begin(), end(), [&](PsqPair item) {
          return item.black() == PsqIndex::OfHand(c, pt, num);
        });
        // a. あるべき要素がリストになかった
        if (iter == end()) return false;
        // b. hand_index_との整合性がとれていなかった
        if (hand_index_[c][pt][num] != (iter - begin())) return false;
      }
    }
  }

  // 3. index_が正しい場所を示しているかチェック
  for (size_t i = 0; i < size_; ++i) {
    PsqPair item = list_[i];
    if (item.square() == kSquareNone) {
      continue;
    }
    if (index_[item.square()] != static_cast<int>(i)) {
      return false;
    }
  }

  return true;
}

bool PsqList::TwoListsHaveSameItems(const PsqList& list1, const PsqList& list2) {
  if (list1.size() != list2.size()) {
    return false;
  }

  // ローカル変数にコピーしたうえで、ソートする
  Array<PsqPair, kMaxSize> list_l = list1.list_, list_r = list2.list_;
  auto less = [](PsqPair lhs, PsqPair rhs) {
    return lhs.black() < rhs.black();
  };
  size_t size = list_l.size();
  std::sort(list_l.begin(), list_l.begin() + size, less);
  std::sort(list_r.begin(), list_r.begin() + size, less);

  // 双方を比較する
  return std::includes(list_l.begin(), list_l.begin() + size,
                       list_r.begin(), list_r.begin() + size, less);
}

constexpr PsqControlIndex::Key PsqControlIndex::kKeyPiece;
constexpr PsqControlIndex::Key PsqControlIndex::kKeyBlackControls;
constexpr PsqControlIndex::Key PsqControlIndex::kKeyWhiteControls;
constexpr PsqControlIndex::Key PsqControlIndex::kKeySquare;

PsqControlList::BitSet128 PsqControlList::ComputeDifference(const PsqControlList& lhs,
                                                            const PsqControlList& rhs) {
  BitSet128 bitset;

  constexpr uint8_t r = 0x80;
  const __m128i kMask = _mm_set_epi8(r, r, r, r, r, r, r, r, 14, 12, 10, 8, 6, 4, 2, 0);
  const __m128i kAllOne = _mm_set1_epi8(static_cast<int8_t>(UINT8_MAX));

  for (size_t i = 0; i < 11; ++i) {
    // 1. ２つのリストが同じかを比較する（これにより、2つのリストが同じ部分だけビットが１になる）
    __m128i equal = _mm_cmpeq_epi16(lhs.xmm(i), rhs.xmm(i));
    // 2. すべてのビットを反転する（これにより、2つのリストが異なっている部分だけビットが１になる）
    __m128i not_equal = _mm_andnot_si128(equal, kAllOne);
    // 3. 比較結果を下位64ビットに集約する
    __m128i shuffled = _mm_shuffle_epi8(not_equal, kMask);
    // 4. 結果をビットセットに保存する
    bitset.byte[i] = static_cast<uint8_t>(_mm_movemask_epi8(shuffled));
  }

  return bitset;
}
