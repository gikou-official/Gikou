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

#include "usi_protocol.h"

#include <iostream>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include "synced_printf.h"

std::string UsiInfo::ToString() const {
  std::string str = "info";

  if (depth         > 0) str += " depth "    + std::to_string(depth);
  if (seldepth      > 0) str += " seldepth " + std::to_string(seldepth);
  if (time          > 0) str += " time "     + std::to_string(time);
  if (nodes         > 0) str += " nodes "    + std::to_string(nodes);
  if (!currmove.empty()) str += " currmove " + currmove;
  if (hashfull      > 0) str += " hashfull " + std::to_string(hashfull);
  if (nps           > 0) str += " nps "      + std::to_string(nps);
  if (multipv       > 0) str += " multipv "  + std::to_string(multipv);

  if (score > -kScoreInfinite) {
    // 1. 評価値の出力
    if (score >= kScoreMateInMaxPly) {
      // a. 自分の勝ちを読みきった場合
      int s = std::min(Score(score), score_mate_in(1));
      str += " score mate " + std::to_string(int(kScoreMate - s));
    } else if (score <= kScoreMatedInMaxPly) {
      // b. 自分の負けを読みきった場合
      int s = std::max(Score(score), score_mated_in(2));
      str += " score mate -" + std::to_string(int(kScoreMate + s));
    } else {
      // c. 勝敗を読みきっていない場合
      str += " score cp " + std::to_string(int(score));
    }

    // 2. upperbound/lowerboundの出力
    if (bound == kBoundUpper) {
      str += " upperbound";
    } else if (bound == kBoundLower) {
      str += " lowerbound";
    }
  }

  if (!pv.empty()) {
    str += " pv";
    for (const std::string& move : pv) {
      str += " " + move;
    }
  } else if (!string.empty()) {
    str += " string " + string;
  }

  return str;
}

UsiProtocol::UsiProtocol(const char* program_name, const char* author_name)
    : program_name_(program_name),
      author_name_(author_name) {
}

void UsiProtocol::Start() {
  // デバッグモード時は、標準エラー出力をログファイルに出力する
#ifndef NDEBUG
  int stderr_log = creat("stderr_log.txt", 0664);
  dup2(stderr_log, STDERR_FILENO);
#endif

  // 標準入出力のバッファリングをオフにする。これは、「将棋所」の作者により推奨されている。
  // http://www.geocities.jp/shogidokoro/enginecaution.html
  std::setvbuf(stdout, NULL, _IONBF, 0);
  std::setvbuf(stdin, NULL, _IONBF, 0);

  for (std::string line; std::getline(std::cin, line); ) {
    std::istringstream is(line);
    std::string type;
    is >> type;

    if (type == "usi") {
      SYNCED_PRINTF("id name %s\n", program_name_);
      SYNCED_PRINTF("id author %s\n", author_name_);
      usi_options_.PrintListOfOptions();
      SYNCED_PRINTF("usiok\n");

    } else if (type == "isready") {
      OnIsreadyCommandEntered();

    } else if (type == "setoption") {
      ParseSetoptionCommand(is, &usi_options_);

    } else if (type == "usinewgame") {
      OnUsinewgameCommandEntered();

    } else if (type == "position") {
      position_sfen_ = line;
      ParsePositionCommand(is, &root_node_);

    } else if (type == "go") {
      UsiGoOptions options = ParseGoCommand(is, root_node());
      OnGoCommandEntered(options);

    } else if (type == "stop") {
      OnStopCommandEntered();

    } else if (type == "ponderhit") {
      OnPonderhitCommandEntered();

    } else if (type == "quit") {
      OnQuitCommandEntered();
      return; // quitコマンドが来たら終了する

    } else if (type == "gameover") {
      std::string result;
      is >> result;
      OnGameoverCommandEntered(result);

    } else if (type == "d") {
      root_node_.Print(root_node_.last_move());
    }
  }
}

void UsiProtocol::ParseSetoptionCommand(std::istringstream& is,
                                        UsiOptions* const usi_options) {
  // 1. setoptionコマンドを読み込む
  std::string name, value;
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

void UsiProtocol::ParsePositionCommand(std::istringstream& is,
                                       Node* const node) {
  // 1. 局面を読み込む
  std::string type;
  is >> type;
  if (type == "startpos") {
    *node = Node(Position::CreateStartPosition());
  } else if (type == "sfen") {
    std::string board, stm, hands, move_count;
    if (is >> board >> stm >> hands >> move_count) {
      std::string sfen = board + " " + stm + " " + hands + " " + move_count;
      *node = Node(Position::FromSfen(sfen));
    } else {
      SYNCED_PRINTF("info string Unsupported SFEN.\n");
      assert(0);
      return;
    }
  } else if (type != "startpos") {
    SYNCED_PRINTF("info string Unsupported Position Type: %s\n", type.c_str());
    assert(0);
    return;
  }

  // 2. 指し手を読み込み、現在の局面まで移動する
  std::string moves;
  is >> moves;
  if (moves == "moves") {
    for (std::string move_str; is >> move_str; ) {
      Move move = Move::FromSfen(move_str, *node);
      node->MakeMove(move);
      node->Evaluate(); // 評価関数の差分計算を行う
    }
  }

  // 3. 探索ノード数をゼロにリセットする
  node->set_nodes_searched(0);
}

UsiGoOptions UsiProtocol::ParseGoCommand(std::istringstream& is,
                                         const Position& pos) {
  UsiGoOptions options;

  for (std::string token; is >> token; ) {
    if      (token == "ponder"     ) options.ponder = true;
    else if (token == "btime"      ) is >> options.time[kBlack];
    else if (token == "wtime"      ) is >> options.time[kWhite];
    else if (token == "byoyomi"    ) is >> options.byoyomi;
    else if (token == "binc"       ) is >> options.inc[kBlack];
    else if (token == "winc"       ) is >> options.inc[kWhite];
    else if (token == "infinite"   ) options.infinite = true;
    else if (token == "nodes"      ) is >> options.nodes;
    else if (token == "depth"      ) is >> options.depth;
    else if (token == "mate"       ) {
      std::string time;
      is >> time;
      if (time == "infinite") {
        options.infinite = true;
      } else {
        options.byoyomi = std::stoi(time);
      }
      options.mate = true;
    }
    else if (token == "searchmoves") {
      for (std::string move; is >> move; ) {
        options.searchmoves.push_back(Move::FromSfen(move, pos));
      }
      break; // searchmovesサブコマンドは、必ず最後に置かれる
    }
    else if (token == "ignoremoves") {
      for (std::string move; is >> move; ) {
        options.ignoremoves.push_back(Move::FromSfen(move, pos));
      }
      break; // ignoremovesサブコマンドは、必ず最後に置かれる
    }
  }

  return options;
}

UsiInfo UsiProtocol::ParseInfoCommand(std::istringstream& is) {
  UsiInfo info;

  for (std::string subcommand; is >> subcommand; ) {
    if      (subcommand == "depth"     ) is >> info.depth;
    else if (subcommand == "seldepth"  ) is >> info.seldepth;
    else if (subcommand == "time"      ) is >> info.time;
    else if (subcommand == "nodes"     ) is >> info.nodes;
    else if (subcommand == "upperbound") info.bound = kBoundUpper;
    else if (subcommand == "lowerbound") info.bound = kBoundLower;
    else if (subcommand == "currmove"  ) is >> info.currmove;
    else if (subcommand == "hashfull"  ) is >> info.hashfull;
    else if (subcommand == "nps"       ) is >> info.nps;
    else if (subcommand == "multipv"   ) is >> info.multipv;
    else if (subcommand == "score"     ) {
      std::string type, score;
      is >> type >> score;
      if (type == "cp") {
        info.score = static_cast<Score>(std::stoi(score));
      } else if (type == "mate") {
        if (score == "+") {
          info.score = kScoreMateInMaxPly;
        } else if (score == "-") {
          info.score = kScoreMatedInMaxPly;
        } else {
          int s = std::stoi(score);
          info.score = (s > 0) ? score_mate_in(s) : score_mated_in(std::abs(s));
        }
      }
    } else if (subcommand == "pv") {
      for (std::string move; is >> move; ) {
        info.pv.push_back(move);
      }
      break;
    } else if (subcommand == "string") {
      is >> std::ws;
      info.string = is.str().substr(is.tellg());
      break;
    }
  }

  return info;
}
