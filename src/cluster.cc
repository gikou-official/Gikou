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

#if !defined(MINIMUM)

#include "cluster.h"

#include <sstream>
#include "book.h"
#include "movegen.h"
#include "synced_printf.h"

namespace {

Book g_book;

}

void MinimaxNode::Split(const std::vector<std::string>& split_moves) {
  assert(is_leaf_node() && ignoremoves_.empty()); // この場合のみ分割できる

  if (split_moves.empty()) {
    return;
  }

  // 上位N手は、１手につき１ノードを割り当てる
  for (std::string move : split_moves) {
    child_nodes_.emplace_back(new MinimaxNode());
    MinimaxNode& child = *child_nodes_.back();
    child.parent_ = this;
    child.root_position_sfen_ = root_position_sfen_;
    child.path_from_root_ = path_from_root_;
    child.path_from_root_.push_back(move);
  }

  // その他すべての手は、最後のノードにまとめて割り当てる
  {
    child_nodes_.emplace_back(new MinimaxNode());
    MinimaxNode& child = *child_nodes_.back();
    child.parent_ = this;
    child.root_position_sfen_ = root_position_sfen_;
    child.path_from_root_ = path_from_root_;
    child.ignoremoves_ = split_moves;
  }
}

void MinimaxNode::RegisterAllLeafNodes(std::vector<MinimaxNode*>* leaf_nodes) {
  assert(leaf_nodes != nullptr);

  if (is_leaf_node()) {
    // a. リーフノードの場合: 自分自身を登録して終了する
    leaf_nodes->push_back(this);
  } else {
    // b. 内部ノードの場合: 子ノードのリーフノードを全て登録する
    for (std::unique_ptr<MinimaxNode>& child : child_nodes_) {
      child->RegisterAllLeafNodes(leaf_nodes);
    }
  }
}

std::string MinimaxNode::GetPositionCommand() const {
  assert(is_leaf_node()); // 直接ワーカーが割り当てられているノード（リーフノード）のみが対象

  if (path_from_root_.empty()) {
    return root_position_sfen_;
  } else {
    std::string command = root_position_sfen_;
    if (root_position_sfen_.find("moves") == std::string::npos) {
      command += " moves";
    }
    for (std::string move : path_from_root_) {
      command += " " + move;
    }
    return command;
  }
}

std::string MinimaxNode::GetGoCommand() const {
  assert(is_leaf_node()); // 直接ワーカーが割り当てられているノード（リーフノード）のみが対象

  std::string command = "go infinite";
  if (!ignoremoves_.empty()) {
    command += " ignoremoves";
    for (const std::string& move : ignoremoves_) {
      command += " " + move;
    }
  }
  return command;
}

void MinimaxNode::UpdateMinimaxTree() {
  //
  // Step 1. usi_info_を更新する
  //
  if (!is_leaf_node()) {
    int best_child_id = 0;
    int max_seldepth = 0;
    Score best_score = -kScoreInfinite - 1;
    int64_t total_nps = 0, total_nodes = 0;

    // 最善手、最善手の評価値、最大seldepth、合計NPS、合計ノード数を求める
    for (size_t child_id = 0; child_id < child_nodes_.size(); ++child_id) {
      const std::unique_ptr<MinimaxNode>& child = child_nodes_.at(child_id);
      const UsiInfo& child_info = child->usi_info_;

      // 子ノードの評価値を取得する
      Score child_score = child_info.score;
      if (child->ignoremoves_.empty()) {
        // ignoremovesが指定されていなければ、相手の手番なので、評価値を反転する
        child_score = -child_score;
      }

      // 子ノードで最大の評価値を更新する
      if (child_score > best_score) {
        best_score = child_score;
        best_child_id = child_id;
      }

      // 子ノードで最大の評価値を更新する
      max_seldepth = std::max(max_seldepth, child_info.seldepth);
      total_nps += child_info.nps;
      total_nodes += child_info.nodes;
    }

    // 最善手の情報を取得する
    const UsiInfo& best_node_info = child_nodes_.at(best_child_id)->usi_info_;

    // このノードの情報を更新する
    usi_info_.depth    = best_node_info.depth;
    usi_info_.seldepth = max_seldepth;
    usi_info_.time     = best_node_info.time;
    usi_info_.nodes    = total_nodes;
    usi_info_.score    = best_score;
    usi_info_.hashfull = best_node_info.hashfull;
    usi_info_.nps      = total_nps;
    usi_info_.pv       = best_node_info.pv;
  }

  //
  // Step 2. 親ノードに遡って、ミニマックス木を更新する（再帰的処理）
  //
  if (parent_ != nullptr) {
    parent_->UpdateMinimaxTree();
  }
}

void MinimaxNode::Reset() {
  parent_ = nullptr;
  child_nodes_.clear();
  usi_info_ = UsiInfo();
  root_position_sfen_.clear();
  path_from_root_.clear();
  ignoremoves_.clear();
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
#if 1
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
  g_book.ReadFromFile(usi_options()["BookFile"].string().c_str());

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
  const int kPresearchTime = 300; // 2015年版YSSと同じ設定

  // 1. 合法手の数を調べる
  SimpleMoveList<kAllMoves, true> legal_moves(root_node());
  int num_legal_moves = legal_moves.size();

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

  // 4. MultiPV探索を行い、ワーカを割り当てる指し手を決める
  int kMaxSplitAtRoot = 8;
  size_t multipv = std::min(num_legal_moves, kMaxSplitAtRoot - 1);
  std::vector<UsiInfo> presearch_infos(multipv);
  ClusterWorker& master = master_worker();
  if (multipv >= 1) {
    // MultiPV探索の指示を出す
    master.SendCommand("setoption name OwnBook value false");
    master.SendCommand("setoption name MultiPV value %zu", multipv);
    master.SendCommand(position_sfen().c_str());
    master.SendCommand("go byoyomi %d", kPresearchTime);
    // infoコマンドを受信して、上位の手を調べる
    for (std::string line; master.RecieveCommand(&line); ) {
      std::istringstream is(line);
      std::string token;
      is >> token;
      if (token == "info") {
        UsiInfo info = ParseInfoCommand(is);
        if (info.multipv >= 1) {
          // multipvは、配列の添字（0から始まる）と異なり、1から始まるので、1を引く
          presearch_infos.at(info.multipv - 1) = info;
        }
      } else if (token == "bestmove") {
        break;
      }
    }
    // MultiPVの設定を元に戻しておく
    master.SendCommand("setoption name MultiPV value 1");
  }

  // 5. MultiPV探索で得られた情報を元に、探索木を構築する
  root_of_minimax_tree_.Reset();
  root_of_minimax_tree_.set_root_position_sfen(position_sfen());
  leaf_nodes_.clear();
  // a. 深さ１: ルートを最大８分割する
  {
    std::vector<std::string> split_moves;
    for (const UsiInfo& info : presearch_infos) {
      split_moves.push_back(info.pv.at(0));
    }
    root_of_minimax_tree_.Split(split_moves);
  }
  // b. 深さ２: 上位７手については、更に２分割する
  for (size_t i = 0; i < presearch_infos.size(); ++i) {
    const std::vector<std::string>& pv = presearch_infos.at(i).pv;
    if (pv.size() >= 2) {
      MinimaxNode& child = root_of_minimax_tree_.GetChild(i);
      std::vector<std::string> split_moves{pv.at(1)};
      child.Split(split_moves);
    }
  }
  // c. 深さ３: 最善手については、更に２分割する
  {
    const std::vector<std::string>& pv = presearch_infos.front().pv;
    if (pv.size() >= 3) {
      MinimaxNode& grandchild = root_of_minimax_tree_.GetChild(0).GetChild(0);
      std::vector<std::string> split_moves{pv.at(2)};
      grandchild.Split(split_moves);
    }
  }
  root_of_minimax_tree_.RegisterAllLeafNodes(&leaf_nodes_);
  assert(leaf_nodes_.size() <= workers_.size());

  // 6. 各リーフノードについて、それぞれ１台のワーカを割り当てる
  for (size_t worker_id = 0; worker_id < leaf_nodes_.size(); ++worker_id) {
    std::unique_ptr<ClusterWorker>& worker = workers_.at(worker_id);
    const MinimaxNode& leaf_node = *leaf_nodes_.at(worker_id);
    worker->SendCommand("%s", leaf_node.GetPositionCommand().c_str());
    worker->SendCommand("%s", leaf_node.GetGoCommand().c_str());
    worker->ExecuteTask();
  }

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
  const std::vector<std::string>& pv = root_of_minimax_tree_.usi_info().pv;
  if (pv.empty()) {
    SYNCED_PRINTF("bestmove resign\n");
  } else {
    // bestmoveコマンドを返す前に、ここで１度は必ずinfoコマンドを送る
    SYNCED_PRINTF("%s\n", root_of_minimax_tree_.usi_info().ToString().c_str());

    if (pv.size() == 1) {
      // PVの長さが１手分しかなければ、bestmoveのみを返す
      SYNCED_PRINTF("bestmove %s\n", pv.at(0).c_str());
    } else {
      // PVの長さが２手分以上あれば、先読み（ponder）の指示をだす
      SYNCED_PRINTF("bestmove %s ponder %s\n", pv.at(0).c_str(), pv.at(1).c_str());
    }
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

  // 前回の最善手情報を保存しておく
  const UsiInfo previous_info = root_of_minimax_tree_.usi_info();

  // たったいま受信したUSIのinfoコマンドの内容を保存する
  leaf_nodes_.at(worker_id)->set_usi_info(usi_info);

  // ミニマックス木を更新する
  leaf_nodes_.at(worker_id)->UpdateMinimaxTree();

  // ミニマックス木更新後の最善手情報を取得する
  const UsiInfo& current_info = root_of_minimax_tree_.usi_info();

  // 最善手の情報が更新されていたら、上流にinfoコマンドを送信する
  if (!current_info.pv.empty()) {
    if (   previous_info.pv.empty()                            // 初回の更新
        || current_info.depth > previous_info.depth            // 読みが深くなった
        || current_info.pv.front() != previous_info.pv.front() // 最善手が変わった
        || current_info.score != previous_info.score) {        // 評価値が変わった
      SYNCED_PRINTF("%s\n", current_info.ToString().c_str());
    }
  }
}

#endif /* !defined(MINIMUM) */
