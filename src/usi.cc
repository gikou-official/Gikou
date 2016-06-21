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

#include "usi.h"

#include <cstdio>
#include <cinttypes>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>
#include "movegen.h"
#include "node.h"
#include "search.h"
#include "synced_printf.h"
#include "thinking.h"
#include "types.h"
#include "usi_protocol.h"

namespace {

const auto kProgramName = "Gikou 20160621";
const auto kAuthorName  = "Yosuke Demura";
const auto kBookFile = "book.bin";

/**
 * USIコマンドを記憶するためのキューです.
 *
 * マルチスレッドに対応するために排他制御されています。
 */
class CommandQueue {
 public:
  /**
   * 一番最初のコマンドを取り出します（FIFO: First In First Out）.
   */
  std::string Pop() {
    std::unique_lock<std::mutex> lock(mutex_);

    // コマンドが送られてくるまでスリープする
    sleep_condition_.wait(lock, [this](){ return !queue_.empty(); });

    // 最初のコマンドを取り出す
    std::string first_command = queue_.front();
    queue_.pop();
    return first_command;
  }

  /**
   * コマンドを一番最後に追加します（FIFO: First In First Out）.
   */
  void Push(const std::string& command) {
    // 排他制御を行いつつ、コマンドを保管する
    mutex_.lock();
    queue_.push(command);
    mutex_.unlock();

    // スレッドが寝ている場合は起こす
    sleep_condition_.notify_one();
  }

 private:
  std::queue<std::string> queue_;
  std::mutex mutex_;
  std::condition_variable sleep_condition_;
};

void ReceiveCommands(CommandQueue* const queue, Thinking* const thinking) {
  assert(queue != nullptr);
  assert(thinking != nullptr);

  // 標準入力から1行ずつ読み込む
  for (std::string command; std::getline(std::cin, command);) {
    std::istringstream is(command);
    std::string type;
    is >> type;

    // quitコマンドが送られてきたときは、思考を中断したうえで、ループを抜ける
    if (type == "quit") {
      thinking->StopThinking();
      queue->Push(command);
      return;
    }

    if (type == "echo") {
      if (command.length() > 5) {
        SYNCED_PRINTF("%s\n", command.substr(5).c_str());
      }
      continue;
    }

    // stop, gameoverコマンドが来たときは、思考を終了する
    if (type == "stop" || type == "gameover") {
      thinking->StopThinking();

    // ponderhitコマンドが来たことを、思考部に知らせる
    } else if (type == "ponderhit") {
      thinking->Ponderhit();

    // goコマンドが来た時は、Thinkingクラスのシグナルの初期化を行う
    // ここで初期化処理を入れないと、シグナルが初期化される前にstop/ponderhitコマンドが
    // 到着してしまい、stop/ponderhitコマンドが機能しないおそれがあるため。
    } else if (type == "go") {
      thinking->ResetSignals();
    }

    // コマンドをキューに保存する
    // ここで保存されたコマンドは、ExecuteCommand()によって、1つずつ実行される
    queue->Push(command);
  }
}

void SetUsiOption(std::istream& is, UsiOptions* const usi_options) {
  assert(usi_options != nullptr);

  std::string name, value;

  // 1. setoptionコマンドを読み込む
  for (std::string token; is >> token;) {
    if (token == "name") {
      is >> name;
    } else if (token == "value") {
      is >> value;
    }
  }

  // 2. 値を保存する
  (*usi_options)[name] = value;
}

void SetRootNode(std::istream& input, Node* const node) {
  assert(node != nullptr);

  // 1. 局面表記の種類（startpos or sfen）を読み込む
  std::string type;
  input >> type;

  // 2. 局面を読み込む
  if (type == "startpos") {
    *node = Node(Position::CreateStartPosition());
  } else if (type == "sfen") {
    std::string board, stm, hands, move_count;
    if (input >> board >> stm >> hands >> move_count) {
      std::string sfen = board + " " + stm + " " + hands + " " + move_count;
      *node = Node(Position::FromSfen(sfen));
    } else {
      SYNCED_PRINTF("info string Unsupported SFEN.\n");
      assert(0);
      return;
    }
  } else {
    SYNCED_PRINTF("info string Unsupported Position Type: %s\n", type.c_str());
    assert(0);
    return;
  }

  // 3. （あれば）指し手を読み込む
  std::string move_str;
  std::vector<std::string> sfen_moves;
  input >> move_str;
  if (move_str == "moves") {
    for (std::string sfen_move; input >> sfen_move;) {
      sfen_moves.push_back(sfen_move);
    }
  }

  // 4. 指し手に沿って、局面を進める
  for (size_t i = 0, n = sfen_moves.size(); i < n; ++i) {
    Move move = Move::FromSfen(sfen_moves[i], *node);
    node->MakeMove(move);
    node->Evaluate(); // 評価関数の差分計算に必要
  }

  // 5. 探索局面数を0にリセットしておく
  node->set_nodes_searched(0);
}

void ExecuteCommand(const std::string& command, Node* const node,
                    UsiOptions* const usi_options, Thinking* const thinking) {
  assert(node != nullptr);
  assert(thinking != nullptr);
  assert(usi_options != nullptr);

  std::istringstream is(command);
  std::string type;

  // コマンドの種類を読み込む
  is >> type;

  // コマンドの種類ごとに処理を行う
  if (type == "usi") {
    SYNCED_PRINTF("id name %s\n", kProgramName);
    SYNCED_PRINTF("id author %s\n", kAuthorName);
    usi_options->PrintListOfOptions();
    SYNCED_PRINTF("usiok\n");

  } else if (type == "isready") {
    thinking->Initialize();
    Evaluation::ReadParametersFromFile("params.bin");
    SYNCED_PRINTF("readyok\n");

  } else if (type == "setoption") {
    SetUsiOption(is, usi_options);

  } else if (type == "usinewgame") {
    thinking->StartNewGame();

  } else if (type == "position") {
    SetRootNode(is, node);

  } else if (type == "go") {
    UsiGoOptions go_options = UsiProtocol::ParseGoCommand(is, *node, usi_options);
    thinking->StartThinking(*node, go_options);

  } else if (type == "stop" || type == "ponderhit" || type == "gameover") {
    // ReceiveCommands()によりすでに処理が完了しているので、特にすることはない

  } else if (type == "quit") {
    SYNCED_PRINTF("info string Thank You! Good Bye!\n");

#ifndef MINIMUM
  } else if (command == "d") {
    node->Print(node->last_move());

  } else if (command == "legalmoves") {
    std::string sfen_moves;
    for (ExtMove ext_move : SimpleMoveList<kAllMoves, true>(*node)) {
      sfen_moves += ext_move.move.ToSfen() + " ";
    }
    SYNCED_PRINTF("%s\n", sfen_moves.c_str());
#endif

  } else {
    SYNCED_PRINTF("info string Unsupported Command: %s\n", command.c_str());
  }
}

} // namespace

void Usi::Start() {
  // 1. 標準入出力のバッファリングをオフにする。
  // これは、「将棋所」の作者により推奨されている。
  // http://www.geocities.jp/shogidokoro/enginecaution.html
  std::setvbuf(stdout, NULL, _IONBF, 0);
  std::setvbuf(stdin, NULL, _IONBF, 0);

  // 2. 変数を準備する
  CommandQueue command_queue;
  Node node(Position::CreateStartPosition());
  UsiOptions usi_options;
  Thinking thinking(usi_options);

  // 3. コマンドの待受を別スレッドで開始する
  std::thread receiving_command_thread([&](){
    ReceiveCommands(&command_queue, &thinking);
  });

  // 4. 送られてきたコマンドを1つずつ実行する
  while (true) {
    // コマンドをキューから1つ取り出す
    std::string command = command_queue.Pop();

    // コマンドを実行する
    ExecuteCommand(command, &node, &usi_options, &thinking);

    // quitコマンドの場合は、エンジンを終了する
    if (command == "quit") {
      break;
    }
  }

  receiving_command_thread.join();
}

UsiOptions::UsiOptions() {
  // トランスポジションテーブルのサイズ（単位はMB）
  map_.emplace("USI_Hash", UsiOption(256, 1, 16384)); // from 1MB to 16GB

  // 先読みを有効にする場合はtrue
  map_.emplace("USI_Ponder", UsiOption(true));

  // 探索に用いるスレッド数
  map_.emplace("Threads", UsiOption(std::thread::hardware_concurrency(), 1, kMaxSearchThreads));

  // USI出力するPVの数
  map_.emplace("MultiPV", UsiOption(1, 1, Move::kMaxLegalMoves));

  // 千日手の評価値（単位は通常の評価値と同じ。１歩が１００点）
  map_.emplace("DrawScore", UsiOption(0, -200, 200));

  // 秒読み時の安全マージン（単位はミリ秒）
  map_.emplace("ByoyomiMargin", UsiOption(100, 0, 10000));

  // フィッシャールール時の安全マージン（単位はミリ秒）
  map_.emplace("FischerMargin", UsiOption(12000, 0, 60000));

  // 切れ負け対局のときに、安全のために予備的に残しておく時間（単位は秒）
  map_.emplace("SuddenDeathMargin", UsiOption(60, 0, 600));

  // 最小思考時間（実際には、ここから安全マージンを引いた時間だけ思考する）（単位はミリ秒）
  map_.emplace("MinThinkingTime", UsiOption(1000, 10, 60000));

  // 定跡を使うか否か（trueならば、定跡を用いる）
  map_.emplace("OwnBook", UsiOption(true));

  // 定跡を用いる最大手数
  map_.emplace("BookMaxPly", UsiOption(50, 0, 50));

  // 定跡手の評価値のしきい値（先手番）（先手番側から見た評価値がこの値未満の定跡手は選択しない）
  map_.emplace("MinBookScoreForBlack", UsiOption(0, -500, 500));

  // 定跡手の評価値のしきい値（後手番）（後手番側から見た評価値がこの値未満の定跡手は選択しない）
  map_.emplace("MinBookScoreForWhite", UsiOption(-180, -500, 500));

  // 出現頻度や勝率が低い定跡を除外する場合はtrue
  map_.emplace("NarrowBook", UsiOption(false));

  // 勝ち数が少ない定跡を除外する場合はtrue
  map_.emplace("TinyBook", UsiOption(false));

  // 探索深さ制限
  map_.emplace("LimitDepth", UsiOption(kMaxPly - 1, 1, kMaxPly - 1));
}

void UsiOptions::PrintListOfOptions() {
  for (const auto& element : map_) {
    const std::string& name = element.first;
    const UsiOption& option = element.second;

    // USI_PonderとUSI_Hashに関しては、将棋所で対局を開始するときに必ず送られてくるので、
    // エンジンがoptionコマンドで指定する必要はない。
    if (name == "USI_Hash" || name == "USI_Ponder") {
      continue;
    }

    // 今のところ、USIオプションは、「check」と「spin」の２種類のみ対応している。
    if (option.type() == UsiOption::kCheck) {
      SYNCED_PRINTF("option name %s type check default %s\n",
                    name.c_str(),
                    option.default_value() ? "true" : "false");
    } else if (option.type() == UsiOption::kSpin) {
      SYNCED_PRINTF("option name %s type spin default %d min %d max %d\n",
                    name.c_str(),
                    option.default_value(),
                    option.min(),
                    option.max());
    }
  }
}
