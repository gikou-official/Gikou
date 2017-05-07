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

#ifndef THINKING_H_
#define THINKING_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>
#include "common/arraymap.h"
#include "book.h"
#include "shared_data.h"
#include "signals.h"
#include "thread.h"
#include "time_manager.h"

class Node;
class UsiGoOptions;
class UsiOptions;

/**
 * マシン１台構成の場合に使用される、シンプルなタイムマネージャです.
 */
class SimpleTimeManager : public TimeManager {
 public:
  SimpleTimeManager(const UsiOptions& usi_options, Signals* signals)
      : TimeManager(usi_options),
        signals_(signals) {
  }
  void HandleTimeUpEvent() {
    signals_->stop = true;
  }
 private:
  Signals* const signals_;
};

/**
 * エンジンに最善手を考えさせるためのクラスです.
 * 通常探索のほか、定跡DBの参照や詰み探索なども行い、最善手を決定します。
 */
class Thinking {
 public:
  Thinking(const UsiOptions& usi_options);

  /**
   * 定跡の読み込みや、置換表の確保など、思考部の初期化処理を行います.
   */
  void Initialize();

  /**
   * 新しい対局を行うために必要な処理（置換表の初期化など）を行います.
   */
  void StartNewGame();

  /**
   * シグナルを初期状態にリセットします.
   */
  void ResetSignals();

  /**
   * 特定の局面での最善手を求めるためにエンジンに考えさせます.
   */
  void StartThinking(const Node& root_node, const UsiGoOptions& go_options);

  /**
   * 思考中のエンジンに対して思考を停止するよう指示します.
   */
  void StopThinking();

  /**
   * 予測読みが的中した（ponderhitコマンドが送られてきた）ことをエンジンに伝えます.
   */
  void Ponderhit();

 private:
  const UsiOptions& usi_options_;
  std::mutex mutex_;
  std::condition_variable sleep_condition_;
  Book book_;
  SharedData shared_data_;
  SimpleTimeManager time_manager_;
  ThreadManager thread_manager_;
};

#endif /* THINKING_H_ */
