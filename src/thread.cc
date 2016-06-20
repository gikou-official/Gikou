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

#include "thread.h"

#include "thinking.h"
#include "time_manager.h"
#include "usi_protocol.h"

SearchThread::SearchThread(size_t thread_id, SharedData& shared_data,
                           ThreadManager& thread_manager)
    : thread_manager_(thread_manager),
      root_node_(Position::CreateStartPosition()),
      search_(shared_data, thread_id),
      searching_{false},
      exit_{false},
      native_thread_([&](){ IdleLoop(); }) {
  // マスタースレッドではなく、ワーカースレッドに限る
  assert(!search_.is_master_thread());
}

SearchThread::~SearchThread() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    exit_ = true;
    sleep_condition_.notify_one();
  }
  native_thread_.join();
}

void SearchThread::IdleLoop() {
  while (!exit_) {
    // exit_ か searching_ が true になるまでスリープする
    {
      std::unique_lock<std::mutex> lock(mutex_);
      sleep_condition_.wait(lock, [this](){ return exit_ || searching_; });
    }

    if (exit_) {
      break;
    }

    search_.IterativeDeepening(root_node_, thread_manager_);

    // 探索終了後の処理
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // searching_フラグを元に戻しておく
      searching_ = false;
      // WaitUntilSearchFinished()で待機している場合があるので、探索終了を知らせる必要がある
      sleep_condition_.notify_one();
    }
  }
}

void SearchThread::SetRootNode(const Node& root_node) {
  root_node_ = root_node;
}

void SearchThread::StartSearching() {
  std::unique_lock<std::mutex> lock(mutex_);
  searching_ = true;
  sleep_condition_.notify_one();
}

void SearchThread::WaitUntilSearchIsFinished() {
  std::unique_lock<std::mutex> lock(mutex_);
  sleep_condition_.wait(lock, [this](){ return !searching_; });
}

ThreadManager::ThreadManager(SharedData& shared_data, TimeManager& time_manager)
    : shared_data_(shared_data),
      time_manager_(time_manager) {
}

void ThreadManager::SetNumSearchThreads(size_t num_search_threads) {
  // 必要なワーカースレッドの数を求める（１を引いているのは、マスタースレッドの分。）
  size_t num_worker_threads = num_search_threads - 1;

  // ワーカースレッドを増やす場合
  while (num_worker_threads > worker_threads_.size()) {
    size_t thread_id = worker_threads_.size() + 1; // ワーカースレッドのIDは1から始める
    worker_threads_.emplace_back(new SearchThread(thread_id, shared_data_, *this));
  }

  // ワーカースレッドを減らす場合
  while (num_worker_threads < worker_threads_.size()) {
    worker_threads_.pop_back();
  }
}

uint64_t ThreadManager::CountNodesSearchedByWorkerThreads() const {
  uint64_t total = 0;
  for (const std::unique_ptr<SearchThread>& worker : worker_threads_) {
    total += worker->search_.num_nodes_searched();
  }
  return total;
}

uint64_t ThreadManager::CountNodesUnder(Move move) const {
  uint64_t total = 0;
  for (const std::unique_ptr<SearchThread>& worker : worker_threads_) {
    total += worker->search_.GetNodesUnder(move);
  }
  return total;
}

RootMove ThreadManager::ParallelSearch(Node& node, const Score draw_score,
                                       const UsiGoOptions& go_options,
                                       const int multipv) {
  // goコマンドのsearchmovesとignoremovesオプションから、
  // ルート局面で探索すべき手を列挙する
  const std::vector<RootMove> root_moves = Search::CreateRootMoves(
      node, go_options.searchmoves, go_options.ignoremoves);

  // ワーカースレッドの探索を開始する
  for (std::unique_ptr<SearchThread>& worker : worker_threads_) {
    worker->SetRootNode(node);
    worker->search_.set_draw_scores(node.side_to_move(), draw_score);
    worker->search_.set_root_moves(root_moves);
    worker->search_.set_multipv(multipv);
    worker->search_.set_limit_nodes(go_options.nodes);
    worker->search_.set_limit_depth(go_options.depth);
    worker->search_.PrepareForNextSearch();
    worker->StartSearching();
  }

  // マスタースレッドの探索を開始する
  Search master_search(shared_data_);
  master_search.set_draw_scores(node.side_to_move(), draw_score);
  master_search.set_root_moves(root_moves);
  master_search.set_multipv(multipv);
  master_search.set_limit_nodes(go_options.nodes);
  master_search.set_limit_depth(go_options.depth);
  master_search.PrepareForNextSearch();
  master_search.IterativeDeepening(node, *this);

  // ワーカースレッドの終了を待つ
  for (std::unique_ptr<SearchThread>& worker : worker_threads_) {
    worker->WaitUntilSearchIsFinished();
  }

  // 最善手と、相手の予想手を取得する
  const RootMove& best_root_move = master_search.GetBestRootMove();
  return best_root_move;
}
