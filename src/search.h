/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016 Yosuke Demura
 * except where otherwise indicated.
 *
 * The Search class below is derived from Stockfish 7.
 * Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 * Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish author)
 * Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish author)
 * Copyright (C) 2016 Yosuke Demura
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

#ifndef SEARCH_H_
#define SEARCH_H_

#include <atomic>
#include <vector>
#include "common/array.h"
#include "common/arraymap.h"
#include "move.h"
#include "node.h"
#include "pvtable.h"
#include "shared_data.h"
#include "stats.h"

class ThreadManager;

/**
 * アルファベータ探索の並列探索で使用する、最大スレッド数です.
 */
constexpr int kMaxSearchThreads = 64;

/**
 * アルファベータ探索を行うためのクラスです.
 */
class Search {
 public:
  enum NodeType {
    kRootNode, kPvNode, kNonPvNode,
  };

  struct Stack {
    Array<Move, 2> killers;
    Move hash_move;
    Move current_move;
    Move excluded_move;
    Depth reduction;
    Score static_score;
    bool skip_null_move;
  };

  static void Init();

  Search(SharedData& shared, size_t thread_id = 0);

  /**
   * 反復深化による探索を行います.
   * 注意：この関数を呼ぶ前に、予めset_root_moves()関数を呼び、root_moves_をセットしておいてください。
   */
  void IterativeDeepening(Node& node, ThreadManager& thread_manager);

  void set_root_moves(const std::vector<RootMove>& root_moves) {
    root_moves_ = root_moves;
  }

  static std::vector<RootMove> CreateRootMoves(const Position& root_position,
                                               const std::vector<Move>& searchmoves,
                                               const std::vector<Move>& ignoremoves);


  Score AlphaBetaSearch(Node& node, Score alpha, Score beta, Depth depth);
  Score NullWindowSearch(Node& node, Score alpha, Score beta, Depth depth);

  template<NodeType kNodeType>
  Score MainSearch(Node& node, Score alpha, Score beta, Depth depth, int ply,
                   bool cut_node);

  bool is_master_thread() const {
    return thread_id_ == 0;
  }

  uint64_t num_nodes_searched() const {
    return num_nodes_searched_;
  }

  void set_learning_mode(bool is_learning) {
    learning_mode_ = is_learning;
  }

  void set_draw_scores(Color root_side_to_move, Score draw_score) {
    assert(-kScoreKnownWin < draw_score && draw_score < kScoreKnownWin);
    draw_scores_[root_side_to_move] = draw_score;
    draw_scores_[~root_side_to_move] = -draw_score;
  }

  void set_multipv(int multipv) {
    multipv_ = std::max(multipv, 1);
  }

  void set_limit_depth(int limit_depth) {
    limit_depth_ = std::max(std::min(limit_depth, kMaxPly - 1), 1);
  }

  void set_limit_nodes(uint64_t limit_nodes) {
    limit_nodes_ = limit_nodes > 1ULL ? limit_nodes : 1ULL;
  }

  std::vector<Move> GetPv() const;

  const RootMove& GetBestRootMove() const;

  uint64_t GetNodesUnder(Move move) const;

  const PvTable& pv_table() const {
    return pv_table_;
  }

  const HistoryStats& history() const {
    return history_;
  }

  const GainsStats& gains() const {
    return gains_;
  }

  void PrepareForNextSearch();

 private:
  static constexpr int kStackSize = kMaxPly + 6;

  template<NodeType kNodeType>
  Score QuiecenceSearch(Node& node, Score alpha, Score beta, Depth depth,
                        int ply) {
    return node.in_check()
         ? QuiecenceSearch<kNodeType, true >(node, alpha, beta, depth, ply)
         : QuiecenceSearch<kNodeType, false>(node, alpha, beta, depth, ply);
  }

  template<NodeType kNodeType, bool kInCheck>
  Score QuiecenceSearch(Node& node, Score alpha, Score beta, Depth depth,
                        int ply);

  void UpdateStats(Search::Stack* ss, Move move, Depth depth, Move* quiets,
                   int quiets_count);

  void SendUsiInfo(const Node& node, int depth, int64_t time, uint64_t nodes,
                   Bound bound = kBoundExact) const;

  void ResetSearchStack() {
    std::memset(stack_.begin(), 0, 5 * sizeof(Stack));
  }

  Stack* search_stack_at_ply(int ply) {
    assert(0 <= ply && ply <= kMaxPly);
    return stack_.begin() + 2 + ply; // stack_at_ply(0) - 2 の参照を可能にするため
  }

  SharedData& shared_;
  ArrayMap<Score, Color> draw_scores_{kScoreDraw, kScoreDraw};
  uint64_t num_nodes_searched_ = 0;
  int max_reach_ply_ = 0;
  int multipv_ = 1, pv_index_ = 0;
  uint64_t limit_nodes_ = 1152921504606846976ULL;
  int limit_depth_ = kMaxPly - 1;
  bool learning_mode_ = false;
  Array<Stack, kStackSize> stack_;
  PvTable pv_table_;

  HistoryStats history_;
  MovesStats countermoves_;
  MovesStats followupmoves_;
  GainsStats gains_;
  std::vector<RootMove> root_moves_;

  const size_t thread_id_;
};

#endif /* SEARCH_H_ */
