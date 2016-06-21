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

#include "search.h"

#include <cinttypes>
#include <cmath>
#include "evaluation.h"
#include "mate1ply.h"
#include "mate3.h"
#include "material.h"
#include "move_probability.h"
#include "movegen.h"
#include "movepick.h"
#include "position.h"
#include "synced_printf.h"
#include "swap.h"
#include "thread.h"
#include "time_manager.h"
#include "usi.h"
#include "zobrist.h"

namespace {

// 開発時に参照する統計データ
uint64_t g_mate3_tried = 0;
uint64_t g_mate3_nodes = 0;
uint64_t g_sum_move_counts = 0;
uint64_t g_num_beta_cuts = 0;
Array<uint64_t, 64> g_cuts_by_move;

Array<int16_t, 2, 2, 64, 64> g_reductions; // [pv][improving][depth][moveNumber]

const Array<std::vector<int>, 20> g_half_density = {
    {0, 1},
    {1, 0},
    {0, 0, 1, 1},
    {0, 1, 1, 0},
    {1, 1, 0, 0},
    {1, 0, 0, 1},
    {0, 0, 0, 1, 1, 1},
    {0, 0, 1, 1, 1, 0},
    {0, 1, 1, 1, 0, 0},
    {1, 1, 1, 0, 0, 0},
    {1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 1, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 1, 0 ,0},
    {0, 1, 1, 1, 1, 0, 0 ,0},
    {1, 1, 1, 1, 0, 0, 0 ,0},
    {1, 1, 1, 0, 0, 0, 0 ,1},
    {1, 1, 0, 0, 0, 0, 1 ,1},
    {1, 0, 0, 0, 0, 1, 1 ,1},
};

inline Score razor_margin(Depth depth) {
  return static_cast<Score>(512 + 32 * (depth / kOnePly));
}

inline int futility_move_count(bool is_pv_node, Depth depth) {
  return (is_pv_node ? 8 : 6) * depth / kOnePly;
}

inline Score futility_margin(Depth depth) {
  return static_cast<Score>(200 * (depth / kOnePly));
}

template<bool kIsPv>
inline Depth reduction(bool i, Depth d, int mn) {
  return static_cast<Depth>(g_reductions[kIsPv][i][std::min(int(d) / kOnePly, 63)][std::min(mn, 63)]);
}

Score ScoreToTt(Score s, int ply) {
  assert(s != kScoreNone);
  return s >= kScoreMateInMaxPly  ? s + ply
       : s <= kScoreMatedInMaxPly ? s - ply : s;
}

Score ScoreFromTt(Score s, int ply) {
  return s == kScoreNone          ? kScoreNone
       : s >= kScoreMateInMaxPly  ? s - ply
       : s <= kScoreMatedInMaxPly ? s + ply : s;
}

template<bool kIsPv>
inline bool HashCutOk(Bound bound, Score hash_score, Score beta) {
  if (kIsPv) {
    return bound == kBoundExact;
  } else {
    return hash_score >= beta ? (bound & kBoundLower) : (bound & kBoundUpper);
  }
}

} // namespace

void Search::Init() {

  int d; // depth (kOnePly == 1)
  int mc; // moveCount

  // Init reductions array
  for (d = 1; d < 64; ++d)
    for (mc = 1; mc < 64; ++mc) {
      double    pvRed = 0.00 + std::log(double(d)) * std::log(double(mc)) / 3.00;
      double nonPVRed = 0.33 + std::log(double(d)) * std::log(double(mc)) / 2.25;
      g_reductions[1][1][d][mc] = int16_t(   pvRed >= 1.0 ?    pvRed * int(kOnePly) : 0);
      g_reductions[0][1][d][mc] = int16_t(nonPVRed >= 1.0 ? nonPVRed * int(kOnePly) : 0);

      g_reductions[1][0][d][mc] = g_reductions[1][1][d][mc];
      g_reductions[0][0][d][mc] = g_reductions[0][1][d][mc];

      if (g_reductions[0][0][d][mc] > 2 * kOnePly) {
        g_reductions[0][0][d][mc] += kOnePly;
      } else if (g_reductions[0][0][d][mc] > 1 * kOnePly) {
        g_reductions[0][0][d][mc] += kOnePly / 2;
      }
    }
}

Search::Search(SharedData& shared, size_t thread_id)
    : shared_(shared),
      thread_id_(thread_id) {
}

std::vector<Move> Search::GetPv() const {
  assert(!root_moves_.empty());
  return std::max_element(root_moves_.begin(), root_moves_.end())->pv;
}

const RootMove& Search::GetBestRootMove() const {
  assert(!root_moves_.empty());
  return *std::max_element(root_moves_.begin(), root_moves_.end());
}

uint64_t Search::GetNodesUnder(Move move) const {
  return std::find(root_moves_.begin(), root_moves_.end(), move)->nodes;
}

void Search::PrepareForNextSearch() {
  // 探索情報をリセットする
  num_nodes_searched_ = 0;
  max_reach_ply_ = 0;
  pv_table_.Clear();
  history_.Clear();
  countermoves_.Clear();
  followupmoves_.Clear();
  gains_.Clear();

  // マスタースレッドの場合は、スレッド間で共有する置換表の世代を更新する
  if (is_master_thread()) {
    shared_.hash_table.NextAge();
  }
}

Score Search::AlphaBetaSearch(Node& node, Score alpha, Score beta,
                              Depth depth) {
  assert(alpha < beta);

  // 探索に使うスタックを初期化する
  ResetSearchStack();

  // root_moves_を初期化する
  root_moves_.clear();
  for (ExtMove ext_move : SimpleMoveList<kAllMoves, true>(node)) {
    root_moves_.push_back(RootMove(ext_move.move));
  }

  // 探索を開始する
  if (depth > kDepthZero) {
    return MainSearch<kRootNode>(node, alpha, beta, depth, 0, false);
  } else {
    return QuiecenceSearch<kPvNode>(node, alpha, beta, depth, 0);
  }
}

Score Search::NullWindowSearch(Node& node, Score alpha, Score beta, Depth depth) {
  assert(alpha + 1 == beta);

  // 探索に使うスタックを初期化する
  ResetSearchStack();

  // 探索を開始する
  if (depth > kDepthZero) {
    return MainSearch<kNonPvNode>(node, alpha, beta, depth, 0, false);
  } else {
    return QuiecenceSearch<kNonPvNode>(node, alpha, beta, depth, 0);
  }
}

void Search::IterativeDeepening(Node& node, ThreadManager& thread_manager) {
  assert(!root_moves_.empty());

  // 統計データをリセットする
  g_mate3_tried = 0;
  g_mate3_nodes = 0;
  g_sum_move_counts = 0;
  g_num_beta_cuts = 0;
  g_cuts_by_move.clear();

  max_reach_ply_ = 0;
  num_nodes_searched_ = 0;

  TimeManager& time_manager = thread_manager.time_manager();

  // スタックの初期化を行う
  ResetSearchStack();

  // MultiPVのサイズが、ルート局面の指し手の数を超えないようにする
  multipv_ = std::min(multipv_, (int) root_moves_.size());

  // 最後にUSIのinfoコマンドを送った時間
  int64_t last_info_time = 0;

  // 反復深化を行う
  for (int iteration = 1; iteration <= limit_depth_; ++iteration) {

    // ワーカースレッドは、平均して２回に１回、スキップする
    if (!is_master_thread()) {
      const auto& half_density = g_half_density[(thread_id_ - 1) % g_half_density.size()];
      if (half_density[(iteration + node.game_ply()) % half_density.size()]) {
        continue;
      }
    }

    // αβウィンドウをセットする
    Score alpha = -kScoreInfinite, beta = kScoreInfinite;

    // 前回探索時の評価値を保存する
    for (RootMove& rm : root_moves_) {
      rm.previous_score = rm.score;
    }

    Score score = kScoreNone;

    for (pv_index_ = 0; pv_index_ < multipv_ && !shared_.signals.stop; ++pv_index_) {

      // Aspiration Windows
      Score half_window = Score(64);
      if (iteration >= 5) {
        Score previous_score = root_moves_.at(pv_index_).previous_score;
        alpha = std::max(previous_score - half_window, -kScoreInfinite);
        beta = std::min(previous_score + half_window, kScoreInfinite);
      }

      while (true) {
        // 探索を行う
        Depth depth = iteration * kOnePly;
        score = MainSearch<kRootNode>(node, alpha, beta, depth, 0, false);

        // 最善手を先頭に持ってくる
        std::stable_sort(root_moves_.begin() + pv_index_, root_moves_.end(),
                         std::greater<RootMove>());

        // 置換表からPVが消える場合があるので、置換表にPVを保存しておく
        for (int i = 0; i <= pv_index_; ++i) {
          shared_.hash_table.InsertMoves(node, root_moves_.at(i).pv);
        }

        // USIのstopコマンドが来ていたら終了する
        if (shared_.signals.stop) {
          break;
        }

        // αβウィンドウを再設定する
        if (score <= alpha) {
          // fail-low
          alpha = std::max(alpha - half_window, -kScoreInfinite);
          beta = (alpha + beta) / 2;
          // パニックモードに変更して、思考時間を延長する
          if (is_master_thread()) {
            time_manager.set_panic_mode(true);
          }
        } else if (score >= beta) {
          // fail-high
          alpha = (alpha + beta) / 2;
          beta = std::min(beta + half_window, kScoreInfinite);
        } else {
          break;
        }

        // ウィンドウを指数関数的に増加させる
        half_window += half_window / 2;
      }

      // 現在までに探索した指し手をソートする
      std::stable_sort(root_moves_.begin(), root_moves_.begin() + pv_index_ + 1,
                       std::greater<RootMove>());
    }

    assert(score != kScoreNone);

    // 経過時間を取得する
    int64_t elapsed_time = time_manager.elapsed_time();

    // 探索ノード数を取得する
    uint64_t nodes = num_nodes_searched()
                   + thread_manager.CountNodesSearchedByWorkerThreads();

    // USIのstopコマンドが来ていたら終了する
    if (shared_.signals.stop) {
      if (is_master_thread()) {
        SendUsiInfo(node, iteration, elapsed_time, nodes);
      }
      break;
    }

    if (!is_master_thread()) {
      continue;
    }

    // infoコマンドを送信する
    // なお、自玉か敵玉が詰んだ時は、短時間で大量のinfoコマンドが送られることを防止するため、
    // 少しinfoコマンドを間引いて表示する
    if (   std::abs(score) < kScoreMaxEval
        || iteration < 10
        || elapsed_time - last_info_time > 100) {
      SendUsiInfo(node, iteration, elapsed_time, nodes);
      last_info_time = elapsed_time;
    }

    // 探索ノード数の制限チェック
    if (nodes >= limit_nodes_) {
      shared_.signals.stop = true; // ワーカースレッドを停止する
      break;
    }

    // 思考時間管理のための統計情報をタイムマネージャーに送る
    if (is_master_thread()) {
      // 時間管理に用いる統計データの保存
      const RootMove& best_root_move = root_moves_.front();
      Move best_move = best_root_move.pv.front();
      uint64_t nodes_under_best_move = std::max(UINT64_C(1), best_root_move.nodes)
                                     + thread_manager.CountNodesUnder(best_move);
      time_manager.stats().num_iterations_finished = iteration;
      time_manager.stats().search_insufficiency = double(nodes) / nodes_under_best_move;
      time_manager.RecordNodesSearched(iteration, nodes);

      // パニックモードを元に戻す
      time_manager.set_panic_mode(false);

      // 次のイテレーションを回す時間が無い場合は、ここで探索を終了する
      if (!time_manager.EnoughTimeIsAvailableForNextIteration()) {
        shared_.signals.stop = true; // ワーカースレッドを停止する
        break;
      }
    }
  }
}

std::vector<RootMove> Search::CreateRootMoves(const Position& root_position,
                                              const std::vector<Move>& searchmoves,
                                              const std::vector<Move>& ignoremoves) {
  std::vector<RootMove> root_moves;

  if (!searchmoves.empty()) {
    // searchmovesオプションの指定がある場合
    for (Move move : searchmoves) {
      if (root_position.MoveIsLegal(move)) {
        root_moves.emplace_back(move);
      }
    }
  } else {
    for (ExtMove ext_move : SimpleMoveList<kAllMoves, true>(root_position)) {
      root_moves.emplace_back(ext_move.move);
    }
    // ignoremovesオプションの指定がある場合
    for (Move move : ignoremoves) {
      root_moves.erase(std::remove(root_moves.begin(), root_moves.end(), move),
                       root_moves.end());
    }
  }

  return root_moves;
}

template<Search::NodeType kNodeType>
Score Search::MainSearch(Node& node, Score alpha, Score beta, const Depth depth,
                         const int ply, const bool cut_node) {

  constexpr bool kIsRoot = kNodeType == kRootNode;
  constexpr bool kIsPv   = kNodeType == kPvNode || kNodeType == kRootNode;

  assert(-kScoreInfinite <= alpha && alpha < beta && beta <= kScoreInfinite);
  assert(kIsPv || (alpha == beta - 1));
  assert(depth > kDepthZero);
  assert(0 <= ply && ply <= kMaxPly);

  ++num_nodes_searched_;
  if (kIsPv && max_reach_ply_ < ply) {
    max_reach_ply_ = ply;
  }

  const HashEntry* entry;
  Key64 pos_key;
  Move best_move, hash_move, excluded_move;
  Score best_score, hash_score, eval;

  // ノードを初期化する
  Stack* const ss = search_stack_at_ply(ply);
  const bool in_check = node.in_check();
  bool mate3_tried = false;

  best_score = -kScoreInfinite;
  ss->current_move = ss->hash_move = (ss+1)->excluded_move = best_move = kMoveNone;
  (ss+1)->skip_null_move = false;
  (ss+1)->reduction = kDepthZero;
  (ss+2)->killers[0] = (ss+2)->killers[1] = kMoveNone;

  if (!kIsRoot) {
    // 最大手数に到達したら、探索を打ち切る
    if (shared_.signals.stop) {
      return kScoreDraw;
    } else if (ply >= kMaxPly) {
      return !in_check ? node.Evaluate() : kScoreDraw;
    }

    // 千日手等を検出する
    Score repetition_score;
    if (node.DetectRepetition(&repetition_score)) {
      if (repetition_score == kScoreDraw) {
        return draw_scores_[node.side_to_move()];
      } else {
        return repetition_score;
      }
    }

    // 入玉宣言勝ちの判定を行う
    if (node.WinDeclarationIsPossible(true)) {
      return score_mate_in(ply);
    }

    // Mate distance pruning.
    alpha = std::max(score_mated_in(ply), alpha);
    beta  = std::min(score_mate_in(ply + 1), beta);
    if (alpha >= beta) {
      return alpha;
    }
  }

  // 置換表を参照する
  excluded_move = ss->excluded_move;
  pos_key = excluded_move != kMoveNone ? node.exclusion_key() : node.key();
  entry = shared_.hash_table.LookUp(pos_key);
  hash_score = entry ? ScoreFromTt(entry->score(), ply) : kScoreNone;
  hash_move = entry ? entry->move() : kMoveNone;
  ss->hash_move = hash_move;

  // Hash Cut
  if (   !kIsPv
      && !learning_mode_
      && entry != nullptr
      && entry->depth() >= depth
      && hash_score != kScoreNone // Only in case of TT access race
      && HashCutOk<kIsPv>(entry->bound(), hash_score, beta)) {
    ss->current_move = hash_move; // hash_move == kMoveNone になりうる
    if (   hash_score >= beta
        && hash_move != kMoveNone
        && hash_move.is_quiet()
        && !in_check) {
      UpdateStats(ss, hash_move, depth, nullptr, 0);
    }
    return hash_score;
  }

  // 評価関数を呼ぶ
  eval = node.Evaluate(); // 差分計算を行うため、常に評価関数を呼ぶ
  if (in_check) {
    ss->static_score = kScoreNone;
    goto moves_loop;
  } else if (entry != nullptr) {
    // 静的評価値を保存しておく
    ss->static_score = eval;
    // 静的評価値よりも、hash scoreが信頼できる場合は、静的評価値をhash scoreで置き換える
    if (hash_score != kScoreNone) {
      if (entry->bound() & (hash_score > eval ? kBoundLower : kBoundUpper)) {
        eval = hash_score;
      }
    }
  } else {
    ss->static_score = eval;
    shared_.hash_table.Save(pos_key, kMoveNone, kScoreNone, kDepthNone, kBoundNone,
                            ss->static_score, false);
  }

  // 評価値のゲイン（１手前の局面と、現局面との評価値の差）に関する統計データを更新する
  if (   ss->static_score != kScoreNone
      && (ss-1)->static_score != kScoreNone
      && (ss-1)->current_move.is_real_move()
      && (ss-1)->current_move.is_quiet()) {
    Score gain = -(ss-1)->static_score - ss->static_score;
    gains_.Update((ss-1)->current_move, gain);
  }

  // Razoring（王手がかかっている場合は、スキップされる）
  if (   !kIsPv
      &&  depth < 4 * kOnePly
      &&  eval + razor_margin(depth) <= alpha
      &&  hash_move == kMoveNone
      &&  abs(beta) < kScoreMateInMaxPly) {
    if (   depth <= kOnePly
        && eval + razor_margin(3 * kOnePly) <= alpha) {
      return QuiecenceSearch<kNonPvNode, false>(node, alpha, beta, kDepthZero, ply);
    }

    Score ralpha = alpha - razor_margin(depth);
    Score s = QuiecenceSearch<kNonPvNode, false>(node, ralpha, ralpha+1, kDepthZero, ply);
    if (s <= ralpha) {
      return s;
    }
  }

  // 子ノードにおける futility pruning（王手がかかっている場合は、スキップされる）
  if (   !kIsPv
      && !ss->skip_null_move
      &&  depth < 7 * kOnePly
      &&  eval - futility_margin(depth) >= beta
      &&  abs(beta) < kScoreMateInMaxPly) {
    return eval - futility_margin(depth);
  }

  // ３手以内の詰みを調べる
  if (   !kIsRoot
      && (entry == nullptr || !entry->skip_mate3())) {
    mate3_tried = true;
    g_mate3_tried++;
    uint64_t m3nodes = node.nodes_searched();
    Mate3Result m3result;
    if (IsMateInThreePlies(node, &m3result)) {
      g_mate3_nodes += node.nodes_searched() - m3nodes;
      Score score = score_mate_in(ply + m3result.mate_distance);
      ss->current_move = m3result.mate_move;
      shared_.hash_table.Save(pos_key, ss->current_move, ScoreToTt(score, ply), depth,
                      kBoundExact, ss->static_score, true);
      return score;
    }
    g_mate3_nodes += node.nodes_searched() - m3nodes;
  }

  // Null move pruning（PVノードではスキップされる）
  // また、王手がかかっている場合も、スキップされる（パスすると自玉を取られてしまうため）
  if (   !kIsPv
      && !ss->skip_null_move
      &&  depth >= 2 * kOnePly
      &&  eval >= beta
      &&  abs(beta) < kScoreMateInMaxPly) {
    shared_.hash_table.Prefetch(node.key_after_null_move());

    ss->current_move = kMoveNull;
    assert(eval - beta >= 0);

    // 削減する深さの決定
    Depth R = 3 * kOnePly + depth / 4 + int(eval - beta) / 200 * kOnePly;

    node.MakeNullMove();
    (ss+1)->skip_null_move = true;
    Score null_score = depth - R < kOnePly
        ? -QuiecenceSearch<kNonPvNode, false>(node, -beta, -beta+1, kDepthZero, ply+1)
        : -MainSearch<kNonPvNode>(node, -beta, -beta+1, depth-R, ply+1, !cut_node);
    (ss+1)->skip_null_move = false;
    node.UnmakeNullMove();

    if (null_score >= beta) {
      // 仮にパスして詰むとしても、詰みの点数は返さない
      if (null_score >= kScoreMateInMaxPly) {
        null_score = beta;
      }
      return null_score;
    }
  }

  // ProbCut（王手がかかっている場合は、スキップされる）
  if (   !kIsPv
      && depth >= 5 * kOnePly
      && !ss->skip_null_move
      && abs(beta) < kScoreMateInMaxPly) {
    Score rbeta = std::min(beta + 200, kScoreInfinite);
    Depth rdepth = depth - 4 * kOnePly;

    assert(rdepth >= kOnePly);
    assert((ss-1)->current_move.is_real_move());

    MovePicker mp(node, history_, gains_, hash_move);

    double dummy;
    for (Move move; (move = mp.NextMove(&dummy)) != kMoveNone;)
      if (node.PseudoLegalMoveIsLegal(move)) {
        // Zobristハッシュキーを更新して、子局面の置換表をプリフェッチする
        Key64 key_after_move = node.key_after(move);
        shared_.hash_table.Prefetch(key_after_move);

        ss->current_move = move;

        node.MakeMove(move, node.MoveGivesCheck(move), key_after_move);
        Score score = -MainSearch<kNonPvNode>(node, -rbeta, -rbeta + 1, rdepth,
                                              ply + 1, !cut_node);
        node.UnmakeMove(move);

        if (score >= rbeta) {
          return score;
        }
      }
  }

  // 内部反復深化（王手がかかっている場合は、スキップされる）
  if (   depth >= (kIsPv ? 5 * kOnePly : 8 * kOnePly)
      && hash_move == kMoveNone
      && (kIsPv || ss->static_score + 256 >= beta)) {
    Depth d = depth - 2 * kOnePly - (kIsPv ? kDepthZero : depth / 4);

    ss->skip_null_move = true;
    MainSearch<kIsPv ? kPvNode : kNonPvNode>(node, alpha, beta, d, ply, true);
    ss->skip_null_move = false;

    entry = shared_.hash_table.LookUp(pos_key);
    hash_move = entry ? entry->move() : kMoveNone;
  }

moves_loop: // 王手がかかっている場合は、ここからスタートする

  const Array<Move, 2> countermoves = countermoves_[(ss-1)->current_move];
  const Array<Move, 2> followupmoves = followupmoves_[(ss-2)->current_move];
  MovePicker move_picker(node, history_, gains_, depth, hash_move,
                         ss->killers, countermoves, followupmoves, ss);

  const bool improving =   ss->static_score >= (ss-2)->static_score
                        || ss->static_score == kScoreNone
                        ||(ss-2)->static_score == kScoreNone;

  bool singular_extension_node =   !kIsRoot
                                && depth >= 8 * kOnePly
                                && hash_move != kMoveNone
                                && excluded_move == kMoveNone // 再帰的シンギュラー探索は行わない
                                && (entry->bound() & kBoundLower)
                                && entry->depth() >= depth - 3 * kOnePly;

  int move_count = 0;
  int searched_move_count = 0;
  int quiet_count = 0;
  Array<Move, 64> quiets_searched;
  double probability = 0;

  // βカットが生じるまで、順番に指し手を探索する
  for (Move move; (move = move_picker.NextMove(&probability)) != kMoveNone;) {
    assert(move.IsOk());

    // シンギュラー延長の探索において、除外されている手はスキップする
    if (move == excluded_move) {
      continue;
    }

    // ルート局面においては、root_moves_に登録されている指し手のみを探索する
    // （searchmovesや、ignoremoves、multipvに対応するため）
    if (kIsRoot) {
      const auto& rm = root_moves_;
      if (std::find(rm.begin() + pv_index_, rm.end(), move) == rm.end()) {
        continue;
      }
    }

    // ルートノードにおいては、非合法手はすでにスキップされているはず。
    assert(!kIsRoot || node.PseudoLegalMoveIsLegal(move));

    ++move_count;

    if (kIsRoot && move_count == 1) {
      shared_.signals.first_move_completed = false; // 最善手の探索をまだ終えていない
    }

    Depth ext = kDepthZero;
    const bool move_is_quiet = move.is_quiet();
    const bool move_gives_check = node.MoveGivesCheck(move);

    // 王手延長
    if (move_gives_check) {
      ext = !Swap::IsLosing(move, node) ? kOnePly : (kOnePly / 2);
    }

    // シンギュラー延長（突出して良い手が存在する場合に、その手を延長する）
    if (   singular_extension_node
        && move == hash_move
        && !ext
        && node.PseudoLegalMoveIsLegal(move)
        && abs(hash_score) < kScoreKnownWin) {
      assert(hash_score != kScoreNone);

      Score rbeta = hash_score - 50;
      ss->excluded_move = move;
      ss->skip_null_move = true;
      Score score = MainSearch<kNonPvNode>(node, rbeta-1, rbeta, depth / 2, ply,
                                           cut_node);
      ss->skip_null_move = false;
      ss->excluded_move = kMoveNone;

      if (score < rbeta) {
        ext = kOnePly / 2;
      }
    }

    const Depth new_depth = depth - kOnePly + ext;

    // 高速に判定できるが、荒っぽい枝刈り
    if (   !kIsRoot
        && (!kIsPv || !learning_mode_)
        && move_is_quiet
        && !in_check
        && !move_gives_check
     /* && move != ttMove Already implicit in the next condition */
        && best_score > kScoreMatedInMaxPly) {

      // Move count based pruning
      if (   depth < 16 * kOnePly
          && move_count >= futility_move_count(kIsPv, depth)
          && gains_[move] < kScoreZero
          && history_.HasNegativeScore(move)) {
        continue;
      }

      Depth r = reduction<kIsPv>(improving, depth, move_count);
      Depth predicted_depth = new_depth - r;

      // futility pruning
      if (predicted_depth < 7 * kOnePly) {
        Score futility_score = ss->static_score + futility_margin(predicted_depth)
                               + 128 + gains_[move];
        if (futility_score <= alpha) {
          best_score = std::max(best_score, futility_score);
          continue;
        }
      }

      // SEEが負の手を枝刈りする
      if (depth < 4 * kOnePly && Swap::IsLosing(move, node)) {
        continue;
      }
    }

    // 合法手か否かをチェックする
    if (   !kIsRoot
        && !node.PseudoLegalMoveIsLegal(move)) {
      move_count--;
      continue;
    }

    // Zobristハッシュキーを更新して、子局面の置換表をプリフェッチする
    Key64 key_after_move = node.key_after(move);
    shared_.hash_table.Prefetch(key_after_move);

    const bool is_pv_move = kIsPv && move_count == 1;
    ss->current_move = move;
    if (move_is_quiet && quiet_count < 64) {
      quiets_searched[quiet_count++] = move;
    }
    uint64_t nodes_before_search = num_nodes_searched_;

    // 指し手に沿って局面を進める
    node.MakeMove(move, move_gives_check, key_after_move);
    Score score = kScoreNone;
    bool do_full_depth_search = !is_pv_move;

    // 子ノードのPVをリセットする
    // 注意：ここでちゃんとリセットしておかないと、マルチスレッド探索時にPVがおかしくなる
    if (kIsPv) {
      pv_table_.ClosePv(ply + 1);
    }

    // 実現確率 及び LMR（Late Move Reduction）
    // 深さ >= 8手 では実現確率を、深さ < 8手 ではLMRを用いる。
    // 本当はすべて実現確率にしたいところだが、実現確率の計算コストが高いため、残り深さが大きい
    // ところに限って実現確率を用いている
    if (   depth >= 3 * kOnePly
        && (move_is_quiet || depth >= 8 * kOnePly)
        && move_count >= 2
        && move != ss->killers[0]
        && move != ss->killers[1]) {

      // 実現確率
      if (depth >= 8 * kOnePly) {
        // 指し手の確率に基づいて、何手減らすかを決定する
        const double kPvFactor = kIsPv ? 0.75 : 1.0;
        double consumption = kPvFactor * -std::log(probability) / std::log(2.0);
        double reduction = std::min(consumption - 1.0, kIsPv ? 4.5 : 6.0);
        // Reductionが１手未満の場合は、ほとんど減らす意味が無いので、通常の深さで探索する
        if (reduction < 1.0) {
          ss->reduction = kDepthZero;
        } else {
          ss->reduction = static_cast<Depth>(reduction * double(kOnePly));
        }

      // LMR
      } else {
        assert(move.is_quiet());

        ss->reduction = reduction<kIsPv>(improving, depth, move_count);

        if (!kIsPv && cut_node) {
          ss->reduction += kOnePly;
        } else if (history_.HasNegativeScore(move)) {
          ss->reduction += kOnePly / 2;
        }
      }

      // カウンター手は、少しreductionを抑えめにする
      if (move == countermoves[0] || move == countermoves[1]) {
        ss->reduction = std::max(kDepthZero, ss->reduction - kOnePly);
      }

      Depth d = std::max(new_depth - ss->reduction, kOnePly);

      score = -MainSearch<kNonPvNode>(node, -(alpha+1), -alpha, d, ply+1, true);

      // 削減深さが大きい場合には、中間的な深さで再探索する
      if (score > alpha && ss->reduction >= 4 * kOnePly) {
        Depth d2 = std::max(new_depth - 2 * kOnePly, kOnePly);
        score = -MainSearch<kNonPvNode>(node, -(alpha+1), -alpha, d2, ply+1, true);
      }

      do_full_depth_search = (score > alpha && ss->reduction != kDepthZero);
      ss->reduction = kDepthZero;
    }

    // 浅い探索でfail-highした場合は、通常の深さで再探索する
    if (do_full_depth_search) {
      score = new_depth < kOnePly
            ? -QuiecenceSearch<kNonPvNode>(node, -(alpha+1), -alpha, kDepthZero, ply+1)
            : -MainSearch<kNonPvNode>(node, -(alpha+1), -alpha, new_depth, ply+1, !cut_node);
    }

    // PVノードにおいて、α値を更新しそうな場合は、full-windowで再探索する
    if (kIsPv && (is_pv_move || (score > alpha && (kIsRoot || score < beta)))) {
      score = new_depth < kOnePly
            ? -QuiecenceSearch<kPvNode>(node, -beta, -alpha, kDepthZero, ply+1)
            : -MainSearch<kPvNode>(node, -beta, -alpha, new_depth, ply+1, false);
    }

    // １手前の局面に戻す
    ++searched_move_count;
    node.UnmakeMove(move);
    assert(-kScoreInfinite < score && score < kScoreInfinite);

    // 探索を停止する指示が出ている場合は、ここで打ち切る
    if (shared_.signals.stop) {
      return kScoreZero;
    }

    if (kIsRoot) {
      auto& root_moves = root_moves_;
      RootMove& rm = *std::find(root_moves.begin(), root_moves.end(), move);

      // PV及び評価値を更新する
      if (is_pv_move || score > alpha) {
        rm.score = score;
        rm.pv.resize(1);
        pv_table_.CopyPv(move, ply);
        for (size_t i = 1; i < pv_table_.size(); ++i) {
          rm.pv.push_back(pv_table_[i]);
        }
      } else {
        rm.score = -kScoreInfinite;
      }

      // 時間管理用の情報を記録する
      shared_.signals.first_move_completed = true; // 最善手の探索を終えた
      rm.nodes += (num_nodes_searched_ - nodes_before_search); // この手の探索に用いたノード数
    }

    if (score > best_score) {
      best_score = score;

      if (score > alpha) {
        best_move = move;

        if (kIsPv && !kIsRoot) {
          pv_table_.CopyPv(move, ply);
        }

        if (kIsPv && score < beta) { // 常に alpha < beta にする
          alpha = score;
        } else {
          assert(score >= beta); // fail high
          // 統計データを更新
          if (true) {
            g_sum_move_counts += searched_move_count;
            g_num_beta_cuts += 1;
            g_cuts_by_move[std::min(63, searched_move_count)] += 1;
          }
          break;
        }
      }
    }
  }

  // 現局面が詰みでないか
  if (move_count == 0) {
    if (excluded_move != kMoveNone) {
      best_score = alpha;
    } else {
      if (node.last_move().is_pawn_drop()) {
        best_score = -kScoreFoul; // 打ち歩詰め
      } else {
        best_score = score_mated_in(ply); // 詰まされた
      }
    }
  } else if (best_score >= beta && best_move.is_quiet() && !in_check) {
    // 最善手が静かな手の場合は、キラー手等を更新する
    UpdateStats(ss, best_move, depth, quiets_searched.begin(), quiet_count - 1);
  }

  shared_.hash_table.Save(pos_key, best_move, ScoreToTt(best_score, ply), depth,
                          best_score >= beta              ? kBoundLower :
                          kIsPv && best_move != kMoveNone ? kBoundExact : kBoundUpper,
                          ss->static_score,
                          mate3_tried || best_score >= kScoreMateInMaxPly);

  assert(-kScoreInfinite < best_score && best_score < kScoreInfinite);
  return best_score;
}

template <Search::NodeType kNodeType, bool kInCheck>
Score Search::QuiecenceSearch(Node& node, Score alpha, Score beta,
                              const Depth depth, const int ply) {
  constexpr bool kIsPv = kNodeType == kPvNode;

  static_assert(kNodeType == kPvNode || kNodeType == kNonPvNode, "");
  assert(kInCheck == node.in_check());
  assert(-kScoreInfinite <= alpha && alpha < beta && beta <= kScoreInfinite);
  assert(kIsPv || (alpha == beta - 1));
  assert(depth <= kDepthZero);
  assert(0 <= ply && ply <= kMaxPly);

  ++num_nodes_searched_;
  Stack* const ss = search_stack_at_ply(ply);
  const Key64 pos_key = node.key();

  if (kIsPv) {
    pv_table_.ClosePv(ply);
  }

  // 最大手数に到達したら、探索を打ち切る
  if (ply >= kMaxPly) {
    return !kInCheck ? node.Evaluate() : kScoreDraw;
  }

  // 千日手等を検出する
  Score repetition_score;
  if (node.DetectRepetition(&repetition_score)) {
    if (repetition_score == kScoreDraw) {
      return draw_scores_[node.side_to_move()];
    } else {
      return repetition_score;
    }
  }

  // 置換表を参照する
  const HashEntry* tte = shared_.hash_table.LookUp(node.key());
  const Move hash_move = tte ? tte->move() : kMoveNone;
  const Score hash_score = tte ? ScoreFromTt(tte->score(), ply) : kScoreNone;
  const Depth hash_depth = kInCheck || depth > MovePicker::kDepthQsNoChecks
                         ? MovePicker::kDepthQsChecks
                         : MovePicker::kDepthQsNoChecks;

  // Hash Cut
  if (   tte != nullptr
      && !learning_mode_
      && tte->depth() >= hash_depth
      && hash_score != kScoreNone // Only in case of TT access race
      && HashCutOk<kIsPv>(tte->bound(), hash_score, beta)) {
    ss->current_move = hash_move; // Can be MOVE_NONE
    return hash_score;
  }

  const Score old_alpha = alpha;
  Score best_score = -kScoreInfinite;
  Score futility_base = -kScoreInfinite;
  double progress;

  // 評価関数を呼んで、静止評価値を求める
  if (kInCheck) {
    node.Evaluate(&progress); // 差分計算を行うため、常に評価関数を呼ぶ
    ss->static_score = kScoreNone;
  } else {
    // １手詰関数を呼ぶ
    if (IsMateInOnePly(node, &ss->current_move)) {
      Score score = score_mate_in(ply + 1);
      shared_.hash_table.Save(pos_key, ss->current_move,
                      ScoreToTt(score, ply), kDepthZero, kBoundExact,
                      ss->static_score, true);
      return score;
    }

    if (tte != nullptr) {
      // 静的評価値をセットする
      ss->static_score = best_score = node.Evaluate(&progress);
      // 置換表の得点のほうが、静的評価値よりも信頼できる場合は、静的評価値を置換表の得点で置き換える
      if (!learning_mode_ && hash_score != kScoreNone) {
        if (tte->bound() & (hash_score > best_score ? kBoundLower : kBoundUpper)) {
          best_score = hash_score;
        }
      }
    } else {
      ss->static_score = best_score = node.Evaluate(&progress);
    }

    // stand pat（何も手を指さなくてもβ値を上回る場合は、ここでfail-highする）
    if (best_score >= beta) {
      if (tte == nullptr) {
        shared_.hash_table.Save(pos_key, kMoveNone, ScoreToTt(best_score, ply),
                                kDepthNone, kBoundLower, ss->static_score, false);
      }
      return best_score;
    }

    // α値を更新する
    if (kIsPv && best_score > alpha) {
      alpha = best_score;
    }

    futility_base = best_score + 128;
  }
  assert(0.0 <= progress && progress <= 1.0);

  // MovePickerオブジェクトを更新する
  MovePicker mp(node, history_, gains_, depth, hash_move);
  Move best_move = kMoveNone;

  // βカットするか、残りの手がなくなるまで、探索する
  double dummy;
  for (Move move; (move = mp.NextMove(&dummy)) != kMoveNone;) {
    assert(move.IsOk());

    const bool move_gives_check = node.MoveGivesCheck(move);

    // Futility pruning
    if (   !kIsPv
        && !kInCheck
        && !move_gives_check
        &&  move != hash_move
        &&  futility_base > -kScoreKnownWin) {
      Score futility_score = futility_base
          + Material::exchange_value(move.captured_piece_type());
      if (move.is_promotion()) {
        futility_score += Material::promotion_value(move.piece_type());
      }

      if (futility_score < beta) {
        best_score = std::max(best_score, futility_score);
        continue;
      }

      if (futility_base < beta && !Swap::IsWinning(move, node)) {
        best_score = std::max(best_score, futility_base);
        continue;
      }
    }

    // 枝刈りしてもよい王手回避手を検出する
    const bool evasion_prunable =    kInCheck
                                  && best_score > kScoreMatedInMaxPly
                                  && !move.is_capture();

    // SEEが負の手は枝刈りする
    if (   (!kInCheck || evasion_prunable)
        && move != hash_move
        && Swap::IsLosing(move, node)) {
      continue;
    }

    // 合法手チェックを行う
    if (!node.PseudoLegalMoveIsLegal(move)) {
      continue;
    }

    ss->current_move = move;

    // Zobristハッシュキーを更新して、子局面の置換表をプリフェッチする
    Key64 key_after_move = node.key_after(move);
    shared_.hash_table.Prefetch(key_after_move);

    // 指し手に沿って局面を進める
    node.MakeMove(move, move_gives_check, key_after_move);
    Score score = -QuiecenceSearch<kNodeType>(node, -beta, -alpha,
                                              depth - kOnePly, ply + 1);

    // １手局面を戻す
    node.UnmakeMove(move);
    assert(-kScoreInfinite < score && score < kScoreInfinite);

    // α値を上回る場合は、最善手を更新する
    if (score > best_score) {
      best_score = score;
      if (score > alpha) {
        if (kIsPv && score < beta) {
          alpha = score;
          best_move = move;
        } else {
          // fail high
          shared_.hash_table.Save(pos_key, move, ScoreToTt(score, ply), hash_depth,
                                  kBoundLower, ss->static_score,
                                  score >= kScoreMateInMaxPly);
          return score;
        }
      }
    }
  }

  // 王手回避手はすべて生成しているので、どの王手回避手を指しても詰まされる場合は、
  // 詰のスコアを返しても大丈夫
  if (kInCheck && best_score == -kScoreInfinite) {
    if (node.last_move().is_pawn_drop()) {
      return -kScoreFoul; // 打ち歩詰め
    } else {
      return score_mated_in(ply); // 詰まされた
    }
  }

  // ３手詰関数を呼ぶ（王手がかかっている場合は、スキップする）
  if (   !kInCheck
      && (tte == nullptr || !tte->skip_mate3())) {
    Mate3Result m3result;
    g_mate3_tried++;
    uint64_t m3nodes = node.nodes_searched();
    if (IsMateInThreePlies(node, &m3result)) {
      g_mate3_nodes += node.nodes_searched() - m3nodes;
      Score score = score_mate_in(ply + m3result.mate_distance);
      ss->current_move = m3result.mate_move;
      shared_.hash_table.Save(pos_key, ss->current_move, ScoreToTt(score, ply),
                      kDepthZero, kBoundExact, ss->static_score, true);
      return score;
    } else {
      g_mate3_nodes += node.nodes_searched() - m3nodes;
    }
  }

  shared_.hash_table.Save(pos_key, best_move, ScoreToTt(best_score, ply), hash_depth,
                          kIsPv && best_score > old_alpha ? kBoundExact : kBoundUpper,
                          ss->static_score, true);

  // PVを保存する
  if (   kIsPv
      && best_move != kMoveNone
      && best_score > old_alpha) { // stand patよりも、1手指したほうがよい場合
    pv_table_.CopyPv(best_move, ply);
  }

  assert(-kScoreInfinite < best_score && best_score < kScoreInfinite);
  return best_score;
}

void Search::UpdateStats(Stack* const ss, Move move, Depth depth,
                         Move* const quiets, int quiets_count) {
  assert(ss != nullptr);
  assert(quiets != nullptr || quiets_count == 0);

  // 1. キラー手を更新する
  if (ss->killers[0] != move) {
    ss->killers[1] = ss->killers[0];
    ss->killers[0] = move;
  }

  // 2. ヒストリー値を更新する
  history_.UpdateSuccess(move, depth);
  for (int i = 0; i < quiets_count; ++i) {
    assert(quiets[i] != move);
    history_.UpdateFail(quiets[i], depth);
  }

  // 3. カウンター手とフォローアップ手を更新する
  if ((ss-1)->current_move.is_real_move()) {
    countermoves_.Update((ss-1)->current_move, move);
  }
  if (   (ss-2)->current_move.is_real_move()
      && (ss-1)->current_move == (ss-1)->hash_move) {
    followupmoves_.Update((ss-2)->current_move, move);
  }
}

void Search::SendUsiInfo(const Node& node, int depth, int64_t time,
                         uint64_t nodes, Bound bound) const {
  // ゼロ除算を防止するため、最低１ミリ秒は経過したことにする
  time = std::max(time, INT64_C(1));

  // infoコマンドを一時的に貯めておくためのバッファ
  std::string buf;

  // マルチPVのループ
  for (int pv_index = multipv_ - 1; pv_index >= 0; --pv_index) {
    Score score = root_moves_.at(pv_index).score;

    // 1. 評価値とPV以外
    buf += "info";
    buf += " depth "    + std::to_string(depth);
    buf += " seldepth " + std::to_string(max_reach_ply_ + 1);
    buf += " time "     + std::to_string(time);
    buf += " nodes "    + std::to_string(nodes);
    buf += " nps "      + std::to_string((1000 * nodes) / time);
    buf += " hashfull " + std::to_string(shared_.hash_table.hashfull());

    // 2. 評価値
    if (score >= kScoreMateInMaxPly) {
      // a. 自分の勝ちを読みきった場合
      score = std::min(score, score_mate_in(1));
      buf += " score mate " + std::to_string(static_cast<int>(kScoreMate - score));
    } else if (score <= kScoreMatedInMaxPly) {
      // b. 自分の負けを読みきった場合
      score = std::max(score, score_mated_in(2));
      buf += " score mate -" + std::to_string(static_cast<int>(kScoreMate + score));
    } else {
      // c. 勝敗を読みきっていない場合
      buf += " score cp " + std::to_string(static_cast<int>(score));
      if (bound == kBoundLower) {
        buf += " lowerbound";
      } else if (bound == kBoundUpper) {
        buf += " upperbound";
      }
    }

    // 3. PV
    buf += " multipv " + std::to_string(pv_index + 1);
    buf += " pv";
    for (Move move : root_moves_.at(pv_index).pv) {
      buf += " " + move.ToSfen();
    }
    if (depth >= 3) {
      const std::vector<Move>& pv = root_moves_.at(pv_index).pv;
      // PVの長さが短すぎる場合は、置換表から残りの読み筋を取得する
      if (pv.size() <= 2U) {
        for (Move move : shared_.hash_table.ExtractMoves(node, pv)) {
          buf += " " + move.ToSfen();
        }
      }
    }

    // 4. 改行コード
    buf += "\n";
  }

  // infoコマンドをまとめて標準出力へ出力する
  SYNCED_PRINTF("%s", buf.c_str());
}
