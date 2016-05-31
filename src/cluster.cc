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

#if !defined(MINIMUM)

#include "cluster.h"

#include <sstream>
#include "book.h"
#include "movegen.h"
#include "synced_printf.h"

namespace {

Book g_book;

}

ClusterWorker::ClusterWorker(size_t worker_id, Cluster& cluster)
    : worker_id_(worker_id),
      cluster_(cluster) {
}

ClusterWorker::~ClusterWorker() {
  // ワーカにquitコマンドを送信
  SendCommand("quit");

  // 外部プロセスの終了を待つ
  external_process_.WaitFor();
}

void ClusterWorker::Initialize() {
  // 1. 外部プロセスを使い、USIエンジンを起動する
  if (worker_id_ == cluster_.master_worker_id()) {
    // a. マスターの起動
    char* const args[] = {
        const_cast<char*>("./release"),
        NULL
    };
    if (external_process_.StartProcess(args[0], args) < 0) {
      std::perror("StartProcess()\n");
      return;
    }
  } else {
    // b. ワーカーの起動
    std::string worker_name = "worker-" + std::to_string(worker_id_);
    char* const args[] = {
#if 0
        // リモートマシンをワーカーとして使用する場合: SSHで通信する
        const_cast<char*>("ssh"),
        const_cast<char*>(worker_name.c_str()), // ~/.ssh/configでワーカーのホスト名を設定しておく
        const_cast<char*>("cd gikou/bin; ./release"), // ディレクトリを移動後、実行する
#else
        // ローカルマシンをワーカーとして使用する場合: パイプで通信する
        const_cast<char*>("./release"),
#endif
        NULL
    };
    if (external_process_.StartProcess(args[0], args) < 0) {
      std::perror("StartProcess()\n");
      return;
    }
  }

  // 2. 対局準備のため、USIコマンドをエンジンに送信する
  SendCommand("usi");
  const UsiOptions& options = cluster_.usi_options(); // オプションの一部を下流に伝達する
  SendCommand("setoption name USI_Hash value %d", (int)options["USI_Hash"]);
  SendCommand("setoption name Threads value %d", (int)options["Threads"]);
  SendCommand("setoption name DrawScore value %d", (int)options["DrawScore"]);
  SendCommand("isready");

  // 3. readyokが送られてくるまで待機する
  for (std::string line; RecieveCommand(&line); ) {
    if (line == "readyok") break;
  }
}

void ClusterWorker::Run() {
  // bestmoveコマンドが帰ってくるまで探索する
  for (std::string line; RecieveCommand(&line); ) {
    std::istringstream is(line);
    std::string token;
    is >> token;
    if (token == "info") {
      UsiInfo info = UsiProtocol::ParseInfoCommand(is);
      cluster_.UpdateInfo(worker_id_, info);
    } else if (token == "bestmove") {
      break;
    }
  }
}

Cluster::Cluster()
    : UsiProtocol("Gikou Cluster", "Yosuke Demura") {
}

void Cluster::OnIsreadyCommandEntered() {
  // 定跡ファイルを読み込む
  g_book.ReadFromFile("book.bin");

  // ワーカを必要なだけ起動する
  if (workers_.empty()) {
    // 初回は別プロセスを立ち上げる
    for (size_t worker_id = 0; worker_id < num_workers_; ++worker_id) {
      ClusterWorker* engine = new ClusterWorker(worker_id, *this);
      engine->StartNewThread();
      workers_.emplace_back(engine);
    }

    // ワーカの準備ができるまで待機する
    for (std::unique_ptr<ClusterWorker>& worker : workers_) {
      worker->WaitForReady();
    }
  } else {
    // 次回以降は、既に起動しているプロセスを使い回す
    SendCommandToAllWorkers("isready");

    // readyokコマンドを送り返して来るまで待機する
    for (std::unique_ptr<ClusterWorker>& worker : workers_) {
      for (std::string line; worker->RecieveCommand(&line); ) {
        if (line == "readyok") break;
      }
    }
  }

  // ワーカの準備ができたら、readyokコマンドを返す
  SYNCED_PRINTF("readyok\n");
}

void Cluster::OnUsinewgameCommandEntered() {
  SendCommandToAllWorkers("usinewgame");
}

void Cluster::OnGoCommandEntered(const UsiGoOptions& go_options) {
  // 上位N手を調べるための浅い探索に用いる時間（ミリ秒で指定）
  const int kShallowSearchTime = 300; // 2015年版YSSと同じ設定

  // 1. 合法手の数を調べる
  SimpleMoveList<kAllMoves, true> legal_moves(root_node());
  size_t num_legal_moves = legal_moves.size();

  // 2. 合法手の数が１手以下であれば、探索は不要
  if (num_legal_moves == 0) {
    SYNCED_PRINTF("bestmove resign\n");
    return;
  } else if (num_legal_moves == 1) {
    SYNCED_PRINTF("bestmove %s\n", legal_moves[0].move.ToSfen().c_str());
    return;
  }

  // 3. 時間制限がある場合は、定跡を使うことを考える
  if (   !go_options.infinite
      && !go_options.ponder
      && usi_options()["OwnBook"]
      && root_node().game_ply() + 1 <= usi_options()["BookMaxPly"]) {
    Move book_move = g_book.GetOneBookMove(root_node(), usi_options());
    if (book_move != kMoveNone) {
      SYNCED_PRINTF("bestmove %s\n", book_move.ToSfen().c_str());
      return;
    }
  }

  std::string ignoremoves = "";
  std::vector<bool> busy_workers(workers_.size(), false);
  worker_infos_.clear();
  worker_infos_.resize(workers_.size());

  // 4. 相手の指し手の予想が当たった場合は、前回のPVの手を優先的にワーカーに割り当てる
  const bool prediction_hit = (position_sfen() == predicted_position_);
  if (prediction_hit) {
    // なるべく前回探索時と同じワーカーに割り当てる
    size_t worker_id = previous_best_worker_ != master_worker_id() ? previous_best_worker_ : 0;
    std::unique_ptr<ClusterWorker>& worker = workers_.at(worker_id);
    SYNCED_PRINTF("info string prediction hit! %zu %s\n", worker_id,
                  predicted_move_.c_str());
    // ワーカーに探索の指示を出す
    worker->SendCommand(position_sfen().c_str());
    worker->SendCommand("go infinite searchmoves %s", predicted_move_.c_str());
    worker->ExecuteTask();
    busy_workers.at(worker_id) = true;
    ignoremoves += " " + predicted_move_;
  }

  // 5. MultiPV探索を行い、ワーカを割り当てる指し手を決める
  size_t multipv = std::min(num_legal_moves, workers_.size()) - (prediction_hit ? 2 : 1);
  std::vector<UsiInfo> presearch_infos(multipv);
  ClusterWorker& master = master_worker();
  if (multipv > 0) {
    // MultiPV探索の指示を出す
    master.SendCommand("setoption name OwnBook value false");
    master.SendCommand("setoption name MultiPV value %zu", multipv);
    master.SendCommand(position_sfen().c_str());
    master.SendCommand("go byoyomi %d ignoremoves%s", kShallowSearchTime, ignoremoves.c_str());
    // infoコマンドを受信して、上位の手を調べる
    for (std::string line; master.RecieveCommand(&line); ) {
      std::istringstream is(line);
      std::string token;
      is >> token;
      if (token == "info") {
        UsiInfo info = ParseInfoCommand(is);
        if (info.multipv >= 1) {
          // presearch_infosは、後でうしろから取り出すので、良い手ほどうしろに保存する
          presearch_infos.at(multipv - info.multipv) = info;
        }
      } else if (token == "bestmove") {
        break;
      }
    }
    // MultiPVの設定を元に戻しておく
    master.SendCommand("setoption name MultiPV value 1");
  }

  // 6. MultiPV探索でヒップアップされた上位の手については、それぞれ１台のワーカに割り当てる
  for (size_t worker_id = 0; !presearch_infos.empty(); ++worker_id) {
    // マスター及び既に探索中のワーカーはスキップする
    if (worker_id == master_worker_id() || busy_workers.at(worker_id)) {
      continue;
    }
    // ワーカーに探索の指示を出す
    const std::vector<std::string>& pv = presearch_infos.back().pv;
    std::unique_ptr<ClusterWorker>& worker = workers_.at(worker_id);
    if (!pv.empty()) {
      worker->SendCommand(position_sfen().c_str());
      worker->SendCommand("go infinite searchmoves %s", pv.front().c_str());
      worker->ExecuteTask();
      busy_workers.at(worker_id) = true;
      ignoremoves += " " + pv.front();
    }
    presearch_infos.pop_back();
  }

  // 7. 残りの手については、まとめてマスターに割当てる
  assert(ignoremoves != "");
  master.SendCommand(position_sfen().c_str());
  master.SendCommand("go infinite ignoremoves%s", ignoremoves.c_str());
  master.ExecuteTask();

#define USE_SIMPLE_TIMER
#ifdef USE_SIMPLE_TIMER
  if (!go_options.infinite && !go_options.ponder) {
    std::thread timer_thread{[&](){
      std::chrono::milliseconds byoyomi(go_options.byoyomi);
      std::this_thread::sleep_for(byoyomi);
      OnStopCommandEntered();
    }};
    timer_thread.join();
  }
#endif
}

void Cluster::OnStopCommandEntered() {
  // 下流の各エンジンにstopコマンドを送信する
  SendCommandToAllWorkers("stop");

  // 各エンジンからbestmoveコマンドを受信するまで待機
  for (std::unique_ptr<ClusterWorker>& worker : workers_) {
    worker->WaitUntilTaskIsFinished();
  }

  // 最善手を送信する
  const std::vector<std::string>& pv = best_move_info_.pv;
  if (pv.empty()) {
    SYNCED_PRINTF("bestmove resign\n");
  } else if (pv.size() == 1) {
    SYNCED_PRINTF("bestmove %s\n", pv.at(0).c_str());
  } else {
    SYNCED_PRINTF("bestmove %s ponder %s\n", pv.at(0).c_str(), pv.at(1).c_str());
  }

  // 次回探索時の探索割当の参考とするため、予想局面及び予想局面における最善手を保存しておく
  if (pv.size() >= 3) {
    predicted_position_ = position_sfen() + " " + pv.at(0) + " " + pv.at(1);
    predicted_move_ = pv.at(2);
  } else {
    predicted_position_.clear();
    predicted_move_.clear();
  }
}

void Cluster::OnPonderhitCommandEntered() {
  SendCommandToAllWorkers("ponderhit");
}

void Cluster::OnQuitCommandEntered() {
  OnStopCommandEntered();
  workers_.clear(); // WorkerEngineのデストラクタが呼ばれるため、外部プロセスは終了する
}

void Cluster::OnGameoverCommandEntered(const std::string& result) {
  SendCommandToAllWorkers(("gameover " + result).c_str());
}

void Cluster::UpdateInfo(int worker_id, const UsiInfo& usi_info) {
  // 最善手を特定する際にデータが更新されないように、排他制御を行う
  std::unique_lock<std::mutex> lock(mutex_);

  // 現在の最善手を求める
  int best_worker_id = 0, second_worker_id = 1;
  Score best_score = -kScoreInfinite - 1, second_score = -kScoreInfinite - 2;
  int64_t total_nps = 0, total_nodes = 0;
  for (size_t i = 0; i < workers_.size(); ++i) {
    const UsiInfo info = worker_infos_.at(i);
    Score score = info.score;
    if (score > best_score) {
      second_score = best_score;
      second_worker_id = best_worker_id;
      best_score = score;
      best_worker_id = i;
    } else if (score > second_score) {
      second_score = score;
      second_worker_id = i;
    }
    total_nps += info.nps;
    total_nodes += info.nodes;
  }

  // 最善手を更新するかどうかを調べる
  const UsiInfo* best_info = nullptr;
  if (worker_id == best_worker_id) {
    // 1. もともと最善手の場合: 次善手の評価値と比較する
    if (   usi_info.score > second_score
        || (usi_info.score == second_score && worker_id < best_worker_id)) {
      // 最善手は変わらないが、最善手の評価値が更新された
      best_info = &usi_info;
      previous_best_worker_ = worker_id;
    } else {
      // 最善手が入れ替わった
      best_info = &worker_infos_.at(second_worker_id);
      previous_best_worker_ = second_worker_id;
    }
  } else {
    // 2. もともと最善手以外の場合: 最善手の評価値と比較する
    if (   usi_info.score > best_score
        || (usi_info.score == best_score && worker_id < best_worker_id)) {
      // 最善手が入れ替わった
      best_info = &usi_info;
      previous_best_worker_ = worker_id;
    }
  }

  // 最善手を出力する
  if (best_info != nullptr) {
    UsiInfo temp = *best_info;
    temp.nps = total_nps;
    temp.nodes = total_nodes;
    std::printf("%s\n", temp.ToString().c_str());
    best_move_info_ = temp;
  }

  // ワーカのinfoコマンドを保存する
  worker_infos_.at(worker_id) = usi_info;
}

#endif /* !defined(MINIMUM) */
