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

#ifndef TASK_THREAD_H_
#define TASK_THREAD_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

/**
 * 繰り返しタスクを実行するためのスレッドです.
 */
class TaskThread {
 public:
  virtual ~TaskThread();

  /**
   * タスク実行前の初期化処理を行います.
   * 実際の処理は、子クラスで実装してください。
   */
  virtual void Initialize() {}

  /**
   * 実行対象のタスクです.
   * 実際に実行するタスクは、子クラスで実装してください。
   */
  virtual void Run() {}

  /**
   * タスクの実行が終了した際に呼ばれるコールバック関数です.
   * 実際に実行すべき処理は、子クラスで実装してください。
   */
  virtual void OnTaskFinished() {}

  /**
   * 新規スレッドを立ち上げます.
   */
  void StartNewThread();

  /**
   * タスクを実行する準備ができるまで、スレッドを待機します.
   */
  void WaitForReady();

  /**
   * タスクを実行します.
   */
  void ExecuteTask();

  /**
   * タスクの実行が終了するまで、スレッドを待機します.
   */
  void WaitUntilTaskIsFinished();

  /**
   * 立ち上げたスレッドを終了します.
   */
  void Exit();

  /**
   * タスクを実行中である場合は、trueを返します.
   */
  bool is_running() const {
    return running_;
  }

 private:
  void IdleLoop();
  std::mutex mutex_;
  std::condition_variable sleep_condition_;
  std::atomic_bool exit_{false}, ready_{false}, running_{false};
  std::thread thread_;
};

#endif /* TASK_THREAD_H_ */
