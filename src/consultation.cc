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

#include "consultation.h"

#include <chrono>
#include <map>
#include <sstream>
#include "book.h"
#include "movegen.h"
#include "synced_printf.h"

namespace {
Book g_book;
}

ConsultationWorker::ConsultationWorker(int worker_id,
                                       Consultation& consultation)
    : worker_id_(worker_id),
      consultation_(consultation) {
}

ConsultationWorker::~ConsultationWorker() {
  // ワーカにquitコマンドを送信
  SendCommand("quit");

  // 外部プロセスの終了を待つ
  external_process_.WaitFor();
}

void ConsultationWorker::Initialize() {
  // 1. 外部プロセスを使い、USIエンジンを起動する
  if (worker_id() == consultation_.master_worker_id()) {
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
    std::string worker_name = "cluster-" + std::to_string(worker_id());
    char* const args[] = {
#if 1
        // リモートマシンをワーカーとして使用する場合: SSHで通信する
        const_cast<char*>("ssh"),
        const_cast<char*>(worker_name.c_str()), // ~/.ssh/configでワーカーのホスト名を設定しておく
        const_cast<char*>("cd gikou/bin; ./release --cluster"), // ディレクトリを移動後、実行する
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
  const UsiOptions& options = consultation_.usi_options();
  if (worker_id() == consultation_.master_worker_id()) {
    // マスターワーカーのメモリ容量とスレッド数については、特定のマシンを使うことにして、ひとまずベタ打ちしておく
    SendCommand("setoption name USI_Hash value %d", 8192);
    SendCommand("setoption name Threads value %d", 5);
    SendCommand("setoption name DrawScore value %d", (int)options["DrawScore"]);
  } else {
    // USIオプションを下流に伝達する
    SendCommand("setoption name USI_Hash value %d", (int)options["USI_Hash"]);
    SendCommand("setoption name Threads value %d", (int)options["Threads"]);
    SendCommand("setoption name DrawScore value %d", (int)options["DrawScore"]);
  }
  SendCommand("isready");

  // 3. readyokが送られてくるまで待機する
  for (std::string line; RecieveCommand(&line); ) {
    if (line == "readyok") break;
  }
}

void ConsultationWorker::Run() {
  // bestmoveコマンドが帰ってくるまで探索する
  for (std::string line; RecieveCommand(&line); ) {
    std::istringstream is(line);
    std::string token;
    is >> token;
    if (token == "info") {
      // ワーカーとの通信が切断された場合、それ以降そのワーカーからのinfoコマンドは無視される
      if (is_alive()) {
        UsiInfo info = UsiProtocol::ParseInfoCommand(is);
        consultation_.UpdateInfo(worker_id_, info);
      }
    } else if (token == "bestmove") {
      break;
    }
  }
}

void ConsultationWorker::OnTaskFinished() {
  consultation_.NotifySearchIsFinished();
}

void TimeManagerForConsultation::HandleTimeUpEvent() {
  consultation_.OnStopCommandEntered();
}

Consultation::Consultation()
    : UsiProtocol("Gikou Hybrid Cluster", "Yosuke Demura"),
      time_manager_(usi_options(), *this) {
}

void Consultation::OnIsreadyCommandEntered() {
  // 定跡ファイルを読み込む
  g_book.ReadFromFile(usi_options()["BookFile"].string().c_str());

  if (workers_.empty()) {
    // 1. 初回は、ワーカーを必要なだけ起動する
    for (size_t worker_id = 0; worker_id < num_workers_ + 1; ++worker_id) {
      ConsultationWorker* worker = new ConsultationWorker(worker_id, *this);
      worker->StartNewThread();
      workers_.emplace_back(worker);
    }

    // ワーカーの準備ができるまで待機する
    for (std::unique_ptr<ConsultationWorker>& worker : workers_) {
      worker->WaitForReady();
    }
  } else {
    // 2. 次回以降は、既に起動しているプロセスを使い回す
    SendCommandToAllWorkers("isready");

    // readyokコマンドを送り返して来るまで待機する
    for (std::unique_ptr<ConsultationWorker>& worker : workers_) {
      for (std::string line; worker->RecieveCommand(&line); ) {
        if (line == "readyok") break;
      }
    }
  }

  // ワーカの準備ができたら、readyokコマンドを返す
  SYNCED_PRINTF("readyok\n");
}

void Consultation::OnUsinewgameCommandEntered() {
  SendCommandToAllWorkers("usinewgame");
}

void Consultation::OnGoCommandEntered(const UsiGoOptions& go_options) {
  // 宣言勝ちできる場合は、勝ち宣言をする
  if (root_node().WinDeclarationIsPossible(true)) {
    SendBestmoveCommand("bestmove win", go_options);
    return;
  }

  // 合法手の数を調べる
  SimpleMoveList<kAllMoves, true> legal_moves(root_node());
  size_t num_legal_moves = legal_moves.size();

  // 合法手の数が１手以下であれば、探索を省略する
  if (num_legal_moves == 0) {
    SendBestmoveCommand("bestmove resign", go_options);
    return;
  } else if (num_legal_moves == 1) {
    SendBestmoveCommand("bestmove " + legal_moves[0].move.ToSfen(), go_options);
    return;
  }

  // 時間制限がある場合は、定跡を使うことを考える
  if (   !go_options.infinite
      && !go_options.ponder
      && usi_options()["OwnBook"]
      && root_node().game_ply() + 1 <= usi_options()["BookMaxPly"]) {
    Move book_move = g_book.GetOneBookMove(root_node(), usi_options());
    if (book_move != kMoveNone) {
      SendBestmoveCommand("bestmove " + book_move.ToSfen(), go_options);
      return;
    }
  }

  // 時間管理を開始する
  time_manager_.WaitUntilTaskIsFinished(); // 時間管理用スレッドが利用可能になるまで待機する
  time_manager_.StartTimeManagement(root_node(), go_options);

  // 探索前に、前回の探索情報をクリアしておく
  best_move_info_ = UsiInfo();
  worker_infos_.clear();
  worker_infos_.resize(workers_.size());

  // 各ワーカーにpositionコマンドを送信する
  SendCommandToAllWorkers(position_sfen().c_str());

  // 各ワーカーに探索の指示を出す
  for (std::unique_ptr<ConsultationWorker>& worker : workers_) {
    worker->SendCommand("go infinite");
    worker->ExecuteTask();
  }
}

void Consultation::OnStopCommandEntered() {
  // bestmoveコマンドがすでに決まっているときは、bestmoveを送信して終了する
  if (send_bestmove_later_) {
    SYNCED_PRINTF("%s\n", bestmove_command_.c_str());
    send_bestmove_later_ = false;
    return;
  }

  // すべてのワーカーにstopコマンドを送信する
  SendCommandToAllWorkers("stop");

  // 時間管理を止める
  time_manager_.StopTimeManagement();

  // すべてのワーカーがbestmoveコマンドを受信するまで待機
  WaitUntilWorkersFinishSearching();

  // 最善手を取得する（他スレッドから最善手を書き換えて壊れるおそれがあるので、排他制御を行う）
  info_mutex_.lock();
  UpdateInfo();
  const std::vector<std::string>& pv = best_move_info_.pv;
  info_mutex_.unlock();

  // 最善手を送信する
  if (pv.empty()) {
    SYNCED_PRINTF("bestmove resign\n");
  } else if (pv.size() == 1) {
    SYNCED_PRINTF("bestmove %s\n", pv.at(0).c_str());
  } else {
    SYNCED_PRINTF("bestmove %s ponder %s\n", pv.at(0).c_str(), pv.at(1).c_str());
  }
}

void Consultation::OnPonderhitCommandEntered() {
  // bestmoveコマンドがすでに決まっているときは、bestmoveコマンドを送信して終了する
  if (send_bestmove_later_) {
    SYNCED_PRINTF("%s\n", bestmove_command_.c_str());
    send_bestmove_later_ = false;
    return;
  }

  // TimeManagerに、ponderhitコマンドが来た時間を記録する
  time_manager_.RecordPonderhitTime();

  // 下流の各エンジンにponderhitコマンドを送信する
  SendCommandToAllWorkers("ponderhit");
}

void Consultation::OnQuitCommandEntered() {
  OnStopCommandEntered();
  workers_.clear(); // ConsultationWorkerクラスのデストラクタが呼ばれる
}

void Consultation::OnGameoverCommandEntered(const std::string& result) {
  SendCommandToAllWorkers(("gameover " + result).c_str());
}

void Consultation::UpdateInfo() {
  // 合議を行う際の「票」を表す構造体
  struct Vote {
    int count = 0;
    int best_score = -kScoreInfinite;
    const UsiInfo* usi_info = nullptr;
    bool operator<(const Vote& rhs) const {
      // 多数決合議
      if (   count == rhs.count
          || best_score >= kScoreKnownWin
          || rhs.best_score >= kScoreKnownWin) {
        // Rule 1: 投票数が同数の場合、または、必勝手が発見された場合は、評価値が高い指し手が優先する
        return best_score < rhs.best_score;
      } else {
        // Rule 2: そうでない場合は、投票数が多い指し手を優先する（多数決合議）
        return count < rhs.count;
      }
    }
  };

  // 各ワーカーが投じる票の重要度を返す関数
  auto get_vote_importance = [&](int id) -> int {
    // 投票できる票数は、マスターは0票とし、ワーカーは各1票とする。
    // マスターの投票数を0票にしておくと、
    //   - ワーカーとの接続が生きている場合は、マスターの手は最終的な指し手に全く影響を与えない
    //   - すべてのワーカーとの接続が切れた場合は、マスターの指し手を指す
    // という動作を実現できる。
    return (id == master_worker_id()) ? 0 : 1;
  };

  // どの指し手がよいか、各ワーカーが投票する
  std::map<std::string, Vote> votes;
  uint64_t total_nodes = 0, total_nps = 0;
  for (size_t i = 0; i < worker_infos_.size(); ++i) {
    // ワーカーとの通信が切断されている場合は、その指し手は無視する
    if (!workers_.at(i)->is_alive()) {
      continue;
    }
    const UsiInfo& info = worker_infos_.at(i);
    if (!info.pv.empty()) {
      const std::string& bestmove = info.pv.front();
      Vote& vote = votes[bestmove];
      // 各ワーカーは、それぞれの推奨手に重み付きで投票する
      vote.count += get_vote_importance(i);
      // 各指し手について、最も高い評価値を記録する
      if (info.score > vote.best_score) {
        vote.best_score = info.score;
        vote.usi_info = &info;
      }
      // 合計NPS及び合計ノード数を記録する
      total_nps += info.nps;
      total_nodes += info.nodes;
    }
  }

  // 最善手の決定
  typedef std::pair<std::string, Vote> Pair;
  auto best_vote = std::max_element(votes.begin(), votes.end(),
                                    [](const Pair& lhs, const Pair& rhs) {
    return lhs.second < rhs.second;
  });

  // infoコマンドの送信など
  if (best_vote != votes.end() && best_vote->second.usi_info != nullptr) {
    UsiInfo temp = *(best_vote->second.usi_info);
    temp.nps = total_nps;
    temp.nodes = total_nodes;

    if (   best_move_info_.pv.empty()
        || temp.pv.front() != best_move_info_.pv.front()
        || temp.depth > best_move_info_.depth) {
      // 投票数を送信
      std::printf("info string votes");
      for (auto it = votes.begin(); it != votes.end(); ++it) {
        std::printf(" %s=%d", it->first.c_str(), it->second.count);
      }
      std::printf("\n");

      // infoコマンドを送信
      std::printf("%s\n", temp.ToString().c_str());
    }

    best_move_info_ = temp;
  }

  // 時間管理に必要な情報をTimeManagerに送る
  if (best_vote != votes.end()) {
    time_manager_.stats().agreement_rate = best_vote->second.count / double(num_workers_);
  }
}

void Consultation::UpdateInfo(int worker_id, const UsiInfo& worker_info) {
  std::unique_lock<std::mutex> lock(info_mutex_);

  // 最新の探索情報を保存する
  worker_infos_.at(worker_id) = worker_info;

  // 最善手に関する情報を更新する
  UpdateInfo();
}

void Consultation::NotifySearchIsFinished() {
  std::unique_lock<std::mutex> lock(wait_mutex_);
  wait_condition_.notify_one();
}

void Consultation::WaitUntilWorkersFinishSearching() {
  auto predicate = [&]() -> bool {
    for (std::unique_ptr<ConsultationWorker>& worker : workers_) {
      if (worker->is_alive() && worker->is_running()) {
        return false;
      }
    }
    return true;
  };

  // 全てのワーカーがbestmoveを返し終わるか、1000ミリ秒経過するまで待機する
  std::unique_lock<std::mutex> lock(wait_mutex_);
  wait_condition_.wait_for(lock, std::chrono::milliseconds(1000), predicate);
  lock.unlock();

  // 一定時間経過してもbestmoveが返ってこないワーカーがあれば、そのワーカーとの通信が切れたものとみなし、
  // 通信が切れたことを示すフラグを立てておく。
  for (std::unique_ptr<ConsultationWorker>& worker : workers_) {
    if (worker->is_running()) {
      worker->set_alive(false);
      SYNCED_PRINTF("info string Worker #%d is dead!\n", worker->worker_id());
    }
  }
}

void Consultation::SendBestmoveCommand(std::string command,
                                       const UsiGoOptions& go_options) {
  // USIプロトコルにおいては、go infiniteか、go ponderの指示が来ている場合は、
  // stopかponderhitが来ない限り、bestmoveを返してはいけないことになっているため、
  // bestmoveコマンドは後で送信する。
  if (go_options.infinite || go_options.ponder) {
    bestmove_command_ = command;
    send_bestmove_later_ = true;
    return;
  }

  // そうでなければ、直ちにbestmoveコマンドを送信する
  SYNCED_PRINTF("%s\n", command.c_str());
}

#endif /* !defined(MINIMUM) */
