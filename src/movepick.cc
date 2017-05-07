/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
 *
 * The MovePicker class below is derived from Stockfish 6.
 * Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 * Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish author)
 * Copyright (C) 2016-2017 Yosuke Demura
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

#include "movepick.h"

#include "material.h"
#include "move_probability.h"
#include "position.h"
#include "swap.h"

namespace {

enum Stage {
  kProbSearch,       kProbability0,
  kMainSearch,       kCaptures1, kKillers1, kGoodQuiets1, kQuiets1, kBadCaptures1,
  kEvasion,          kEvasions2,
  kQuiecenceSearch0, kCaptures3, kQuietChecks3,
  kQuiecenceSearch1, kCaptures4,
  kQuiecenceSearch2, kRecaptures5,
  kProbCut,          kCaptures6,
  kStop,
};

inline bool is_less(const ExtMove& lhs, const ExtMove& rhs) {
  return lhs.score < rhs.score;
}

inline bool is_greater(const ExtMove& lhs, const ExtMove& rhs) {
  return lhs.score > rhs.score;
}

inline bool has_good_score(const ExtMove& em) {
  return em.score > 0;
}

inline ExtMove* PickBest(ExtMove* begin, ExtMove* end) {
  std::swap(*begin, *std::max_element(begin, end, is_less));
  return begin;
}

inline void SortMoves(ExtMove* begin, ExtMove* end) {
  return std::sort(begin, end, is_greater); // 降順でソートする
}

inline Score GetMvvLvaScore(Move move) {
  Score victim = Material::exchange_value(move.captured_piece_type());
  Score aggressor = Material::exchange_order(move.piece_type());
  assert(aggressor < 16);
  return victim - aggressor;
}

} // namespace

MovePicker::MovePicker(const Position& pos, const HistoryStats& history,
                       const GainsStats& gains, Depth depth, Move hash_move,
                       const Array<Move, 2>& killermoves,
                       const Array<Move, 2>& countermoves,
                       const Array<Move, 2>& followupmoves,
                       Search::Stack* const ss)
    : pos_(pos),
      history_(history),
      gains_(gains),
      ss_(ss),
      depth_(depth),
      killermoves_(killermoves),
      countermoves_(countermoves),
      followupmoves_(followupmoves) {
  assert(hash_move.IsOk());
  assert(depth > kDepthZero);
  assert(ss != nullptr);

  // ポインタを初期化する
  cur_ = moves_.begin();
  end_ = moves_.begin();
  end_bad_captures_ = moves_.end() - 1;

  // 指し手生成のカテゴリをセットする
  if (depth >= MoveProbability::kAppliedDepth) {
    stage_ = kProbSearch;
  } else if (pos.in_check()) {
    stage_ = kEvasion;
  } else {
    stage_ = kMainSearch;
  }

  // ハッシュ手をセットする
  if (hash_move != kMoveNone && pos.MoveIsPseudoLegal(hash_move)) {
    hash_move_ = hash_move;
    ++end_;
  }
}

MovePicker::MovePicker(const Position& pos, const HistoryStats& history,
                       const GainsStats& gains, Depth depth, Move hash_move)
    : pos_(pos),
      history_(history),
      gains_(gains),
      depth_(depth) {
  assert(hash_move.IsOk());
  assert(depth <= kDepthZero);

  // ポインタを初期化する
  cur_ = moves_.begin();
  end_ = moves_.begin();

  // 指し手生成のカテゴリをセットする
  if (pos.in_check()) {
    stage_ = kEvasion;
  } else if (depth > kDepthQsNoChecks) {
    stage_ = kQuiecenceSearch0;
  } else if (depth > kDepthQsRecaptures) {
    stage_ = kQuiecenceSearch1;
    if (hash_move != kMoveNone && hash_move.is_quiet()) {
      hash_move = kMoveNone;
    }
  } else {
    stage_ = kQuiecenceSearch2;
    hash_move = kMoveNone;
  }

  // ハッシュ手をセットする
  if (hash_move != kMoveNone && pos.MoveIsPseudoLegal(hash_move)) {
    hash_move_ = hash_move;
    ++end_;
  }
}

MovePicker::MovePicker(const Position& pos, const HistoryStats& history,
                       const GainsStats& gains, Move hash_move,
                       Score capture_threshold)
    : pos_(pos),
      history_(history),
      gains_(gains),
      stage_(kProbCut) {
  // ポインタを初期化する
  cur_ = moves_.begin();
  end_ = moves_.begin();

  // 取る手のしきい値をセットする
  capture_threshold_ = capture_threshold;

  // ハッシュ手をセットする
  if (   hash_move != kMoveNone
      && pos.MoveIsPseudoLegal(hash_move)
      && hash_move.is_capture()
      && Swap::Evaluate(hash_move, pos) > capture_threshold_) {
    hash_move_ = hash_move;
    ++end_;
  }
}

Move MovePicker::NextMove(double* const probability) {
  while (true) {
    // スタックに指し手がなければ、次のカテゴリの指し手を生成する
    while (cur_ == end_) {
      GenerateNext();
    }
    Move move;
    switch (stage_) {
      case kProbSearch:
      case kMainSearch:
      case kEvasion:
      case kQuiecenceSearch0:
      case kQuiecenceSearch1:
      case kProbCut:
        ++cur_;
        return hash_move_;

      case kProbability0: {
        move = cur_->move;
        int32_t score = cur_->score;
        cur_++;
        if (move != hash_move_) {
          if (probability != nullptr) {
            *probability = double(score) / double(1 << 30);
          }
          return move;
        }
        break;
      }

      case kCaptures1:
        move = PickBest(cur_++, end_)->move;
        if (move != hash_move_) {
          if (!Swap::IsLosing(move, pos_)) {
            return move;
          } else {
            (end_bad_captures_--)->move = move;
          }
        }
        break;

      case kKillers1:
        move = (cur_++)->move;
        if (   move != kMoveNone
            && move != hash_move_
            && pos_.MoveIsPseudoLegal(move)
            && move.is_quiet()) {
          return move;
        }
        break;

      case kGoodQuiets1:
      case kQuiets1:
        move = (cur_++)->move;
        if (   move != hash_move_
            && move != killers_[0].move
            && move != killers_[1].move
            && move != killers_[2].move
            && move != killers_[3].move
            && move != killers_[4].move
            && move != killers_[5].move) {
          return move;
        }
        break;

      case kBadCaptures1:
        return (cur_--)->move;

      case kEvasions2:
      case kCaptures3:
      case kCaptures4:
        move = PickBest(cur_++, end_)->move;
        if (move != hash_move_) {
          return move;
        }
        break;

      case kRecaptures5:
        return PickBest(cur_++, end_)->move;

      case kCaptures6:
        move = PickBest(cur_++, end_)->move;
        if (   move != hash_move_
            && Swap::Evaluate(move, pos_) > capture_threshold_) {
          return move;
        }
        break;

      case kQuietChecks3:
        move = (cur_++)->move;
        if (move != hash_move_) {
          return move;
        }
        break;

      case kStop:
        return kMoveNone;

      default:
        assert(0);
        return kMoveNone;
    }
  }

  assert(0);
  return kMoveNone;
}

template<>
void MovePicker::ScoreMoves<kCaptures>() {
  for (ExtMove* it = moves_.begin(); it != end_; ++it) {
    Move move = it->move;
    it->score = GetMvvLvaScore(move);
    if (move.is_promotion()) {
      it->score += Material::promotion_value(move.piece_type());
    }
  }
}

template<>
void MovePicker::ScoreMoves<kQuiets>() {
  HistoryStats* cmh = (ss_-1)->countermoves_history;
  HistoryStats* fmh = (ss_-2)->countermoves_history;
  for (ExtMove* it = moves_.begin(); it != end_; ++it) {
    Move move = it->move;
    if (cmh != nullptr) {
      it->score = history_[move] + (*cmh)[move];
    } else if (fmh != nullptr) {
      it->score = history_[move] + (*fmh)[move];
    } else {
      it->score = history_[move];
    }
  }
}

template<>
void MovePicker::ScoreMoves<kEvasions>() {
  for (ExtMove* it = moves_.begin(); it != end_; ++it) {
    Move move = it->move;
    Score swap_score = Swap::Evaluate(move, pos_);
    if (swap_score < kScoreZero) {
      it->score = swap_score - HistoryStats::kMax; // 末尾に
    } else if (!move.is_quiet()) {
      it->score = GetMvvLvaScore(move) + HistoryStats::kMax; // 先頭に
      it->score += move.is_promotion();
    } else {
      it->score = history_[move];
    }
  }
}

void MovePicker::GenerateNext() {
  switch (++stage_) {
    case kProbability0: {
      cur_ = moves_.begin();
      end_ = GenerateMoves<kAllMoves>(pos_, cur_);
      end_ = RemoveIllegalMoves(pos_, cur_, end_);
      size_t num_moves = end_ - cur_;

      // 指し手の実現確率を計算する
      const HistoryStats* cmh = (ss_-1)->countermoves_history;
      const HistoryStats* fmh = (ss_-2)->countermoves_history;
      std::valarray<double> probabilities = MoveProbability::ComputeProbabilitiesWithCache(
          pos_, history_, gains_, cmh, fmh);

      // 指し手の実現確率が高い順にソートする
      for (size_t i = 0; i < num_moves; ++i) {
        (cur_ + i)->score = static_cast<int>(double(1 << 30) * probabilities[i]);
      }
      SortMoves(cur_, end_);
      return;
    }

    case kCaptures1:
    case kCaptures3:
    case kCaptures4:
    case kCaptures6:
      cur_ = moves_.begin();
      end_ = GenerateMoves<kCaptures>(pos_, cur_);
      ScoreMoves<kCaptures>();
      return;

    case kKillers1:
      cur_ = killers_.begin();
      end_ = cur_ + 2;

      // 1. キラー手を追加する
      killers_.clear();
      killers_[0].move = killermoves_[0];
      killers_[1].move = killermoves_[1];

      // 2. カウンター手を追加する
      for (Move countermove : countermoves_) {
        if (   countermove != (cur_+0)->move
            && countermove != (cur_+1)->move) {
          (end_++)->move = countermove;
        }
      }

      // 3. フォローアップ手を追加する
      for (Move followupmove : followupmoves_) {
        if (   followupmove != (cur_+0)->move
            && followupmove != (cur_+1)->move
            && followupmove != (cur_+2)->move
            && followupmove != (cur_+3)->move) {
          (end_++)->move = followupmove;
        }
      }
      return;

    case kGoodQuiets1:
      cur_ = moves_.begin();
      end_ = end_quiets_ = GenerateMoves<kQuiets>(pos_, cur_);
      ScoreMoves<kQuiets>();
      end_ = std::partition(cur_, end_, has_good_score);
      SortMoves(cur_, end_);
      return;

    case kQuiets1:
      cur_ = end_;
      end_ = end_quiets_;
      if (depth_ >= 3 * kOnePly) {
        SortMoves(cur_, end_);
      }
      return;

    case kBadCaptures1:
      cur_ = moves_.end() - 1;
      end_ = end_bad_captures_;
      return;

    case kEvasions2:
      cur_ = moves_.begin();
      end_ = GenerateMoves<kEvasions>(pos_, cur_);
      if (end_ - cur_ > 1) {
        ScoreMoves<kEvasions>();
      }
      return;

    case kQuietChecks3:
      cur_ = moves_.begin();
      end_ = GenerateMoves<kQuietChecks>(pos_, cur_);
      return;

    case kRecaptures5:
      cur_ = moves_.begin();
      end_ = GenerateMoves<kRecaptures>(pos_, cur_);
      ScoreMoves<kCaptures>();
      return;

    case kMainSearch:
    case kEvasion:
    case kQuiecenceSearch0:
    case kQuiecenceSearch1:
    case kQuiecenceSearch2:
    case kProbCut:
      stage_ = kStop;
      /* no break */

    case kStop:
      end_ = cur_ + 1; // GenerateNext()の呼び出しを止める
      return;

    default:
      assert(false);
  }
}
