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

#include "time_control.h"

#include "movegen.h"
#include "node.h"
#include "signals.h"
#include "synced_printf.h"
#include "usi_protocol.h"

TimeControl::TimeControl(const Position& position,
                         const UsiGoOptions& go_options,
                         const UsiOptions& usi_options)
    : position_(position),
      go_options_(go_options),
      usi_options_(usi_options),
      num_legal_moves_(SimpleMoveList<kAllMoves, true>(position).size()) {
}

int64_t TimeControl::time_per_move() const {
  int64_t inc = go_options_.inc[position_.side_to_move()];
  int64_t byoyomi = go_options_.byoyomi;
  return std::max(inc, byoyomi);
}

int64_t TimeControl::remaining_time() const {
  if (go_options_.infinite) {
    return INT64_MAX;
  } else {
    int64_t time = go_options_.time[position_.side_to_move()];
    return time + time_per_move();
  }
}

FixedTimeControl::FixedTimeControl(const Position& position,
                                   const UsiGoOptions& go_options,
                                   const UsiOptions& usi_options)
    : TimeControl(position, go_options, usi_options) {
  // 常に固定された時間だけ考える
  fixed_time_ = time_per_move() - usi_options["ByoyomiMargin"];
  fixed_time_ = std::max(fixed_time_, INT64_C(10)); // 不具合を避けるため、思考時間を10ミリ秒未満にしない
}

DynamicTimeControl::DynamicTimeControl(const Position& position,
                                       const UsiGoOptions& go_options,
                                       const UsiOptions& usi_options)
    : TimeControl(position, go_options, usi_options),
      byoyomi_margin_(usi_options_["ByoyomiMargin"]) {
}

int64_t DynamicTimeControl::maximum_time() const {
  // 合法手が１手しかない場合は、思考の早期打ち切りを行う。
  //（合法手が１手のときは、先読みすべき手さえ分かれば良いため）
  if (   num_legal_moves_ == 1
      && stats.num_iterations_finished >= 2) {
    return minimum_time();
  }

  return maximum_time_;
}

int64_t DynamicTimeControl::target_time() const {
  // 1. 単体で探索している場合の時間制御
  // 最善手以下の探索ノード数が不十分な場合には、思考時間を延長する
  double insufficiency = std::max(1.0, stats.search_insufficiency);
  double target = static_cast<double>(base_time_) * insufficiency;

  // 2. 合議中の場合の時間制御
  if (stats.agreement_rate > 0.0) {
    // ワーカーの指し手の一致率が低いと、そのぶん分母が小さくなり、思考時間が延長される
    target /= stats.agreement_rate;
  }

  // 3. 先読み中は、思考時間を25%延長する
  if (go_options_.ponder) {
    target += target * 0.25;
  }

  return static_cast<int64_t>(target) - byoyomi_margin_;
}

FischerTimeControl::FischerTimeControl(const Position& position,
                                       const UsiGoOptions& go_options,
                                       const UsiOptions& usi_options)
    : DynamicTimeControl(position, go_options, usi_options) {
  // 時間制御を調整するためのパラメータ
  const int64_t kHorizon = 45;
  const int64_t kMaxRatio = 5;
  const int64_t kMinRatio = 3;

  // 目標思考時間の基礎となる時間
  int64_t fischer_margin = usi_options_["FischerMargin"];
  int64_t remaining = std::max(INT64_C(0), remaining_time() - fischer_margin);
  base_time_ = (remaining / kHorizon) + time_per_move();

  // 最大思考時間
  maximum_time_ = std::min(base_time_ * kMaxRatio, remaining) - byoyomi_margin_;

  // 最小思考時間
  int64_t min_thinking_time = usi_options_["MinThinkingTime"];
  minimum_time_ = std::max(base_time_ / kMinRatio, min_thinking_time) - byoyomi_margin_;
  minimum_time_ = std::max(minimum_time_, INT64_C(10)); // 不具合を避けるため、最小思考時間を10ミリ秒未満にしない

  SYNCED_PRINTF("info string Time Control: base=%" PRIu64 " max=%" PRIu64 " min=%" PRIu64 "\n",
                base_time_, maximum_time_, minimum_time_);
}

ByoyomiTimeControl::ByoyomiTimeControl(const Position& position,
                                       const UsiGoOptions& go_options,
                                       const UsiOptions& usi_options)
    : DynamicTimeControl(position, go_options, usi_options) {
  // 時間制御を調整するためのパラメータ
  const int64_t kHorizon = 35;
  const int64_t kMaxRatio = 5;
  const int64_t kMinRatio = 3;

  // 目標思考時間の基礎となる時間
  int64_t remaining = std::max(INT64_C(0), remaining_time());
  base_time_ = (remaining / kHorizon) + time_per_move();

  // 最大思考時間
  maximum_time_ = std::min(base_time_ * kMaxRatio, remaining) - byoyomi_margin_;

  // 最小思考時間
  int64_t min_thinking_time = usi_options_["MinThinkingTime"];
  minimum_time_ = std::max(base_time_ / kMinRatio, min_thinking_time) - byoyomi_margin_;
  minimum_time_ = std::max(minimum_time_, INT64_C(10)); // 不具合を避けるため、最小思考時間を10ミリ秒未満にしない

  SYNCED_PRINTF("info string Time Control: base=%" PRIu64 " max=%" PRIu64 " min=%" PRIu64 "\n",
                base_time_, maximum_time_, minimum_time_);
}

SuddenDeathTimeControl::SuddenDeathTimeControl(const Position& position,
                                               const UsiGoOptions& go_options,
                                               const UsiOptions& usi_options)
    : DynamicTimeControl(position, go_options, usi_options) {
  // 時間制御を調整するためのパラメータ
  const int64_t kHorizon = 35;
  const int64_t kMaxRatio = 5;
  const int64_t kMinRatio = 3;

  // 目標思考時間の基礎となる時間
  int64_t sudden_death_margin = INT64_C(1000) * usi_options["SuddenDeathMargin"]; // 秒単位に変換
  int64_t remaining = std::max(INT64_C(0), remaining_time() - sudden_death_margin);
  base_time_ = std::max(INT64_C(1), remaining / kHorizon);

  // 最大思考時間
  maximum_time_ = std::min(base_time_ * kMaxRatio, remaining) - byoyomi_margin_;

  // 最小思考時間
  int64_t min_thinking_time = usi_options_["MinThinkingTime"];
  minimum_time_ = std::max(base_time_ / kMinRatio, min_thinking_time) - byoyomi_margin_;
  minimum_time_ = std::max(minimum_time_, INT64_C(10)); // 不具合を避けるため、最小思考時間を10ミリ秒未満にしない

  SYNCED_PRINTF("info string Time Control: base=%" PRIu64 " max=%" PRIu64 " min=%" PRIu64 "\n",
                base_time_, maximum_time_, minimum_time_);
}
