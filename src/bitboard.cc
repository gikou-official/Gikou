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

#include "bitboard.h"

#include <vector>
#include "common/array.h"

namespace {

// 以下の３つは、ビットボードのテーブルを初期化するための関数です
Bitboard ComputeSlidingAttacks(Square from, Bitboard occ,
                               const std::vector<Square>& deltas);
Bitboard ComputeOccupancy(Bitboard mask, uint32_t index);
Bitboard ComputeCheckerCandidates(Piece, Square, bool adjacent_check_only);

const ArrayMap<int, Square> g_lance_shifts = {
     1,  1,  1,  1,  1,  1,  1,  1,  1,
    10, 10, 10, 10, 10, 10, 10, 10, 10,
    19, 19, 19, 19, 19, 19, 19, 19, 19,
    28, 28, 28, 28, 28, 28, 28, 28, 28,
    37, 37, 37, 37, 37, 37, 37, 37, 37,
    46, 46, 46, 46, 46, 46, 46, 46, 46,
    55, 55, 55, 55, 55, 55, 55, 55, 55,
     1,  1,  1,  1,  1,  1,  1,  1,  1,
    10, 10, 10, 10, 10, 10, 10, 10, 10,
};

const ArrayMap<int, Square> g_bishop_shifts = {
    57, 58, 58, 58, 58, 58, 58, 58, 57,
    58, 58, 58, 58, 58, 58, 58, 58, 58,
    58, 58, 56, 56, 56, 56, 56, 58, 58,
    58, 58, 56, 54, 54, 54, 56, 58, 58,
    58, 58, 56, 54, 52, 54, 56, 58, 58,
    58, 58, 56, 54, 54, 54, 56, 58, 58,
    58, 58, 56, 56, 56, 56, 56, 58, 58,
    58, 58, 58, 58, 58, 58, 58, 58, 58,
    57, 58, 58, 58, 58, 58, 58, 58, 57
};

// [17]と[53]については、シフト量を減らしてマジックナンバーを見つけやすくしています
//（idea from issei_y）。
// 参考: http://d.hatena.ne.jp/hiraoka64/20110116/1295175872
const ArrayMap<int, Square> g_rook_shifts = {
    50, 51, 51, 51, 51, 51, 51, 51, 50,
    51, 52, 52, 52, 52, 52, 52, 52, 50, // [17] は、51から50に変更
    51, 52, 52, 52, 52, 52, 52, 52, 51,
    51, 52, 52, 52, 52, 52, 52, 52, 51,
    51, 52, 52, 52, 52, 52, 52, 52, 51,
    51, 52, 52, 52, 52, 52, 52, 52, 50, // [53] は、51から50に変更
    51, 52, 52, 52, 52, 52, 52, 52, 51,
    51, 52, 52, 52, 52, 52, 52, 52, 51,
    50, 51, 51, 51, 51, 51, 51, 51, 50
};

/**
 * 角の利きを計算するためのマジックナンバーです.
 */
const ArrayMap<uint64_t, Square> g_bishop_magics = {
      0x60504089481002,   0x12002180051011,   0x20881010480400,
    0x4210221210080455,  0x205004808001100, 0x9808210402000000,
        0x801021084084,   0x544b0013804020, 0x6020022408308020,
    0x4202010300882042, 0x2010080010200404,      0x20010021001,
    0x80400200400400a4,       0x4020900004,     0x112201024010,
    0x905020108100a08a,  0x989006004400880,  0x100402110840840,
      0x20000410444908,  0xb10440488800404, 0x2c08100820402001,
    0x3022000048c00b43,  0x106081110200400, 0x2201000120048210,
    0x4000200040813004, 0x1002402404404802,  0x9200402040810a0,
    0x8202108002001440, 0x1202088000402660,    0x4010000208108,
     0x602000800400102,  0xa02010000040082, 0x2000042004008010,
     0xd0008a00010400c, 0x108024c080242048,  0x45080408d0a1421,
     0x886080881850011, 0x2202420280220841,  0x401202844604008,
    0x8200040200800801,   0x40810020028004, 0x2000104020110081,
     0x828001002000c08,  0x902001202209004,  0x400201010890042,
     0x208840208202440,   0x8021020940c0a0, 0xb011100449000020,
    0x21000408008c4010, 0x4100000880400011, 0x15402400a0004015,
    0x6110001280020033,  0x218004004820202, 0x9480810003411002,
    0x2200402389020040, 0x2400421283000810,  0x1008040a0100403,
     0x400184014102202,  0x200100602008440, 0x8100010208085000,
      0x50080080610500, 0x8a040a0009440200, 0x8100981420143400,
     0x100481300608140, 0x800010c082202804, 0x4000040802208440,
          0x9010208204,  0x404092401010088,  0x20022040201050a,
    0x2408010500084082, 0x21200400800a0101, 0xa101048c40821002,
     0x200811040020860, 0x4100204010686005, 0xa081208204201812,
     0x402022030008104,  0x820004030104082, 0x2042000000410021,
      0x20008810800810,   0x200c10808c2050,   0x11082100206011,
};

/**
 * 飛車の利きを計算するためのマジックナンバーです.
 */
const ArrayMap<uint64_t, Square> g_rook_magics = {
     0x14000050b010100, 0x3020000200584280, 0x1040030600004e80,
      0x800114022ac440, 0x2d40020008248890, 0xa0400100000d0808,
    0x4040004001880a18,   0x80000b10601204,   0x40000458820042,
      0x14200008008100, 0x1001100001420080,    0x4200400008001,
     0x404400081000082,  0x208400040480020, 0x1810a00040002001,
     0xb40200020040308, 0x1000201000100002,    0x1040002002101,
     0x418885000020001, 0x4000080c00028020,   0x20084000900041,
    0x1000102800800450, 0x2040409000400820,  0x540005000200010,
    0xa080002000400808, 0x4840009000080024, 0x1040003000010102,
    0x410d100008000085,   0x20080104000040, 0x6000081008004040,
     0xd40040028002120, 0x60010420100063a0, 0x8600808008002001,
      0x80004010002008,     0x400008040004, 0x20400024c8000142,
      0x900408000c0001,    0x8000800020080,   0x20080001080022,
     0x802040008040020, 0x8800101040060010,  0x400100040020004,
     0x580010020180028,  0x140004000040202, 0x1040040544040012,
    0x20110c1104000200, 0x200129081b002100, 0x9000080100400080,
    0x8c00020001000201,  0x410400808008004,  0xd00008000400201,
    0x8900008000400402, 0x2300800010000201,   0xa5000410000041,
     0x1c002a001460082, 0x104008427002000a, 0x1000140108010003,
    0x13400901000a8001, 0xc180608080800142, 0x2080020020202002,
     0xb00220180881084,   0x42024100044418,  0x880029408464802,
    0x1c40000408808100,  0x840080004000041, 0x3240040008025180,
    0x2480082000882040,  0x880042005252060, 0x294002d000200410,
    0x2040002040000801, 0xe140014000480101,   0x80000488101512,
    0x8a04200006408100,  0x406500002000340, 0x90008800008000d0,
    0x1000300100009810,   0x91200080030820,  0x400600020400018,
      0x85400021242018, 0x4410200110008104,     0x600021024282,
};

} // namespace

ArrayMap<Bitboard, Square> Bitboard::square_bb_;
ArrayMap<Bitboard, File> Bitboard::file_bb_;
ArrayMap<Bitboard, Rank> Bitboard::rank_bb_;
ArrayMap<Bitboard, Color> Bitboard::promotion_zone_bb_;
ArrayMap<Bitboard, Square, Square> Bitboard::line_bb_;
ArrayMap<Bitboard, Square, Square> Bitboard::between_bb_;
ArrayMap<Bitboard, Square, DirectionSet> Bitboard::direction_bb_;
ArrayMap<Bitboard, Square> Bitboard::neighborhood24_bb_;
ArrayMap<Bitboard, Square, Piece> Bitboard::checker_candidates_bb_;
ArrayMap<Bitboard, Square, Piece> Bitboard::adjacent_check_candidates_bb_;
ArrayMap<Bitboard, Square, Piece> Bitboard::step_attacks_bb_;
ArrayMap<Bitboard, Square, Piece> Bitboard::min_attacks_bb_;
ArrayMap<Bitboard, Square, Piece> Bitboard::max_attacks_bb_;
ArrayMap<Bitboard::MagicNumber<Bitboard>, Square> Bitboard::magic_numbers_;
ArrayMap<Array<Bitboard, 128>, Square> Bitboard::lance_attacks_bb_;
Array<Bitboard, 20224> Bitboard::bishop_attacks_bb_;
Array<Bitboard, 512000> Bitboard::rook_attacks_bb_;
ArrayMap<uint64_t, Square> Bitboard::eight_neighborhoods_magics_;

Bitboard Bitboard::FileFill(Bitboard x) {
  // Kogge-Stone Algorithm
  // (http://chessprogramming.wikispaces.com/Kogge-Stone+Algorithm)
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_srli_epi64(x.xmm_, 1));
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_srli_epi64(x.xmm_, 2));
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_srli_epi64(x.xmm_, 4));
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_srli_epi64(x.xmm_, 1));
  x &= rank_bb(kRank1);
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_slli_epi64(x.xmm_, 1));
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_slli_epi64(x.xmm_, 2));
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_slli_epi64(x.xmm_, 4));
  x.xmm_ = _mm_or_si128(x.xmm_, _mm_slli_epi64(x.xmm_, 1));
  return x & board_bb();
}

/**
 * Bitboardの中のビットを、SFEN表記風の文字列に変換します.
 */
std::string Bitboard::ToString() const {
  std::string str;
  for (Rank r = kRank1; r <= kRank9; ++r) {
    int empty_count = 0;
    for (File f = kFile9; f >= kFile1; --f) {
      if (test(Square(f, r))) {
        if (empty_count) {
          str += std::to_string(empty_count);
          empty_count = 0;
        }
        str += '*';
      } else {
        ++empty_count;
      }
    }
    if (empty_count)
      str += std::to_string(empty_count);
    if (r != kRank9)
      str += '/';
  }
  return str;
}

/**
 * Bitboardの中のビットを、Chess Programming Wiki風スタイルで、標準出力へプリントします.
 * 参考: https://chessprogramming.wikispaces.com/General+Setwise+Operations
 */
void Bitboard::Print() const {
  for (Rank r = kRank1; r <= kRank9; ++r) {
    for (File f = kFile9; f >= kFile1; --f) {
      if (test(Square(f, r)))
        std::printf(" 1");
      else
        std::printf(" .");
    }
    std::printf("\n");
  }
  // デバッグの便宜のため、ToString()の結果もプリントしておく。
  std::printf("%s\n", ToString().c_str());
}

Bitboard AttacksFrom(Piece piece, Square from, Bitboard occ) {
  switch (piece.type()) {
    case kLance:
      return lance_attacks_bb(from, occ, piece.color());
    case kBishop:
      return bishop_attacks_bb(from, occ);
    case kRook:
      return rook_attacks_bb(from, occ);
    case kHorse:
      return bishop_attacks_bb(from, occ) | step_attacks_bb(kBlackKing, from);
    case kDragon:
      return rook_attacks_bb(from, occ) | step_attacks_bb(kBlackKing, from);
    default:
      return step_attacks_bb(piece, from);
  }
}

void Bitboard::Init() {
  const ArrayMap<std::vector<Square>, PieceType> steps = {
      {kPawn   , {kDeltaN}},
      {kLance  , {}},
      {kKnight , {kDeltaNNW, kDeltaNNE}},
      {kSilver , {kDeltaNW, kDeltaN, kDeltaNE, kDeltaSW, kDeltaSE}},
      {kGold   , {kDeltaNW, kDeltaN, kDeltaNE, kDeltaW, kDeltaE, kDeltaS}},
      {kBishop , {}},
      {kRook   , {}},
      {kKing   , {kDeltaNW, kDeltaN, kDeltaNE, kDeltaW, kDeltaE, kDeltaSW, kDeltaS, kDeltaSE}},
      {kPPawn  , {kDeltaNW, kDeltaN, kDeltaNE, kDeltaW, kDeltaE, kDeltaS}},
      {kPLance , {kDeltaNW, kDeltaN, kDeltaNE, kDeltaW, kDeltaE, kDeltaS}},
      {kPKnight, {kDeltaNW, kDeltaN, kDeltaNE, kDeltaW, kDeltaE, kDeltaS}},
      {kPSilver, {kDeltaNW, kDeltaN, kDeltaNE, kDeltaW, kDeltaE, kDeltaS}},
      {kHorse  , {kDeltaN, kDeltaW, kDeltaE, kDeltaS}},
      {kDragon , {kDeltaNW, kDeltaNE, kDeltaSW, kDeltaSE}}
  };
  const ArrayMap<std::vector<Square>, PieceType> slides = {
      {kLance , {kDeltaN}},
      {kBishop, {kDeltaNW, kDeltaNE, kDeltaSW, kDeltaSE}},
      {kRook  , {kDeltaN, kDeltaW, kDeltaE, kDeltaS}},
      {kHorse , {kDeltaNW, kDeltaNE, kDeltaSW, kDeltaSE}},
      {kDragon, {kDeltaN, kDeltaW, kDeltaE, kDeltaS}},
  };

  // 1. square_bb_
  for (Square s : Square::all_squares()) {
    if (s < 63)
      square_bb_[s] = Bitboard(0, UINT64_C(1) << s);
    else
      square_bb_[s] = Bitboard(UINT64_C(1) << (s - 63), 0);
  }

  // 2. file_bb_ and rank_bb_
  for (File f = kFile1; f <= kFile9; ++f)
    for (Rank r = kRank1; r <= kRank9; ++r) {
      file_bb_[f] |= square_bb_[Square(f, r)];
      rank_bb_[r] |= square_bb_[Square(f, r)];
    }

  // 3. promotion_zone_bb_
  promotion_zone_bb_[kBlack] = rank_bb(kRank1) | rank_bb(kRank2) | rank_bb(kRank3);
  promotion_zone_bb_[kWhite] = rank_bb(kRank7) | rank_bb(kRank8) | rank_bb(kRank9);

  // 4. step_attacks_bb_
  for (Square s : Square::all_squares())
    for (Piece p : Piece::all_pieces())
      for (Square delta : steps[p.type()]) {
        Square to = s + (p.is(kBlack) ? delta : -delta);
        if (to.IsOk() && Square::distance(s, to) <= 2) {
          step_attacks_bb_[s][p] |= square_bb_[to];
        }
      }

  // 5. min_attacks_bb_ and max_attacks_bb_
  min_attacks_bb_ = max_attacks_bb_ = step_attacks_bb_;
  for (Square s : Square::all_squares())
    for (Piece p : Piece::all_pieces())
      for (Square delta : slides[p.type()]) {
        Square step = p.is(kBlack) ? delta : -delta;
        Square from = s;
        Square to   = s + step;
        // 1. minimum attacks（利きの最小値）
        if (to.IsOk() && Square::distance(from, to) == 1) {
          min_attacks_bb_[s][p] |= square_bb_[to];
        }
        // 2. maximum attacks（利きの最大値）
        while (to.IsOk() && Square::distance(from, to) == 1) {
          max_attacks_bb_[s][p] |= square_bb_[to];
          from += step, to += step;
        }
      }

  // 6. マジックナンバー
  Bitboard* bishop_ptr = bishop_attacks_bb_.begin();
  Bitboard* rook_ptr   = rook_attacks_bb_.begin();
  for (Square sq : Square::all_squares()) {
    Bitboard empty_bb;
    Bitboard file19 = file_bb(kFile1) | file_bb(kFile9);
    Bitboard rank19 = rank_bb(kRank1) | rank_bb(kRank9);
    Bitboard file_edge = file19.andnot(file_bb(sq.file()));
    Bitboard rank_edge = rank19.andnot(rank_bb(sq.rank()));
    Bitboard edge_bb = file_edge | rank_edge;

    // マジックナンバーを、magic_numbers_配列にまとめておく。
    // このようにマスごとに１箇所にまとめておくほうが、アライメントの関係でほんの少しだけ高速化されるはず。
    auto& m = magic_numbers_[sq];
    m.lance_postmask[kBlack] = max_attacks_bb_[sq][kBlackLance];
    m.lance_postmask[kWhite] = max_attacks_bb_[sq][kWhiteLance];
    m.lance_premask = file_bb(sq.file()).andnot(rank19);
    m.bishop_mask   = max_attacks_bb_[sq][kBlackBishop].andnot(edge_bb);
    m.rook_mask     = max_attacks_bb_[sq][kBlackRook  ].andnot(edge_bb);
    m.bishop_magic  = g_bishop_magics[sq];
    m.rook_magic    = g_rook_magics[sq];
    m.bishop_ptr    = bishop_ptr;
    m.rook_ptr      = rook_ptr;
    m.lance_shift   = g_lance_shifts[sq];
    m.bishop_shift  = g_bishop_shifts[sq];
    m.rook_shift    = g_rook_shifts[sq];
    bishop_ptr += static_cast<ptrdiff_t>(1) << (64 - m.bishop_shift);
    rook_ptr   += static_cast<ptrdiff_t>(1) << (64 - m.rook_shift);

    // lance_attacks_bb_
    for (int i = 0; i < 128; ++i) {
      Bitboard occ   = ComputeOccupancy(m.lance_premask, i);
      uint64_t index = occ.uint64() >> m.lance_shift;
      lance_attacks_bb_[sq][index] = ComputeSlidingAttacks(sq, occ,
                                                           {kDeltaN, kDeltaS});
    }

    // bishop_attacks_bb_
    for (int i = 0, n = 1 << m.bishop_mask.count(); i < n; ++i) {
      Bitboard occ   = ComputeOccupancy(m.bishop_mask, i);
      uint64_t index = (occ.uint64() * m.bishop_magic) >> m.bishop_shift;
      m.bishop_ptr[index] = ComputeSlidingAttacks(sq, occ, slides[kBishop]);
    }

    // rook_attacks_bb_
    for (int i = 0, n = 1 << m.rook_mask.count(); i < n; ++i) {
      Bitboard occ   = ComputeOccupancy(m.rook_mask, i);
      uint64_t index = (occ.uint64() * m.rook_magic) >> m.rook_shift;
      m.rook_ptr[index] = ComputeSlidingAttacks(sq, occ, slides[kRook]);
    }
  }
  assert(bishop_ptr == bishop_attacks_bb_.end());
  assert(rook_ptr == rook_attacks_bb_.end());

  // 7. line_bb_ and between_bb_
  for (Square i : Square::all_squares())
    for (Square j : Square::all_squares())
      if (i != j && queen_attacks_bb(i, Bitboard()).test(j)) {
        Square delta = (j - i) / Square::distance(i, j);
        // between_bb_
        for (Square s = i + delta; s != j; s += delta) {
          between_bb_[i][j] |= square_bb_[s];
        }
        // line_bb_
        for (Square d : {delta, -delta}) {
          for (Square s = i; (s + d).IsOk() && Square::distance(s, s + d) == 1;
               s += d) {
            line_bb_[i][j] |= square_bb_[s] | square_bb_[s + d];
          }
        }
      }

  // 8. direction_bb_
  for (Square s : Square::all_squares())
    for (unsigned i = DirectionSet::min(); i <= DirectionSet::max(); ++i) {
      Bitboard bb;
      DirectionSet directions(i);
      for (Direction dir : directions) {
        Square delta = Square::direction_to_delta(dir);
        if ((s + delta).IsOk() && Square::distance(s, s + delta) == 1) {
          direction_bb_[s][directions].set(s + delta);
        }
      }
    }

  // 9. neighborhood24_bb_
  for (Square s1 : Square::all_squares()) {
    step_attacks_bb_[s1][kBlackKing].ForEach([&](Square s2) {
      neighborhood24_bb_[s1] |= step_attacks_bb_[s2][kBlackKing];
    });
    neighborhood24_bb_[s1].reset(s1); // 中心のマスのビットは、リセットしておく
  }

  // 10. checker_candidates_bb_ and adjacent_check_candidates_bb_
  for (Square s : Square::all_squares())
    for (Piece p : Piece::all_pieces()) {
      checker_candidates_bb_[s][p] = ComputeCheckerCandidates(p, s, false);
      adjacent_check_candidates_bb_[s][p] = ComputeCheckerCandidates(p, s, true);
    }

  // 11. eight_neighborhoods_magics_
  {
    constexpr uint64_t kOne = 1;
    enum BaseMagics : uint64_t {
                /*     left              center              right     */
      kBase00 = (kOne << (62- 9)) | (kOne << (59- 0)),
      kBase09 = (kOne << (62-18)) | (kOne << (59- 9)) | (kOne << (56- 0)),
    };
    for (int i =  0; i <  9; ++i) {
      eight_neighborhoods_magics_[Square(i)] = kBase00 >> (i -  0 + 1);
    }
    for (int i =  9; i < 45; ++i) {
      eight_neighborhoods_magics_[Square(i)] = kBase09 >> (i -  9 + 1);
    }
    for (int i = 45; i < 81; ++i) {
      eight_neighborhoods_magics_[Square(i)] =
          eight_neighborhoods_magics_[Square(9 + (i - 45))];
    }
  }
}

namespace {

/**
 * 飛び駒の利きを計算します.
 * @param from   駒の移動元
 * @param occ    飛び利きを遮る駒の配置パターン
 * @param deltas 飛び利きの方向
 * @return 飛び利きのある場所を表すビットボード
 */
Bitboard ComputeSlidingAttacks(Square from, Bitboard occ,
                               const std::vector<Square>& deltas) {
  Bitboard attacks_bb;
  for (Square delta : deltas)
    for (Square to = from + delta;
         to.IsOk() && Square::distance(to - delta, to) == 1; to += delta) {
      attacks_bb.set(to);
      if (occ.test(to)) {
        break;
      }
    }
  return attacks_bb;
}

/**
 * 飛び利きを遮る駒の配置パターンを返します.
 * @param mask  飛び駒の利きが存在する可能性のある場所
 * @param index 駒の配置パターンのインデックス
 * @return 飛び利きを遮る駒の配置パターン
 */
Bitboard ComputeOccupancy(Bitboard mask, uint32_t index) {
  Bitboard occ;
  for (int i = 0, n = mask.count(); i < n; ++i) {
    Square s = mask.pop_first_one();
    if (index & (UINT32_C(1) << i)) {
      occ.set(s);
    }
  }
  return occ;
}

/**
 * 王手をかけることができる駒の候補を返します.
 * @param piece               攻め方の駒の種類
 * @param king_square         受け方の玉の位置
 * @param adjacent_check_only trueであれば、近接王手をかけられる駒のみに限定する
 * @return 王手を王手をかけることができる駒の候補を表した、ビットボード
 */
Bitboard ComputeCheckerCandidates(Piece piece, Square king_square,
                                  bool adjacent_check_only) {
  // 玉を使って、相手玉に王手をかけることはできない
  if (piece.is(kKing)) {
    return Bitboard();
  }

  const Color color = piece.color();
  const Bitboard neighborhood12 = neighborhood8_bb(king_square)
                            | step_attacks_bb(kBlackKnight, king_square)
                            | step_attacks_bb(kWhiteKnight, king_square);
  const Bitboard all_squares = Bitboard::board_bb();
  const Bitboard target = adjacent_check_only ? neighborhood12 : all_squares;
  Bitboard result;
  
  // 1. 不成の王手
  const Piece original_piece = piece.opponent_piece();
  Bitboard non_promotion_target = max_attacks_bb(original_piece, king_square) & target;
  non_promotion_target.ForEach([&](Square to) {
    result |= max_attacks_bb(original_piece, to);
  });

  // 2. 成る王手
  if (piece.can_promote()) {
    Piece promoted_piece = piece.opponent_piece().promoted_piece();
    Bitboard promotion_zone = color == kBlack ? rank_bb<1, 3>() : rank_bb<7, 9>();
    Bitboard promotion_target = max_attacks_bb(promoted_piece, king_square) & target;
    promotion_target.ForEach([&](Square to) {
      if (to.is_promotion_zone_of(color)) {
        result |= max_attacks_bb(original_piece, to);
      } else {
        result |= max_attacks_bb(original_piece, to) & promotion_zone;
      }
    });
  }

  // 3. すでに玉に効きをつけている駒は、候補から除く
  Bitboard already_attacking = min_attacks_bb(original_piece, king_square);
  result = result.andnot(already_attacking);

  // 4. 受け方の玉があるマスは、取り除いておく
  result.reset(king_square);

  return result;
}

} // namespace
