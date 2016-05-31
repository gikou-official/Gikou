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
#include "task_thread.h"

TaskThread::~TaskThread() {
  // IdleLoop()のループを抜ける
  Exit();

  // スレッドの終了を待つ
  thread_.join();
}

void TaskThread::Exit() {
  // IdleLoop()を終了させる
  mutex_.lock();
  exit_ = true;
  sleep_condition_.notify_one();
  mutex_.unlock();
}

void TaskThread::StartNewThread() {
  // 新しくスレッドを立ち上げる
  thread_ = std::thread([&](){ IdleLoop(); });
}

void TaskThread::WaitForReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  sleep_condition_.wait(lock, [&](){ return ready_.load(); });
}

void TaskThread::ExecuteTask() {
  std::unique_lock<std::mutex> lock(mutex_);
  running_ = true;
  sleep_condition_.notify_one();
}

void TaskThread::WaitUntilTaskIsFinished() {
  std::unique_lock<std::mutex> lock(mutex_);
  sleep_condition_.wait(lock, [&](){ return !running_; });
}

void TaskThread::IdleLoop() {
  // タスクの初期化処理を行う
  Initialize();

  // タスク実行の準備ができたことを知らせる
  std::unique_lock<std::mutex> lock(mutex_);
  ready_ = true;
  sleep_condition_.notify_all();

  // 実行指示が出るたびに、繰り返しタスクを行う
  while (!exit_) {
    // 指示があるまでスリープさせる
    sleep_condition_.wait(lock, [this](){ return exit_ || running_; });
    lock.unlock();

    // スレッド終了の指示が来た場合、ループを抜ける
    if (exit_) {
      break;
    }

    // タスクを実行する
    Run();

    // タスク終了後の処理を行う
    lock.lock();
    running_ = false; // タスク実行中中のフラグをオフにする
    sleep_condition_.notify_all(); // タスク終了を知らせる
    OnTaskFinished(); // タスクの実行が終了した際に呼ばれるコールバック関数
  }
}
