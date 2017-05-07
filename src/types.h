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

#ifndef TYPES_H_
#define TYPES_H_

#include <cstdint>
#include "common/bitset.h"
#include "common/limits.h"
#include "common/operators.h"

// 通常の対局には不要の開発用コードを無効化するためのマクロ
// （評価関数の学習等を行う場合はコメントアウトしてください）
//#define MINIMUM

// GCC特有の属性を付けるマクロ
#ifdef __GNUC__
# define FORCE_INLINE inline __attribute__((always_inline))
# define UNROLL_LOOPS __attribute__((optimize("unroll-loops")))
#else
# define FORCE_INLINE inline
# define UNROLL_LOOPS
#endif

constexpr int kMaxPly = 100;

enum Color {
  kBlack = 0, /// "sente" in Japanese
  kWhite = 1  /// "gote" in Japanese
};

enum File {
  kFile1 = 0, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9
};

enum Rank {
  // Numeric notations
  kRank1 = 0, kRank2, kRank3, kRank4, kRank5, kRank6, kRank7, kRank8, kRank9,
  // SFEN style notations
  kRankA = 0, kRankB, kRankC, kRankD, kRankE, kRankF, kRankG, kRankH, kRankI
};

enum Direction {
  kDirNE  = 0, /// North East
  kDirE   = 1, /// East
  kDirSE  = 2, /// South East
  kDirN   = 3, /// North
  kDirS   = 4, /// South
  kDirNW  = 5, /// North West
  kDirW   = 6, /// West
  kDirSW  = 7  /// South West
};

enum Score {
  kScoreFoul     = -32400,
  kScoreZero     =      0,
  kScoreDraw     =      0,
  kScoreMaxEval  =  30000,
  kScoreSuperior =  30500,
  kScoreKnownWin =  31000,
  kScoreMate     =  32000,
  kScoreInfinite =  32600,
  kScoreNone     =  32601,
  kScoreMateInMaxPly  = +kScoreMate - kMaxPly,
  kScoreMatedInMaxPly = -kScoreMate + kMaxPly,
};

enum Depth {
  kDepthZero = 0,
  kOnePly    = 64,
  kDepthNone = -127 * kOnePly,
};

enum Bound {
  kBoundNone  = 0,
  kBoundUpper = 1,
  kBoundLower = 1 << 1,
  kBoundExact = kBoundUpper | kBoundLower
};

typedef BitSet<Direction, 8> DirectionSet;

/**
 * Zobristハッシュに用いられる、32ビットのハッシュキーです.
 */
typedef uint32_t Key32;

/**
 * Zobristハッシュに用いられる、64ビットのハッシュキーです.
 * ネイティブの整数型と比較すると、使用できる演算子が限定されています.
 */
class Key64
{
 public:
  explicit Key64(int64_t key = 0) : key_(key) {}
  operator int64_t() const { return static_cast<int64_t>(key_); }
  bool operator==(Key64 rhs) const { return key_ == rhs.key_; }
  bool operator!=(Key64 rhs) const { return key_ != rhs.key_; }
  Key64 operator+() const { return Key64( key_); }
  Key64 operator-() const { return Key64(-key_); }
  Key64& operator+=(Key64 rhs) { key_ += rhs.key_; return *this; }
  Key64& operator-=(Key64 rhs) { key_ -= rhs.key_; return *this; }
  Key64 operator+(Key64 rhs) const { return Key64(*this) += rhs; }
  Key64 operator-(Key64 rhs) const { return Key64(*this) -= rhs; }
  Key32 ToKey32() const {
    return static_cast<Key32>(static_cast<uint64_t>(key_) >> 32);
  }
 protected:
  int64_t key_;
};

ENABLE_ADD_AND_SUBTRACT_OPERATORS(File)
ENABLE_ADD_AND_SUBTRACT_OPERATORS(Rank)
ENABLE_ARITHMETIC_OPERATORS(Score)
ENABLE_ARITHMETIC_OPERATORS(Depth)

DEFINE_SPECIALIZED_LIMITS_CLASS_FOR_ENUM(Color, kBlack, kWhite)
DEFINE_SPECIALIZED_LIMITS_CLASS_FOR_ENUM(File, kFile1, kFile9)
DEFINE_SPECIALIZED_LIMITS_CLASS_FOR_ENUM(Rank, kRank1, kRank9)
DEFINE_SPECIALIZED_LIMITS_CLASS_FOR_ENUM(Direction, 0, 7)

constexpr Color operator~(Color c) {
  return static_cast<Color>(c ^ 1);
}

inline char color_to_char(Color c) {
  return c == kBlack ? 'b' : 'w';
}

inline char file_to_char(File f) {
  assert(f >= kFile1 && f <= kFile9);
  return static_cast<char>(static_cast<int>(f - kFile1) + '1');
}

inline File file_from_char(char c) {
  assert(c >= '1' && c <= '9');
  return kFile1 + static_cast<File>(c - '1');
}

inline char rank_to_char(Rank r) {
  assert(r >= kRank1 && r <= kRank9);
  return static_cast<char>(static_cast<int>(r - kRank1) + 'a');
}

inline Rank rank_from_char(char c) {
  assert(c >= 'a' && c <= 'i');
  return kRank1 + static_cast<Rank>(c - 'a');
}

constexpr Rank relative_rank(Color c, Rank r) {
  return c == kBlack ? r : kRank9 - r;
}

constexpr Direction inverse_direction(Direction d) {
  return static_cast<Direction>(7 - d);
}

constexpr Direction mirror_horizontally(Direction d) {
  return d == kDirNE ? kDirNW :
         d == kDirE  ? kDirW  :
         d == kDirSE ? kDirSW :
         d == kDirN  ? kDirN  :
         d == kDirS  ? kDirS  :
         d == kDirNW ? kDirNE :
         d == kDirW  ? kDirE  :
       /*d == kDirSW*/ kDirSE;
}

inline Score score_mate_in(int ply) {
  assert(0 <= ply && ply <= kMaxPly);
  return kScoreMate - ply;
}

inline Score score_mated_in(int ply) {
  assert(0 <= ply && ply <= kMaxPly);
  return -kScoreMate + ply;
}

#endif /* TYPES_H_ */
