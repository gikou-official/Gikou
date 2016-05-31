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

#ifndef TIME_MANAGER_H_
#define TIME_MANAGER_H_

#include <chrono>
#include <memory>
#include <vector>
#include "task_thread.h"
#include "time_control.h"

class UsiGoOptions;

/**
 * USIエンジンが対局する際に、残り時間を管理するためのクラスです.
 *
 * 時計合わせの影響を受けないように、std::chrono::steady_clockを使って実装されています。
 */
class TimeManager : public TaskThread {
 public:
  TimeManager(const UsiOptions& usi_options);

  /**
   * 時間切れになった時に呼ばれる関数です.
   * 具体的な処理は、TimeManagerクラスを継承した子クラスで実装してください。
   */
  virtual void HandleTimeUpEvent() {}

  /**
   * 時間管理を開始します.
   */
  void StartTimeManagement(const Position& pos, const UsiGoOptions& go_options);

  /**
   * 時間管理を停止します.
   */
  void StopTimeManagement();

  /**
   * USIのponderhitコマンドが到着した時間を記録します.
   */
  void RecordPonderhitTime();

  /**
   * 反復深化の各イテレーション終了時点での、探索ノード数を記録します.
   * ここで記録された探索ノード数のデータは、有効分岐因子の計算に用いられます。
   */
  void RecordNodesSearched(int iteration, uint64_t nodes_searched);

  /**
   * 反復深化の次のイテレーションを回す十分な時間が残っていれば、trueを返します.
   */
  bool EnoughTimeIsAvailableForNextIteration() const;

  /**
   * 思考開始から現在までの「経過時間」をミリ秒で返します.
   * この関数の返す時間には、先読み中に使った時間も含みます。
   */
  int64_t elapsed_time() const;

  /**
   * 今回の思考における「消費時間」をミリ秒で返します.
   * この関数の返す時間は、GetElapsedTime()関数とは異なり、先読み中に使った時間は含まれません。
   */
  int64_t expended_time() const;

  /**
   * fail-lowした場合には、このパニックモードをオンにすることで、思考時間を延長します.
   */
  void set_panic_mode(bool panic_mode) {
    panic_mode_ = panic_mode;
  }

  TimeControl::Stats& stats() {
    return time_control_->stats;
  }

 private:
  void Run();
  const UsiOptions& usi_options_;
  bool ponder_ = false;
  std::unique_ptr<TimeControl> time_control_;
  std::atomic_bool stop_{false};
  std::atomic_bool ponderhit_{false};
  std::atomic_bool panic_mode_{false};
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
  std::chrono::time_point<std::chrono::steady_clock> ponderhit_time_;
  std::mutex mutex_;
  std::condition_variable sleep_condition_;
  std::vector<uint64_t> num_nodes_searched_;
};

#endif /* TIME_MANAGER_H_ */
