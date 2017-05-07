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

#include "thinking.h"

#include "book.h"
#include "move_probability.h"
#include "movegen.h"
#include "node.h"
#include "search.h"
#include "synced_printf.h"
#include "thread.h"
#include "usi.h"
#include "usi_protocol.h"

Thinking::Thinking(const UsiOptions& usi_options)
    : usi_options_(usi_options),
      time_manager_(usi_options, &shared_data_.signals),
      thread_manager_(shared_data_, time_manager_) {
}

void Thinking::Initialize() {
  book_.ReadFromFile(usi_options_["BookFile"].string().c_str());
  shared_data_.hash_table.SetSize(usi_options_["USI_Hash"]);
  shared_data_.countermoves_history.Clear();
  MoveProbability::SetCacheTableSize(ProbabilityCacheTable::kDefaultSize * usi_options_["Threads"]);
}

void Thinking::StartNewGame() {
  // 現在のところ、特に行う処理はない
}

void Thinking::ResetSignals() {
  shared_data_.signals.Reset();
}

void Thinking::StartThinking(const Node& root_node,
                             const UsiGoOptions& go_options) {
  bool win_declaration_is_possible = false;
  Move best_move = kMoveNone;
  Move ponder_move = kMoveNone;
  SimpleMoveList<kAllMoves, true> all_legal_moves(root_node);

  // searchmovesオプション、ignoremovesオプションを考慮して、探索すべき手を確定する
  const std::vector<RootMove> root_moves = Search::CreateRootMoves(
      root_node, go_options.searchmoves, go_options.ignoremoves);

  // 1. 入玉宣言勝ち
  if (root_node.WinDeclarationIsPossible(true)) {
    win_declaration_is_possible = true;
    SYNCED_PRINTF("info depth 1 nodes 0 time 0 score mate + string Nyugyoku\n");
    goto send_best_move;
  }

  // 2. 合法手が存在しなければ、投了する
  if (all_legal_moves.size() == 0) {
    SYNCED_PRINTF("info depth 0 nodes 0 time 0 string No legal moves.\n");
    best_move = kMoveNone;
    goto send_best_move;
  }

  // 3. 全ての指し手を無視するように指示されていたら、探索をスキップする
  if (root_moves.empty()) {
    SYNCED_PRINTF("info depth 0 nodes 0 time 0 string All moves are ignored.\n");
    best_move = kMoveNone;
    goto send_best_move;
  }

  // 4. 時間制限が存在する場合は、定跡を使う
  if (   !go_options.infinite
      && !go_options.ponder
      && go_options.searchmoves.empty()
      && go_options.ignoremoves.empty()
      && usi_options_["OwnBook"]
      && root_node.game_ply() + 1 <= usi_options_["BookMaxPly"]) {
    // 定跡DBから１手取得する
    Move book_move = book_.GetOneBookMove(root_node, usi_options_);

    // 定跡存在するときは、通常探索をスキップする
    if (book_move != kMoveNone) {
      best_move = book_move;
      goto send_best_move;
    }
  }

  // 5. 通常探索を行う
  if (!go_options.mate) {
    // a. 時間管理を開始する
    time_manager_.StartTimeManagement(root_node, go_options);

    // b. 探索の準備をする
    Node node = root_node;
    Score draw_score = Score(int(usi_options_["DrawScore"]));
    thread_manager_.SetNumSearchThreads(usi_options_["Threads"]);

    // c. 探索を開始する
    // 読みの深さ制限機能については、USIオプションよりも、goコマンドのオプションを優先する
    int depth_limit = (go_options.depth != kMaxPly) ? go_options.depth : int(usi_options_["DepthLimit"]);
    uint64_t nodes_limit = go_options.nodes;
    const RootMove& best_root_move = thread_manager_.ParallelSearch(node,
                                                                    draw_score,
                                                                    root_moves,
                                                                    usi_options_["MultiPV"],
                                                                    depth_limit,
                                                                    nodes_limit);

    // d. 時間管理用のスレッドに終了の指示を出す
    time_manager_.StopTimeManagement();

    // e. 最善手と、相手の予想手を取得する
    const std::vector<Move>& pv = best_root_move.pv;
    best_move   = pv.size() >= 1U ? pv.at(0) : kMoveNone;
    ponder_move = pv.size() >= 2U ? pv.at(1) : kMoveNone;

    // f. 相手の予想手が取得できない場合は、ハッシュテーブルからの取得を試みる
    if (ponder_move == kMoveNone) {
      // note: まれに、ハッシュテーブルからも取得できない場合もある
      ponder_move = shared_data_.hash_table.GetPonderMove(root_node, best_move);
    }

    // g. 時間管理用のスレッドが終了するまで待機する
    time_manager_.WaitUntilTaskIsFinished();

    // h. USIオプションで指定された、投了する評価値を下回ったときは、投了する
    if (best_root_move.score < Score(int(usi_options_["ResignScore"]))) {
      best_move = kMoveNone;
    }
  }

send_best_move:

  // 6. 必要であれば、最善手を送る前に待機する
  //    USIプロトコルにおいては、go infiniteか、go ponderで始まった場合は、
  //    stopかponderhitが来ない限り、bestmoveを返してはいけないことになっているため。
  //    http://www.geocities.jp/shogidokoro/usi.html
  if (go_options.infinite || go_options.ponder) {
    std::unique_lock<std::mutex> lock(mutex_);
    sleep_condition_.wait(lock, [&](){
      return shared_data_.signals.stop || shared_data_.signals.ponderhit;
    });
  }

  // 7. 最善手を送る
  if (win_declaration_is_possible) {
    // a. 入玉宣言勝ちができる場合は、勝ち宣言を行う
    SYNCED_PRINTF("bestmove win\n");
  } else if (best_move == kMoveNone) {
    // b. 最善手がなければ投了する
    SYNCED_PRINTF("bestmove resign\n");
  } else if (usi_options_["USI_Ponder"] && ponder_move != kMoveNone) {
    // c. 最善手と、予測読みの手を送る
    SYNCED_PRINTF("bestmove %s ponder %s\n",
                  best_move.ToSfen().c_str(),
                  ponder_move.ToSfen().c_str());
  } else {
    // d. 最善手のみを送る
    SYNCED_PRINTF("bestmove %s\n", best_move.ToSfen().c_str());
  }
}

void Thinking::StopThinking() {
  mutex_.lock();
  shared_data_.signals.stop = true;
  mutex_.unlock();

  sleep_condition_.notify_one();
}

void Thinking::Ponderhit() {
  time_manager_.RecordPonderhitTime();

  mutex_.lock();
  shared_data_.signals.ponderhit = true;
  mutex_.unlock();

  sleep_condition_.notify_one();
}
