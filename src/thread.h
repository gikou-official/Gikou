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

#ifndef THREAD_H_
#define THREAD_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "node.h"
#include "search.h"

class ThreadManager;
class TimeManager;
class ThinkingConditions;
class UsiGoOptions;

/**
 * LazySMPの探索を担当するスレッドです.
 */
class SearchThread {
 public:
  SearchThread(size_t thread_id, SharedData& shared_data,
               ThreadManager& thread_manager);
  ~SearchThread();
  void IdleLoop();
  void SetRootNode(const Node& node);
  void StartSearching();
  void WaitUntilSearchIsFinished();
 private:
  friend class ThreadManager;
  ThreadManager& thread_manager_;
  Node root_node_;
  Search search_;
  std::mutex mutex_;
  std::condition_variable sleep_condition_;
  std::atomic_bool searching_, exit_;
  std::thread native_thread_;
};

/**
 * LazySMPに使用するスレッドを管理するためのクラスです.
 */
class ThreadManager {
 public:
  ThreadManager(SharedData& shared_data, TimeManager& time_manager);
  TimeManager& time_manager() {
    return time_manager_;
  }
  void SetNumSearchThreads(size_t num_threads);
  uint64_t CountNodesSearchedByWorkerThreads() const;
  uint64_t CountNodesUnder(Move move) const;
  RootMove ParallelSearch(Node& node, Score draw_score,
                          const UsiGoOptions& go_options,
                          int multipv);
 private:
  SharedData& shared_data_;
  TimeManager& time_manager_;
  std::vector<std::unique_ptr<SearchThread>> worker_threads_;
};

#endif /* THREAD_H_ */
