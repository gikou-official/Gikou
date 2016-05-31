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

#include "time_manager.h"

#include "signals.h"
#include "usi_protocol.h"

TimeManager::TimeManager(const UsiOptions& usi_options)
    : usi_options_(usi_options) {
  TaskThread::StartNewThread();
}

void TimeManager::StartTimeManagement(const Position& pos,
                                      const UsiGoOptions& go_options) {
  // 思考開始時刻を記録する
  start_time_ = ponderhit_time_ = std::chrono::steady_clock::now();

  // 各種データをリセットする
  num_nodes_searched_.clear();
  panic_mode_ = false;

  // 今回の設定を保存しておく
  ponder_ = go_options.ponder;
  ponderhit_ = false;

  // 時間制御のモードを選択する
  if (   go_options.time[pos.side_to_move()] == 0
      && go_options.byoyomi > 0) {
    // 思考時間固定モード
    time_control_.reset(new FixedTimeControl(pos, go_options, usi_options_));
  } else if (go_options.inc[pos.side_to_move()] > 0) {
    // フィッシャーモード
    time_control_.reset(new FischerTimeControl(pos, go_options, usi_options_));
  } else if (go_options.byoyomi > 0) {
    // 秒読みモード
    time_control_.reset(new ByoyomiTimeControl(pos, go_options, usi_options_));
  } else if (go_options.time[pos.side_to_move()] > 0) {
    // 切れ負けモード
    time_control_.reset(new SuddenDeathTimeControl(pos, go_options, usi_options_));
  } else {
    // その他の場合（時間制御は行われない）
    time_control_.reset(new TimeControl(pos, go_options, usi_options_));
  }

  // 時間無制限のときは、時間管理を行わない
  if (go_options.infinite) {
    return;
  }

  // 別スレッドでの時間制御を開始する
  stop_ = false;
  TaskThread::ExecuteTask();
}

void TimeManager::StopTimeManagement() {
  std::unique_lock<std::mutex> lock(mutex_);
  stop_ = true;
  sleep_condition_.notify_one();
}

void TimeManager::RecordPonderhitTime() {
  ponderhit_time_ = std::chrono::steady_clock::now();
  ponderhit_ = true;
}

int64_t TimeManager::elapsed_time() const {
  auto diff = std::chrono::steady_clock::now() - start_time_;
  return std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
}

int64_t TimeManager::expended_time() const {
  // 先読み中は、消費時間はゼロ
  if (ponder_ && !ponderhit_) {
    return 0;
  }

  // 先読みしていないときは、ponderhitコマンドが到着した時からの時間を計算する
  auto diff = std::chrono::steady_clock::now() - ponderhit_time_;
  return std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
}

void TimeManager::RecordNodesSearched(int iteration, uint64_t nodes_searched) {
  assert(iteration >= 1);

  if (iteration <= (int)num_nodes_searched_.size()) {
    num_nodes_searched_.at(iteration - 1) = nodes_searched; // 上書き
  } else {
    num_nodes_searched_.push_back(nodes_searched); // 新規保存
  }
}

bool TimeManager::EnoughTimeIsAvailableForNextIteration() const {
  // 目標思考時間の何分の１を少なくとも使うか
  const int64_t kMinRatioToTargetTime = 3;

  // まだ１手しか読んでいないときは、次の反復に進む
  int iterations_finished = num_nodes_searched_.size();
  if (iterations_finished <= 1) {
    return true;
  }

  // 最小思考時間を使いきっていない場合は、次の反復に進む
  if (expended_time() < time_control_->minimum_time()) {
    return true;
  }

  // 目標思考時間の一定割合を使い切っていない場合は、思考を打ち切らずに、次の反復に進む
  if (elapsed_time() < time_control_->target_time() / kMinRatioToTargetTime) {
    return true;
  }

  // 有効分岐因子を計算する
  double sum = 1.0, count = 0.0;
  for (int i = std::max(iterations_finished - 4, 2); i <= iterations_finished; ++i) {
    double current_nodes = num_nodes_searched_.at(i - 1);
    double previous_nodes = num_nodes_searched_.at(i - 2);
    sum *= current_nodes / previous_nodes;
    count += 1.0;
  }
  double effective_branching_factor = std::pow(sum, 1.0 / count); // 直近５反復の相乗平均
  assert(effective_branching_factor >= 1.0);

  // 次の反復が終了するまでにかかる時間を、有効分岐因子を用いて推定する
  int64_t estimated_time = int64_t(double(elapsed_time()) * effective_branching_factor);

  // 次の反復が目標時間以内に終わりそうにない場合は、思考の早期打ち切りを行い、次の反復には進まない
  return estimated_time > time_control_->target_time();
}

void TimeManager::Run() {
  while (!stop_) {
    // Step 1. 最小思考時間を下回っているときは、打ち切りを行わない
    if (expended_time() < time_control_->minimum_time()) {
      goto sleep;
    }

    // Step 2. 消費時間ベースの打ち切り
    // 消費時間が最大思考時間を上回ったら思考を直ちに終了する
    if (expended_time() >= time_control_->maximum_time()) {
      HandleTimeUpEvent();
      break;
    }

    // Step 3. 経過時間ベースの打ち切り
    // 経過時間が、目標時間を上回ったら思考を終了する（fail-low時を除く）
    if (   !panic_mode_
        && elapsed_time() >= time_control_->target_time()) {
      HandleTimeUpEvent();
      break;
    }

sleep:
    // 一定時間スリープしてから、再度時間をチェックする
    std::unique_lock<std::mutex> lock(mutex_);
    if (!stop_) {
      sleep_condition_.wait_for(lock, std::chrono::milliseconds(5));
    }
  }
}
