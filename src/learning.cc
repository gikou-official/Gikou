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

#include "learning.h"

#include <cinttypes>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <random>
#include <thread>
#include <omp.h>
#include "common/progress_timer.h"
#include "evaluation.h"
#include "gamedb.h"
#include "huffman_code.h"
#include "material.h"
#include "movegen.h"
#include "progress.h"
#include "search.h"
#include "teacher_data.h"

#if !defined(MINIMUM)

namespace {

// デバッグ設定
constexpr bool kVerboseMessage = false; // デバッグメッセージを多めに出力するか否か

// 学習の設定
constexpr int kNumIteration = 10000; // イテレーション数（パラメータを更新する回数）
constexpr int kNumGames = 30000;     // 学習に使用する棋譜の数
constexpr int kNumTestSet = 1000;    // 指し手の一致率テストに使用する棋譜の数
constexpr int kBatchSize = 8000;     // 勾配を計算するのに用いる局面の数（ミニバッチの大きさ）

// 探索の設定
constexpr int kMinSearchDepth = 1; // PVを求めるために行われる探索の、最小深さ
constexpr int kMaxSearchDepth = 2; // PVを求めるために行われる探索の、最大深さ

// 損失関数の設定
constexpr float kWinRateCoefficient = 100.0f;   // 損失関数の第２項（勝率予測の誤差）に掛ける係数
constexpr float kOscillationCoefficient = 0.1f; // 損失関数の第３項（異なる深さ間の誤差）に掛ける係数
constexpr float kL1Penalty = 10.0f * (kBatchSize / (kNumGames * 100.0f)); // 正則化項（L1ペナルティ）の係数

// RootStrapの設定
constexpr float kRootStrapCoefficient = 2000.0f; // RootStrapの損失項に掛ける係数
constexpr int kRootStrapBatchSize = 80000;       // RootStrapの勾配計算に用いる局面数（ミニバッチの大きさ）

// ロジスティック回帰損失（「激指」方式）の設定
constexpr float kLogisticRegressionCoefficient = 25.0f; // ロジスティック回帰の損失項に掛ける係数
constexpr int kLogisticRegressionBatchSize = 80000;     // ロジスティック回帰の勾配計算に用いる局面数

// 学習率の設定（RMSpropの設定）
constexpr float kRmsPropStepRate = 5.0f; // １回の更新で、最大何点まで各パラメータを動かすか
constexpr float kRmsPropDecay = 0.9975f; // どの程度過去の勾配を重視するか（大きいほど過去の勾配を重視）
constexpr float kRmsPropEpsilon = 1e-4f; // ゼロ除算を防止するために分母に加算される、非常に小さな数

// 平均化SGDの設定
constexpr float kAveragedSgdDecay = 0.9995f; // 指数移動平均の減衰率（大きいほど過去のパラメータを重視）

/**
 * 学習時の統計データをまとめて保存するためのクラスです.
 */
struct LearningStats {
  LearningStats& operator+=(const LearningStats& rhs) {
    loss                        += rhs.loss;
    penalty                     += rhs.penalty;
    win_rate_loss               += rhs.win_rate_loss;
    win_rate_error              += rhs.win_rate_error;
    win_rate_samples            += rhs.win_rate_samples;
    oscillation_loss            += rhs.oscillation_loss;
    oscillation_error           += rhs.oscillation_error;
    oscillation_samples         += rhs.oscillation_samples;
    rootstrap_loss              += rhs.rootstrap_loss;
    rootstrap_error             += rhs.rootstrap_error;
    rootstrap_samples           += rhs.rootstrap_samples;
    logistic_regression_loss    += rhs.logistic_regression_loss;
    logistic_regression_error   += rhs.logistic_regression_error;
    logistic_regression_samples += rhs.logistic_regression_samples;
    num_positions               += rhs.num_positions;
    num_right_answers           += rhs.num_right_answers;
    num_moves                   += rhs.num_moves;
    num_samples                 += rhs.num_samples;
    num_nodes                   += rhs.num_nodes;
    return *this;
  }
  float loss                        = 0.0f; // 損失関数全体の損失
  float penalty                     = 0.0f; // ペナルティ項
  float win_rate_loss               = 0.0f; // 第２項（勝率予測）の損失
  float win_rate_error              = 0.0f; // 勝率予測の誤差（標準偏差を計算するために用いる）
  float win_rate_samples            = 0.0f; // 第２項（勝率予測）の学習サンプル数
  float oscillation_loss            = 0.0f; // 第３項（異なる深さ間の誤差）の損失
  float oscillation_error           = 0.0f; // 異なる深さ間の誤差（標準偏差を計算するために用いる）
  float oscillation_samples         = 0.0f; // 第３項（異なる深さ間の誤差）の学習サンプル数
  float rootstrap_loss              = 0.0f; // RootStrapの損失
  float rootstrap_error             = 0.0f; // RootStrapの誤差
  float rootstrap_samples           = 0.0f; // RootStrapの学習サンプル数
  float logistic_regression_loss    = 0.0f; // ロジスティック回帰の損失
  float logistic_regression_error   = 0.0f; // ロジスティック回帰の誤差
  float logistic_regression_samples = 0.0f; // ロジスティック回帰の学習サンプル数
  int num_positions     = 0;
  int num_right_answers = 0;
  int num_moves         = 0;
  int num_samples       = 0;
  int num_nodes         = 0;
};

/**
 * 駒割の値を初期化します.
 */
template<typename ParamsType>
void ResetMaterialValues(ParamsType* const params) {
  assert(params != nullptr);
  // Bonanza 6.0の駒割を初期値にする
  // 実装上は、Bonanzaの値に100/88を掛けた値を用いている（歩の点数を100点に揃えるため）
  // （参考文献）
  //   保木邦仁: Bonanza - The Computer Shogi Program, http://www.geocities.jp/bonanza_shogi/.
  params->material[kPawn   ] =  100.0f;
  params->material[kLance  ] =  267.0f;
  params->material[kKnight ] =  295.0f;
  params->material[kSilver ] =  424.0f;
  params->material[kGold   ] =  510.0f;
  params->material[kBishop ] =  654.0f;
  params->material[kRook   ] =  738.0f;
  params->material[kPPawn  ] =  614.0f;
  params->material[kPLance ] =  562.0f;
  params->material[kPKnight] =  586.0f;
  params->material[kPSilver] =  569.0f;
  params->material[kHorse  ] =  951.0f;
  params->material[kDragon ] = 1086.0f;
}

/**
 * 棋譜DBから棋譜を読み込みます.
 * @param num_games DBから読み込む棋譜の数
 * @param begin     何番目以降の棋譜を読み込むか
 * @return 読み込んだ棋譜
 */
std::vector<Game> ExtractGamesFromDatabase(const size_t num_games,
                                           const size_t begin = 0) {
  // 1. データベースを準備する
  std::ifstream db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(db_file);
  game_db.set_title_matches_only(true);

  // 2. 必要な数だけ、棋譜を抽出する
  std::vector<Game> games;
  size_t game_index = 0;
  for (Game game; game_db.ReadOneGame(&game) && games.size() < num_games; ) {
    if (game_index++ < begin) {
      continue;
    }
    games.push_back(game);
  }

  return games;
}

/*
 * 損失関数の勾配を更新します.
 */
void UpdateGradient(const Position& pos, const float delta,
                    Gradient* const gradient) {
  assert(gradient != nullptr);
  PsqList psq_list(pos);
  float progress = static_cast<float>(Progress::EstimateProgress(pos, psq_list));
  gradient->Update(pos, psq_list, delta, progress);
}


/**
 * 「棋譜の手との不一致率」（idea from 激指）の損失項について、勾配を計算します.
 */
void ComputeGradientOfDisagreementLoss(
    const std::vector<std::vector<Move>>& pv_list,
    const std::valarray<int>& scores, const int margin, Position& pos,
    Gradient* const gradient, LearningStats* const stats) {
  assert(pv_list.size() == scores.size());
  assert(margin >= 0);
  assert(gradient != nullptr);
  assert(stats != nullptr);

  // 1. 分類に失敗した手（評価値＋マージン >= 棋譜の手の評価値となった手）があるか調べる
  std::valarray<float> deltas(scores.size());
  int num_failed_moves = 0;
  for (size_t i = 1; i < scores.size(); ++i) {
    if ((scores[i] + margin) >= scores[0]) {
      deltas[i] = 1.0f;
      num_failed_moves += 1;
    } else {
      deltas[i] = 0.0f;
    }
  }

  // 2. 勾配ベクトルを変化させる量（delta）を指し手ごとに計算する
  if (num_failed_moves != 0) {
    stats->loss += (scores.max() + margin) - scores[0];
    deltas /= static_cast<float>(num_failed_moves);
    deltas[0] = -1.0f;
  } else {
    deltas[0] = 0.0f;
  }

  // 3. 各指し手ごとに、勾配ベクトルを更新する
  for (size_t i = 0; i < pv_list.size(); ++i) {
    // PVが存在しない手 or 勾配ベクトルを動かす必要がない手 はスキップする
    if (pv_list.at(i).empty() || deltas[i] == 0.0f) {
      continue;
    }

    // 後手番の局面の場合は、符号を逆にする
    float delta = pos.side_to_move() == kBlack ? deltas[i] : -deltas[i];

    // リーフノードへ移動する
    for (Move m : pv_list.at(i)) {
      pos.MakeMove(m);
    }

    // 勾配のアップデート
    UpdateGradient(pos, delta, gradient);
    stats->num_samples += 1;

    // 元の局面に戻る
    for (auto it = pv_list.at(i).rbegin(); it != pv_list.at(i).rend(); ++it) {
      pos.UnmakeMove(*it);
    }
  }
}

/**
 * 「勝率予測と勝敗との負の対数尤度」（idea from 習甦）の損失項について、勾配を計算します.
 */
void ComputeGradientOfLogLiklihoodLoss(const Position& pos,
                                       const float progress, const Color winner,
                                       Gradient* const gradient,
                                       LearningStats* const stats) {
  assert(0.0f <= progress && progress <= 1.0f);
  assert(gradient != nullptr);
  assert(stats != nullptr);

  // 1. 評価値を勝率に変換する
  // （参考文献）
  //    山本一成: 評価値と勝率との関係, comment on twitter,
  //    https://twitter.com/issei_y/status/589642166818877440, 2015.
  const float kGain = 1.0f / 600.0f;
  float root_score = Evaluation::Evaluate(pos);
  float t = pos.side_to_move() == winner ? 1.0f : 0.0f;
  float y = math::sigmoid(kGain * root_score);

  // 2. この損失項に掛ける係数を決定する
  // 以下の参考文献によると、終盤になるほど係数を大きくするのが良いようです。
  // （参考文献）
  //     佐藤佳州: ゲームにおける棋譜の性質と強さの関係に基づいた学習, pp.66-67, 2014.
  float c = kWinRateCoefficient * std::min(std::max(progress, 0.25f), 0.75f);

  // 3. 勾配の更新を行う
  float d = c * kGain * (y - t);
  float delta = pos.side_to_move() == kBlack ? d : -d;
  UpdateGradient(pos, delta, gradient);

  // 4. 損失値を計算する
  stats->win_rate_loss += c * -(t * std::log(y) + (1.0f - t) * std::log(1.0f - y));
  stats->win_rate_error += (y - t) * (y - t);
  stats->win_rate_samples += 1.0f;
}

/**
 * 「浅い探索結果と深い探索結果との誤差」（idea from 習甦）の損失項について、勾配を計算します.
 */
void ComputeGradientOfOscillationLoss(
    const std::vector<std::vector<Move>>& pv_list, const int num_pvs,
    const std::valarray<int>& scores, Position& pos, Gradient* const gradient,
    LearningStats* const stats) {
  assert(gradient != nullptr);
  assert(stats != nullptr);

  for (size_t i = 0; i < pv_list.size(); ++i) {
    // PVの長さが2手未満の場合は、比較対照となる「深い探索結果」が存在しないので、スキップする
    if (pv_list.at(i).size() < 2U) {
      continue;
    }

    const Move move = pv_list.at(i).front();

    // シグモイド関数とその導関数を準備
    const float kGain = 1.0f / 600.0f;
    auto sigmoid = [kGain](float x) -> float {
      return math::sigmoid(kGain * x);
    };
    auto d_sigmoid = [kGain](float x) -> float {
      return kGain * math::derivative_of_sigmoid(kGain * x);
    };

    // 微分値を計算する
    // （「生の評価値の誤差」と「予想勝率の誤差」の双方を、バランスよく最小化するように配慮している）
    pos.MakeMove(move);
    float sign = pos.side_to_move() == kBlack ? 1.0f : -1.0f;
    float c = kOscillationCoefficient / float(num_pvs);
    float score_y = Evaluation::Evaluate(pos); // 浅い探索の評価値（生の評価値）
    float score_t = -scores[i];                // 深い探索の評価値（生の評価値）
    float y = sigmoid(score_y); // 浅い探索での予想勝率（評価値を勝率換算したもの）
    float t = sigmoid(score_t); // 深い探索での予想勝率（評価値を勝率換算したもの）
    float delta_y = sign * c * (d_sigmoid(score_y) * (score_y - score_t) + (y - t));
    float delta_t = sign * c * (d_sigmoid(score_t) * (score_t - score_y) + (t - y));

    // 親局面の勾配のアップデート
    UpdateGradient(pos, delta_y, gradient);

    // 子局面の勾配のアップデート
    for (auto it = pv_list.at(i).begin() + 1; it != pv_list.at(i).end(); ++it) {
      pos.MakeMove(*it);
    }
    UpdateGradient(pos, delta_t, gradient);
    for (auto it = pv_list.at(i).rbegin(); it != pv_list.at(i).rend(); ++it) {
      pos.UnmakeMove(*it);
    }

    // 損失・誤差の計算
    stats->oscillation_loss    += c * (y - t) * (score_y - score_t);
    stats->oscillation_error   += (score_y - score_t) * (score_y - score_t);
    stats->oscillation_samples += 1.0f;
  }
}

/**
 * RootStrapの損失項について、勾配を計算します.
 */
void ComputeGradientOfRootStrapLoss(const TeacherPosition& teacher_position,
                                    SharedData& shared_data,
                                    Gradient* const gradient,
                                    LearningStats* const stats) {
  assert(gradient != nullptr);
  assert(stats != nullptr);

  Position pos = HuffmanCode::DecodePosition(teacher_position.huffman_code);

  // 1. 静止探索を行う
  Node node(pos);
  Search search(shared_data);
  search.PrepareForNextSearch();
  Score alpha = -kScoreKnownWin, beta = kScoreKnownWin;
  Score score = search.AlphaBetaSearch(node, alpha, beta, kDepthZero);
  if (std::abs(score) >= kScoreKnownWin) {
    return;
  }

  // 2. 評価値を勝率に変換する
  const float kGain = 1.0f / 600.0f;
  float float_score = score;
  float t = math::sigmoid(kGain * float(teacher_position.score));
  float y = std::min(std::max(math::sigmoid(kGain * float_score), 0.001), 0.999);

  // 3. 勾配ベクトルの更新を行う
  float c = kRootStrapCoefficient;
  float d = c * kGain * (y - t) * math::derivative_of_sigmoid(kGain * y);
  float delta = pos.side_to_move() == kBlack ? d : -d;
  for (size_t i = 0; i < search.pv_table().size(); ++i) {
    pos.MakeMove(search.pv_table()[i]);
  }
  UpdateGradient(pos, delta, gradient);

  // 4. 損失値を計算する
  stats->rootstrap_loss += c * (y - t) * (y - t);
  stats->rootstrap_error += (y - t) * (y - t);
  stats->rootstrap_samples += 1.0f;
}

/**
 * 「自己対戦棋譜の勝敗データとのロジスティック回帰」についての損失項です（idea from 激指）.
 *
 * （参考文献）
 *   - 鶴岡慶雅, 横山大作, 丸山孝志, 高瀬亮, 大内拓実: 「激指」アピール文書（WCSC26）,
 *     http://www.computer-shogi.org/wcsc26/appeal/Gekisashi/appeal.txt, 2016.
 */
void ComputeGradientOfLogisticRegressionLoss(const TeacherPosition& teacher_position,
                                             Gradient* const gradient,
                                             LearningStats* const stats) {
  // ハフマン符号で圧縮されている局面をデコードする
  Position pos = HuffmanCode::DecodePosition(teacher_position.huffman_code);

  // 1. 局面の現在の評価値と、教師値を求める
  const float kGain = 1.0f / 600.0f;
  float score = Evaluation::Evaluate(pos);
  float t = teacher_position.score == kScoreKnownWin ? 1.0f : 0.0f;
  float y = std::min(std::max(math::sigmoid(kGain * score), 0.001), 0.999);

  // 2. 勾配の更新を行う
  float c = kLogisticRegressionCoefficient;
  float d = c * kGain * (y - t);
  float delta = pos.side_to_move() == kBlack ? d : -d;
  UpdateGradient(pos, delta, gradient);

  // 3. 損失値を計算する
  stats->logistic_regression_loss += c * -(t * std::log(y) + (1.0f - t) * std::log(1.0f - y));
  stats->logistic_regression_error += (y - t) * (y - t);
  stats->logistic_regression_samples += 1.0f;
}

/**
 * １つの特定の局面について、損失関数の勾配を計算します.
 *
 * なお、現在の実装では、評価関数の学習に用いる損失関数は、
 *    - 第1項: 棋譜の手との不一致率（idea from 激指）
 *    - 第2項: 勝率予測と勝敗との負の対数尤度（idea from 習甦）
 *    - 第3項: 浅い探索結果と深い探索結果との誤差（idea from 習甦）
 * の３つから構成されています。
 *
 * （参考文献）
 *   - 鶴岡慶雅: 「激指」の最近の改良について --コンピュータ将棋と機械学習--,
 *     『コンピュータ将棋の進歩６』, pp.77-81, 共立出版, 2012.
 *   - 竹内章: 習甦の誕生, 『人間に勝つコンピュータ将棋の作り方』, pp.184-189, 技術評論社, 2012.
 *   - 佐藤佳州: ゲームにおける棋譜の性質と強さの関係に基づいた学習, pp.66-67, 2014.
 *
 * @param pos              損失関数の勾配を計算したい局面
 * @param shared_data      探索中のデータ
 * @param teacher_move     教師とすべき、棋譜の手
 * @param progress         局面の進行度（初期局面が0で、投了局面が1となる値）
 * @param winner           その対局で勝った側の手番
 * @param mersenne_twister 乱数生成器（メルセンヌ・ツイスタ）
 * @param gradient         損失関数の勾配
 * @return 学習中の統計データ
 */
LearningStats ComputeGradient(Position& pos, SharedData& shared_data,
                              const Move teacher_move, const float progress,
                              const Color winner,
                              std::mt19937& mersenne_twister,
                              Gradient* const gradient) {
  assert(gradient != nullptr);

  LearningStats stats;

  // 棋譜の手が、非合法手または飛角の不成などの場合は、学習の対象としない
  if (!pos.MoveIsLegal(teacher_move) || teacher_move.IsInferior()) {
    return stats;
  }

  // 合法手を生成する
  SimpleMoveList<kAllMoves, true> legal_moves(pos);
  if (legal_moves.size() <= 1) {
    return stats;
  }

  // 棋譜の手を合法手リストの先頭に持ってくる
  auto iter = std::find_if(legal_moves.begin(), legal_moves.end(),
                           [teacher_move](ExtMove em) {
    return em.move == teacher_move;
  });
  std::iter_swap(legal_moves.begin(), iter);
  assert(legal_moves[0].move == teacher_move);

  // 探索の準備をする
  Search search(shared_data);
  search.set_learning_mode(true);
  search.PrepareForNextSearch();
  shared_data.countermoves_history.Clear();
  Node node(pos);

  // 子ノードを探索する
  std::vector<std::vector<Move>> pv_list(legal_moves.size());
  std::valarray<int> scores(legal_moves.size());
  const int margin = static_cast<int>(10.0f + 256.0f * progress);
  Score alpha = -kScoreKnownWin;
  const Score beta = kScoreKnownWin;
  Score best_score = -kScoreInfinite;
  Move best_move = kMoveNone;
  int num_pvs = 0;
  for (size_t i = 0; i < legal_moves.size(); ++i) {
    const Move move = legal_moves[i].move;

    // 学習時の探索深さの決定（最小深さから最大深さの範囲で、ランダムに探索深さを選ぶ）
    // （参考文献）
    //   平岡拓也: Apery on GitHub, https://github.com/HiraokaTakuya/apery.
    std::uniform_int_distribution<int> dis(kMinSearchDepth, kMaxSearchDepth);
    const Depth depth = dis(mersenne_twister) * kOnePly;

    // １手進める
    node.MakeMove(move);
    node.Evaluate(); // 評価関数の差分計算を行うために必要

    // null window search
    Score score = -kScoreInfinite;
    if (move != teacher_move) {
      score = -search.NullWindowSearch(node, -(alpha+1), -alpha, depth);
    }

    // full window search（null window searchで、α値を超えた場合のみ）
    if (score > alpha || move == teacher_move) {
      score = -search.AlphaBetaSearch(node, -beta, -alpha, depth);
    }

    // １手戻す
    node.UnmakeMove(move);

    // 詰みを見つけた場合は探索を打ち切る（学習に適さないと考えられるため）
    if (score >= kScoreKnownWin || (move == teacher_move && score <= alpha)) {
      stats.loss              = 0.0f;
      stats.num_positions     = 0;
      stats.num_right_answers = 0;
      stats.num_moves         = 0;
      stats.num_samples       = 0;
      stats.num_nodes         = search.num_nodes_searched();
      return stats;
    }

    // 探索結果を保存する
    if (alpha < score && score < beta) {
      scores[i] = score;
      pv_list.at(i).push_back(move);
      for (Move m : search.pv_table()) {
        pv_list.at(i).push_back(m);
      }
      num_pvs += 1;
    } else {
      scores[i] = -kScoreInfinite; // ウィンドウを外れたことを示すため、スコアを-∞とする
    }

    // αβ探索のウィンドウを再設定する
    if (score > best_score) {
      if (move == teacher_move) {
        alpha = std::max(score - margin, -kScoreKnownWin);
      }
      best_score = score;
      best_move = move;
    }
  }

  //
  // 損失関数の第1項: 棋譜の手との不一致率（idea from 激指）
  //
  // 基本的には、激指と同様の損失項（マージンありパーセプトロン）を用いています。
  // いろいろ実験してみましたが、マージンの値は、激指と同様に、
  //   10 + 256 * progress
  // と設定するのが結局一番よいようです。
  //
  // （参考文献）
  //     鶴岡慶雅: 「激指」の最近の改良について --コンピュータ将棋と機械学習--,
  //     『コンピュータ将棋の進歩６』, pp.77-81, 共立出版, 2012.
  ComputeGradientOfDisagreementLoss(pv_list, scores, margin, pos, gradient, &stats);

  //
  // 損失関数の第2項: 勝率予測と勝敗との負の対数尤度（idea from 習甦）
  //
  // この損失項を入れることで、評価関数の値と勝率がより正確に対応するようになります。
  // そのため、例えば「負けそうな局面なのに、評価値が過大評価される」といったことが減少するようです。
  // 印象としては、終盤の評価が正確になり、終盤で反省することが少なくなるようです。
  //
  // （参考文献）
  //     竹内章: 習甦の誕生, 『人間に勝つコンピュータ将棋の作り方』, pp.184-189, 技術評論社, 2012.
  ComputeGradientOfLogLiklihoodLoss(pos, progress, winner, gradient, &stats);

  //
  // 損失関数の第3項: 浅い探索結果と深い探索結果との誤差（idea from 習甦）
  //
  // この損失項を入れることで、探索深さを変えても、局面の評価値がブレにくくなります。
  // そのため、例えば「20手読みまでは評価値が+200だったのに、21手読みをした途端に-800まで転落」
  // といった事態が少なくなるようです。
  // 印象としては、評価値の推移が滑らかになるようです。
  //
  // （参考文献）
  //     竹内章: 習甦の誕生, 『人間に勝つコンピュータ将棋の作り方』, pp.184-189, 技術評論社, 2012.
  ComputeGradientOfOscillationLoss(pv_list, num_pvs, scores, pos, gradient, &stats);

  stats.num_positions     = 1;
  stats.num_right_answers = (best_move == teacher_move);
  stats.num_moves         = legal_moves.size();
  stats.num_nodes         = search.num_nodes_searched();

  return stats;
}

/**
 * 勾配ベクトルについて、いわゆる次元下げを適用します.
 * @param gradient            勾配ベクトル（input）
 * @param convoluted_gradient 次元下げが適用された勾配ベクトル（output）
 */
void ConvoluteGradient(const std::unique_ptr<Gradient>& gradient,
                       std::unique_ptr<ExtendedParams>& convoluted_gradient) {
  // 短い別名をつける
  std::unique_ptr<ExtendedParams>& cv = convoluted_gradient;

  // 1. KPを次元下げ
#pragma omp parallel for schedule(static)
  for (int i = Square::min(); i <= Square::max(); ++i) {
    Square king_sq(i);
    for (PsqIndex psq : PsqIndex::all_indices()) {
      ExtendedParams::Updater updater(gradient->king_piece[king_sq][psq]);
      cv->EachKP<kBlack>(king_sq, psq, updater);
    }
  }

  // 2. PPを次元下げ
#pragma omp parallel for schedule(static)
  for (int i = PsqIndex::min(); i <= PsqIndex::max(); ++i) {
    PsqIndex psq1(i);
    for (PsqIndex psq2 : PsqIndex::all_indices()) {
      ExtendedParams::Updater updater(gradient->two_pieces[psq1][psq2]);
      cv->EachPP(psq1, psq2, updater);
    }
  }

  const auto all_pieces_plus_no_piece = Piece::all_pieces().set(kNoPiece);

  // 3. 各マスの利き評価を次元下げ
#pragma omp parallel for schedule(static)
  for (int i = PsqControlIndex::min(); i <= PsqControlIndex::max(); ++i) {
    PsqControlIndex index(i);
    if (index.IsOk()) {
      for (Square ksq : Square::all_squares()) {
        ExtendedParams::Updater updater_b(gradient->controls[kBlack][ksq][index]);
        ExtendedParams::Updater updater_w(gradient->controls[kWhite][ksq][index]);
        cv->EachControl<kBlack>(ksq, index, updater_b);
        cv->EachControl<kWhite>(ksq, index, updater_w);
      }
    }
  }

  // 4. 玉の安全度を次元下げ
#pragma omp parallel for schedule(static)
  for (unsigned h = HandSet::min(); h <= HandSet::max(); ++h)
    for (int d = 0; d < 8; ++d)
      for (Piece piece : all_pieces_plus_no_piece)
        for (int attacks = 0; attacks < 4; ++attacks)
          for (int defenses = 0; defenses < 4; ++defenses) {
            HandSet hand_set(h);
            Direction dir = static_cast<Direction>(d);
            ExtendedParams::Updater updater(gradient->king_safety[hand_set][dir][piece][attacks][defenses]);
            cv->EachKingSafety<kBlack>(hand_set, dir, piece, attacks, defenses, updater);
          }

  // 5. 飛車・角・香車の利きを次元下げ
#pragma omp parallel for schedule(static)
  for (int s = Square::min(); s <= Square::max(); ++s) {
    Square i(s);
    for (Color c : {kBlack, kWhite})
      for (Square j : Square::all_squares())
        for (Square k : Square::all_squares()) {
          ExtendedParams::Updater updater_r(gradient->rook_control[c][i][j][k]);
          ExtendedParams::Updater updater_b(gradient->bishop_control[c][i][j][k]);
          ExtendedParams::Updater updater_l(gradient->lance_control[c][i][j][k]);
          cv->EachSliderControl<kBlack, kRook>(c, i, j, k, updater_r);
          cv->EachSliderControl<kBlack, kBishop>(c, i, j, k, updater_b);
          cv->EachSliderControl<kBlack, kLance>(c, i, j, k, updater_l);
        }

    for (Square j : Square::all_squares())
      for (Piece p : Piece::all_pieces()) {
        ExtendedParams::Updater updater_r(gradient->rook_threat[i][j][p]);
        ExtendedParams::Updater updater_b(gradient->bishop_threat[i][j][p]);
        ExtendedParams::Updater updater_l(gradient->lance_threat[i][j][p]);
        cv->EachThreat<kBlack, kRook>(i, j, p, updater_r);
        cv->EachThreat<kBlack, kBishop>(i, j, p, updater_b);
        cv->EachThreat<kBlack, kLance>(i, j, p, updater_l);
      }
  }

  // 6. 手番をコピー
  cv->tempo = gradient->tempo;
}

/*
 * 計算した勾配を使って、評価関数のパラメータを更新します.
 *
 * パラメータの更新には、RMSprop及びFOBOSを用いて、素早く学習が収束するようにしています。
 *
 * （参考文献）
 *   - 海野裕也, et al.: 『オンライン機械学習』, pp.95-96, 講談社, 2015.
 *   - John Duchi, Yoram Singer: Efficient Learning with Forward-Backward Splitting,
 *     http://web.stanford.edu/~jduchi/projects/DuchiSi09c_slides.pdf, p.12, 2009.
 */
LearningStats UpdateParams(const std::unique_ptr<ExtendedParams>& gradient,
                           std::unique_ptr<ExtendedParams>& accumulated_gradient,
                           std::unique_ptr<ExtendedParams>& params) {
  if (kVerboseMessage) {
    std::printf("Update params.\n");
  }

  float l1_penalty = 0.0f;

#pragma omp parallel for reduction(+:l1_penalty) schedule(static)
  for (size_t i = 0; i < gradient->size(); ++i) {
    auto& v = *(params->begin() + i);
    auto& a = *(accumulated_gradient->begin() + i);
    auto g = *(gradient->begin() + i);

    // ペナルティ項の現在の値を計算する（駒割にはペナルティをかけない）
    if (i >= gradient->size_of_material()) {
      for (size_t j = 0; j < v.size(); ++j) {
        l1_penalty += std::abs(v[j]);
      }
    }

    // 更新幅の設定（RMSprop）
    a = a * kRmsPropDecay + (g * g);
    const PackedWeight eta = kRmsPropStepRate / (a + kRmsPropEpsilon).apply(std::sqrt);

    // パラメータを更新（坂を下る）
    v -= eta * g;

    // 駒割り以外のパラメータについてのみ、ペナルティをかける（FOBOS）
    if (i >= gradient->size_of_material()) {
      auto ramp = [](float x) {
        return std::max(x, 0.0f);
      };
      v = v.apply(math::sign) * (v.apply(std::abs) - eta * kL1Penalty).apply(ramp);
    }
  }

  // 歩の価値を１００点に固定する
  params->material[kPawn] = 100.0f;

  LearningStats stats;
  stats.penalty = kL1Penalty * l1_penalty;

  return stats;
}

/**
 * パラメータの中身を、画面に表示します.
 */
void PrintParams(const std::unique_ptr<ExtendedParams>& params, int index) {
  // 駒の価値
  for (PieceType pt : Piece::all_piece_types()) {
    ExtendedParams::Accumulator accumulator;
    std::printf("pt=%s %.0f\n",
                Piece(kBlack, pt).ToSfen().c_str(),
                params->material[pt][index]);
  }
  std::printf("\n");

  // 持ち駒
  for (Piece piece : Piece::all_pieces()) {
    if (!piece.is_droppable()) continue;
    std::printf("Hand=%s", piece.ToSfen().c_str());
    for (int n = 1; n <= GetMaxNumber(piece.type()); ++n) {
      RelativePsq rp = RelativePsq::OfHand(n);
      std::printf(" %6.0f", params->relative_kp[piece][rp][index]);
    }
    std::printf("\n");
  }

  // 盤上の駒
  for (Piece piece : Piece::all_pieces()) {
    if (piece.is(kKing)) continue;
    std::printf("Piece=%s\n", piece.ToSfen().c_str());
    for (int y = 0; y < 17; ++y) {
      for (int x = 0; x < 9; ++x) {
        RelativePsq rp(x + 9 * y);
        std::printf(" %6.0f", params->relative_kp[piece][rp][index]);
      }
      std::printf("\n");
    }
    std::printf("\n");
  }

  // 玉の安全度
#define PRINT_ADJACENT8(h, t) \
  std::printf(" %6.0f %6.0f %6.0f\n", h[kDirNW]t[index], h[kDirN]t[index], h[kDirNE]t[index]); \
  std::printf(" %6.0f %6.0f %6.0f\n", h[kDirW ]t[index],             0.0f, h[kDirE ]t[index]); \
  std::printf(" %6.0f %6.0f %6.0f\n", h[kDirSW]t[index], h[kDirS]t[index], h[kDirSE]t[index]); \
  std::printf("\n");

  std::printf("Weak-Points=not_weak\n");
  PRINT_ADJACENT8(params->weak_points_of_king[false], [false][true])
  PRINT_ADJACENT8(params->weak_points_of_king[true ], [false][true])
  std::printf("Weak-Points=weak\n");
  PRINT_ADJACENT8(params->weak_points_of_king[false], [true][false])
  PRINT_ADJACENT8(params->weak_points_of_king[true ], [true][false])

#undef PRINT_ADJACENT8

  // 飛車の利き
  std::printf("Rook-Control-Defense\n");
  for (int y = 0; y < 17; ++y) {
    for (int x = 0; x < 9; ++x) {
      RelativeSquare rs(x + 9 * y);
      std::printf(" %6.0f", params->rook_control[kBlack][rs][index]);
    }
    std::printf("\n");
  }
  std::printf("Rook-Control-Attack\n");
  for (int y = 0; y < 17; ++y) {
    for (int x = 0; x < 9; ++x) {
      RelativeSquare rs(x + 9 * y);
      std::printf(" %6.0f", params->rook_control[kWhite][rs][index]);
    }
    std::printf("\n");
  }

  // 角の利き
  std::printf("Bishop-Control-Defense\n");
  for (int y = 0; y < 17; ++y) {
    for (int x = 0; x < 9; ++x) {
      RelativeSquare rs(x + 9 * y);
      std::printf(" %6.0f", params->bishop_control[kBlack][rs][index]);
    }
    std::printf("\n");
  }
  std::printf("Bishop-Control-Attack\n");
  for (int y = 0; y < 17; ++y) {
    for (int x = 0; x < 9; ++x) {
      RelativeSquare rs(x + 9 * y);
      std::printf(" %6.0f", params->bishop_control[kWhite][rs][index]);
    }
    std::printf("\n");
  }

  // 各マスの利き
  std::printf("Each-Square-Control\n");
  for (int own_controls = 0; own_controls <= 3; ++own_controls)
    for (int opp_controls = 0; opp_controls <= 3; ++opp_controls) {
      std::printf("own_controls=%d/opp_controls=%d\n", own_controls, opp_controls);
      for (int y = 0; y < 17; ++y) {
        for (int x = 0; x < 9; ++x) {
          RelativeSquare rs(x + 9 * y);
          std::printf(" %6.0f", params->controls_relative[rs][own_controls][opp_controls][index]);
        }
        std::printf("\n");
      }
    }

  // 飛車・角・香車が利きをつけている駒
  std::printf("Rook-Threatened-Piece\n");
  for (Piece piece : Piece::all_pieces()) {
    std::printf("%s: %6.0f\n", piece.ToSfen().c_str(),
                params->rook_threatened_piece[piece][index]);
  }
  std::printf("Bishop-Threatened-Piece\n");
  for (Piece piece : Piece::all_pieces()) {
    std::printf("%s: %6.0f\n", piece.ToSfen().c_str(),
                params->bishop_threatened_piece[piece][index]);
  }
  std::printf("Lance-Threatened-Piece\n");
  for (Piece piece : Piece::all_pieces()) {
    std::printf("%s: %6.0f\n", piece.ToSfen().c_str(),
                params->lance_threatened_piece[piece][index]);
  }

  // 駒取りの脅威の評価 [駒の種類][味方の利きの数][敵の利き数]
  std::printf("CaptureThreat\n");
  for (PieceType pt : Piece::all_piece_types()) {
    for (int own_controls = 0; own_controls < 4; ++own_controls) {
      std::printf("%s: %6.0f %6.0f %6.0f %6.0f\n",
                  Piece(kBlack, pt).ToSfen().c_str(),
                  params->capture_threat[pt][own_controls][0][index],
                  params->capture_threat[pt][own_controls][1][index],
                  params->capture_threat[pt][own_controls][2][index],
                  params->capture_threat[pt][own_controls][3][index]);
    }
  }

  // 手番
  std::printf("Tempo\n");
  std::printf(" %6.0f %6.0f %6.0f\n",
              params->tempo[0], params->tempo[1], params->tempo[2]);
}

/*
 * 現在学習中のパラメータを、探索用のパラメータ（g_eval_params）にコピーします.
 */
void CopyParams(const std::unique_ptr<ExtendedParams>& params) {

  auto to_packed_score = [](PackedWeight& x) -> PackedScore {
    PackedWeight score = x * static_cast<float>(kFvScale);
    return PackedScore(static_cast<int32_t>(score[0]),
                       static_cast<int32_t>(score[1]),
                       static_cast<int32_t>(score[2]),
                       static_cast<int32_t>(score[3]));
  };

  // 1. 駒の価値をコピーする
  for (PieceType pt : Piece::all_piece_types()) {
    // 序盤〜中盤〜終盤を通した、平均的な駒の価値を求める
    float value = 0.0f;
    value += 0.25f * params->material[pt][0]; // 序盤
    value += 0.50f * params->material[pt][1]; // 中盤
    value += 0.25f * params->material[pt][2]; // 終盤
    int int_value = static_cast<int>(value);
    g_eval_params->material[pt] = static_cast<Score>(int_value);
  }
  Material::UpdateTables();

  // 2. KPをコピー
#pragma omp parallel for schedule(static)
  for (int i = Square::min(); i <= Square::max(); ++i) {
    Square king_sq(i);
    for (PsqIndex psq : PsqIndex::all_indices()) {
      ExtendedParams::Accumulator accumulator;
      auto sum = params->EachKP<kBlack>(king_sq, psq, accumulator).sum();
      PackedScore packed_score = to_packed_score(sum);
      packed_score[3] = Progress::weights[king_sq][psq]; // 進行度を保存する
      g_eval_params->king_piece[king_sq][psq] = packed_score;
    }
  }

  // 3. PPをコピー
#pragma omp parallel for schedule(static)
  for (int i = PsqIndex::min(); i <= PsqIndex::max(); ++i) {
    PsqIndex psq1(i);
    for (PsqIndex psq2 : PsqIndex::all_indices()) {
      ExtendedParams::Accumulator accumulator;
      auto sum = params->EachPP(psq1, psq2, accumulator).sum();
      g_eval_params->two_pieces[psq1][psq2] = to_packed_score(sum);
    }
  }

  const auto all_pieces_plus_no_piece = Piece::all_pieces().set(kNoPiece);

  // 4. 各マスの利き評価をコピー
#pragma omp parallel for schedule(static)
  for (int i = PsqControlIndex::min(); i <= PsqControlIndex::max(); ++i) {
    PsqControlIndex index(i);
    if (index.IsOk()) {
      for (Square ksq : Square::all_squares()) {
        ExtendedParams::Accumulator accumulator;
        auto sum_b = params->EachControl<kBlack>(ksq, index, accumulator).sum();
        auto sum_w = params->EachControl<kWhite>(ksq, index, accumulator).sum();
        g_eval_params->controls[kBlack][ksq][index] = to_packed_score(sum_b);
        g_eval_params->controls[kWhite][ksq][index] = to_packed_score(sum_w);
      }
    }
  }

  // 5. 玉の安全度をコピー
#pragma omp parallel for schedule(static)
  for (unsigned h = HandSet::min(); h <= HandSet::max(); ++h)
    for (int d = 0; d < 8; ++d)
      for (Piece piece : all_pieces_plus_no_piece)
        for (int attacks = 0; attacks < 4; ++attacks)
          for (int defenses = 0; defenses < 4; ++defenses) {
            ExtendedParams::Accumulator accumulator;
            HandSet hand_set(h);
            Direction dir = static_cast<Direction>(d);
            auto sum = params->EachKingSafety<kBlack>(hand_set, dir, piece,
                                                      attacks, defenses,
                                                      accumulator).sum();
            PackedScore ps = to_packed_score(sum);
            g_eval_params->king_safety[hand_set][dir][piece][attacks][defenses] = ps;
          }

  // 6. 飛車・角・香車の利きをコピー
#pragma omp parallel for schedule(static)
  for (int s = Square::min(); s <= Square::max(); ++s) {
    Square i(s);
    for (Color c : {kBlack, kWhite})
      for (Square j : Square::all_squares())
        for (Square k : Square::all_squares()) {
          ExtendedParams::Accumulator accumulator;
          auto sum_r = params->EachSliderControl<kBlack, kRook>(c, i, j, k, accumulator).sum();
          auto sum_b = params->EachSliderControl<kBlack, kBishop>(c, i, j, k, accumulator).sum();
          auto sum_l = params->EachSliderControl<kBlack, kLance>(c, i, j, k, accumulator).sum();
          g_eval_params->rook_control[c][i][j][k] = to_packed_score(sum_r);
          g_eval_params->bishop_control[c][i][j][k] = to_packed_score(sum_b);
          g_eval_params->lance_control[c][i][j][k] = to_packed_score(sum_l);
        }

    for (Square j : Square::all_squares())
      for (Piece p : Piece::all_pieces()) {
         ExtendedParams::Accumulator accumulator;
         auto sum_r = params->EachThreat<kBlack, kRook>(i, j, p, accumulator).sum();
         auto sum_b = params->EachThreat<kBlack, kBishop>(i, j, p, accumulator).sum();
         auto sum_l = params->EachThreat<kBlack, kLance>(i, j, p, accumulator).sum();
         g_eval_params->rook_threat[i][j][p] = to_packed_score(sum_r);
         g_eval_params->bishop_threat[i][j][p] = to_packed_score(sum_b);
         g_eval_params->lance_threat[i][j][p] = to_packed_score(sum_l);
      }
  }

  // 7. 手番をコピー
  g_eval_params->tempo = to_packed_score(params->tempo);
}

/*
 * 学習したパラメータを用いて、棋譜（テストデータ）との一致率を計算する。
 */
float ComputeAccuracy(const std::vector<Game>& games) {
  // 設定変数
  const int transposition_table_size = 1;
  const Score alpha = -kScoreKnownWin, beta = kScoreKnownWin;

  ProgressTimer timer(games.size());
  int sum_accuracy = 0, num_positions = 0;

#pragma omp parallel for schedule(dynamic)
  for (size_t game_id = 0; game_id < games.size(); ++game_id) {
    const Game& game = games.at(game_id);
    Node node(Position::CreateStartPosition());
    SharedData shared_data;
    shared_data.hash_table.SetSize(transposition_table_size);
    Search search(shared_data);
    for (size_t ply = 0; ply < game.moves.size(); ++ply) {
      const Move teacher_move = game.moves[ply];
      if (!node.MoveIsLegal(teacher_move)) {
        break;
      }
      // 探索を行う。学習部との整合性をとるため、深さは＋１手されている。
      search.PrepareForNextSearch();
      search.AlphaBetaSearch(node, alpha, beta, (kMaxSearchDepth + 1) * kOnePly);
      if (search.pv_table().size() == 0) {
        break;
      }
      Move best_move = search.pv_table()[0];
#pragma omp atomic
      sum_accuracy += (best_move == teacher_move);
#pragma omp atomic
      num_positions += 1;
      node.MakeMove(teacher_move);
      node.Evaluate(); // 評価関数の差分計算を行うために必要
    }

    timer.IncrementCounter();
    timer.PrintProgress("Accuracy=%f%% (%d/%d)",
                        static_cast<float>(sum_accuracy) / num_positions,
                        sum_accuracy, num_positions);
  }

  return static_cast<float>(sum_accuracy) / num_positions;
}

} // namespace

void Learning::LearnEvaluationParameters(const bool use_rootstrap,
                                         const bool use_logistic_regression) {
  // スレッド数の設定
  const int num_threads = std::max(1U, std::thread::hardware_concurrency());
  omp_set_num_threads(num_threads);
  std::printf("Set num_threads = %d\n", num_threads);

  // データベースから棋譜を読み込む（学習用と、交差検定用の2つがある）
  std::printf("Extract the games from the database.\n");
  const std::vector<Game> games = ExtractGamesFromDatabase(kNumGames, 0);
  const std::vector<Game> test_set = ExtractGamesFromDatabase(kNumTestSet, kNumGames);

  // 全局面にIDを割り振る（あとで局面のシャッフルを行うため）
  struct PositionId {
    PositionId(int g, int p) : game_id(g), ply(p) {}
    int game_id;
    int ply;
  };
  std::vector<PositionId> position_ids;
  for (size_t i = 0; i < games.size(); ++i) {
    for (size_t j = 0; j < games.at(i).moves.size(); ++j) {
      position_ids.emplace_back(i, j);
    }
  }

  // RootStrapに用いる教師局面を読み込む
  std::vector<TeacherPosition> rootstrap_positions;
  if (use_rootstrap) {
    std::printf("Read the teacher positions from the file.\n");
    std::FILE* teacher_file = std::fopen("teacher_positions.bin", "rb");
    for (TeacherPosition buf; std::fread(&buf, sizeof(buf), 1, teacher_file); ) {
      rootstrap_positions.push_back(buf);
    }
    std::fclose(teacher_file);
  }

  // ロジスティック回帰に用いる教師局面を読み込む
  std::vector<TeacherPosition> logistic_regression_positions;
  if (use_logistic_regression) {
    std::printf("Read the teacher games from the file.\n");
    std::FILE* teacher_file = std::fopen("teacher_games.bin", "rb");
    for (TeacherPosition buf; std::fread(&buf, sizeof(buf), 1, teacher_file); ) {
      logistic_regression_positions.push_back(buf);
    }
    std::fclose(teacher_file);
  }

  // 乱数発生器（メルセンヌ・ツイスタ）の準備
  std::random_device rd;
  std::vector<std::mt19937> mersenne_twisters;
  for (int i = 0; i < num_threads; ++i) {
    mersenne_twisters.emplace_back(rd());
  }

  // 勾配やパラメータを初期化する
  std::unique_ptr<ExtendedParams> convoluted_gradient(new ExtendedParams);
  std::unique_ptr<ExtendedParams> accumulated_gradient(new ExtendedParams);
  std::unique_ptr<ExtendedParams> current_params(new ExtendedParams);
  std::unique_ptr<ExtendedParams> accumulated_params(new ExtendedParams);
  std::unique_ptr<Gradient> gradient(new Gradient);
  std::vector<Gradient> thread_local_gradient(num_threads);
  std::vector<SharedData> shared_data(num_threads);
  g_eval_params->Clear();
  accumulated_gradient->Clear();
  current_params->Clear();
  accumulated_params->Clear();
  ResetMaterialValues(current_params.get());
  CopyParams(current_params);
  for (auto& s : shared_data) {
    s.hash_table.SetSize(64);
  }

  // ログファイルのクリア
  {
    std::FILE* fp = std::fopen("learning_log.txt", "w");
    if (fp == nullptr) {
      std::printf("Failed to open learning_log.txt.\n");
    }
    std::fclose(fp);
  }
  {
    std::FILE* fp = std::fopen("learning_material.txt", "w");
    if (fp == nullptr) {
      std::printf("Failed to open learning_material.txt.\n");
    }
    std::fclose(fp);
  }

  // 学習のイテレーションを開始する
  for (int iteration = 1; iteration <= kNumIteration; ++iteration) {
    if (kVerboseMessage) {
      std::printf("Start new iteration: %d\n", iteration);
    }

    // 学習に使う局面をシャッフルする（復元抽出）
    for (int i = 0; i < kBatchSize; ++i) {
      std::uniform_int_distribution<size_t> dis(i, position_ids.size() - 1);
      auto begin = position_ids.begin();
      std::iter_swap(begin + i, begin + dis(mersenne_twisters.front()));
    }

    // 勾配を計算する準備をする
#pragma omp parallel for schedule(static, 1)
    for (int i = 0; i < num_threads; ++i) {
      thread_local_gradient.at(i).Clear();
      shared_data.at(i).Clear();
    }

    // 勾配を計算する
    if (kVerboseMessage) {
      std::printf("Compute Gradient...\n");
    }
    const Position kStartPosition = Position::CreateStartPosition();
    LearningStats stats;
#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < kBatchSize; ++i) {
      const Game& game = games.at(position_ids.at(i).game_id);
      const int ply = position_ids.at(i).ply;

      // 学習局面に移動する
      Position pos = kStartPosition;
      for (int j = 0; j < ply; ++j) {
        pos.MakeMove(game.moves.at(j));
      }

      // 勾配を計算する
      int thread_id = omp_get_thread_num();
      Move teacher_move = game.moves.at(ply);
      float progress = float(ply) / float(game.moves.size());
      Color winner = game.result == Game::kBlackWin ? kBlack : kWhite;
      auto temp = ComputeGradient(pos, shared_data.at(thread_id),
                                  teacher_move, progress, winner,
                                  mersenne_twisters.at(thread_id),
                                  &thread_local_gradient.at(thread_id));
#pragma omp critical
      stats += temp;
    }

    // RootStrapの勾配を計算する
    if (use_rootstrap) {
      // 予め作成しておいた教師局面の中から、RootStrapの勾配計算に用いる局面をランダムに選ぶ
      for (int i = 0; i < kRootStrapBatchSize; ++i) {
        std::uniform_int_distribution<size_t> dis(i, rootstrap_positions.size() - 1);
        auto begin = rootstrap_positions.begin();
        std::iter_swap(begin + i, begin + dis(mersenne_twisters.front()));
      }

      // 予め作成しておいた教師局面を使って、RootStrapの勾配を計算する
#pragma omp parallel for schedule(dynamic)
      for (int i = 0; i < kRootStrapBatchSize; ++i) {
        const TeacherPosition& teacher_pos = rootstrap_positions.at(i);
        LearningStats temp;
        int thread_id = omp_get_thread_num();
        ComputeGradientOfRootStrapLoss(teacher_pos, shared_data.at(thread_id),
                                       &thread_local_gradient.at(thread_id),
                                       &temp);
#pragma omp critical
        stats += temp;
      }
    }

    // ロジスティック回帰の勾配を計算する
    if (use_logistic_regression) {
      // 予め作成しておいた自己対戦棋譜中の局面から、ロジスティック回帰の勾配計算に用いる局面をランダムに選ぶ
      for (int i = 0; i < kLogisticRegressionBatchSize; ++i) {
        std::uniform_int_distribution<size_t> dis(i, logistic_regression_positions.size() - 1);
        auto begin = logistic_regression_positions.begin();
        std::iter_swap(begin + i, begin + dis(mersenne_twisters.front()));
      }

      // 予め作成しておいた自己対戦棋譜中の局面を使って、ロジスティック回帰の勾配を計算する
#pragma omp parallel for schedule(dynamic)
      for (int i = 0; i < kLogisticRegressionBatchSize; ++i) {
        const TeacherPosition& teacher_pos = logistic_regression_positions.at(i);
        LearningStats temp;
        int thread_id = omp_get_thread_num();
        ComputeGradientOfLogisticRegressionLoss(teacher_pos,
                                                &thread_local_gradient.at(thread_id),
                                                &temp);
#pragma omp critical
        stats += temp;
      }
    }

    // 各スレッドの勾配を集約する
    gradient->Clear();
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < gradient->size(); ++i) {
      for (int j = 0; j < num_threads; ++j) {
        (*gradient)[i] += thread_local_gradient[j][i];
      }
    }

    // 勾配ベクトルについて、いわゆる次元下げを適用する
    convoluted_gradient->Clear();
    ConvoluteGradient(gradient, convoluted_gradient);

    // 勾配を利用して、パラメータを更新する
    if (kVerboseMessage) {
      std::printf("Update the evaluation parameters...\n");
    }
    stats += UpdateParams(convoluted_gradient, accumulated_gradient, current_params);
    CopyParams(current_params);

    // 後で平均化パラメータを求めるために、現在のパラメータを足し込んでおく
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < accumulated_params->size(); ++i) {
      (*accumulated_params)[i] *= kAveragedSgdDecay;
      (*accumulated_params)[i] += (*current_params)[i];
    }

    if (iteration % 100 == 0) {
      // 平均化パラメータを求める
      // ここでは、より直近のデータを重視するため、「指数移動平均」を使っている。
      // （参考文献）
      //     Wikipedia: 移動平均, https://ja.wikipedia.org/wiki/移動平均.
      std::unique_ptr<ExtendedParams> average(new ExtendedParams);
      *average = *accumulated_params;
      float denominator = (1.0f - std::pow(kAveragedSgdDecay, iteration)) / (1.0f - kAveragedSgdDecay);
      PackedWeight reciprocal(1.0f / denominator);
      for (size_t i = 0; i < average->size(); ++i) {
        (*average)[i] *= reciprocal;
      }

      // 平均化パラメータの表示
      PrintParams(average, 0);
      PrintParams(average, 1);
      PrintParams(average, 2);

      // ファイル保存＆一致率計算には、平均化パラメータを使用
      CopyParams(average);

      // 平均化パラメータをファイルに保存する
      std::FILE* fp_params = std::fopen("params.bin", "wb");
      if (fp_params == nullptr) {
        std::printf("Failed to open params.bin.\n");
        break;
      }
      std::fwrite(g_eval_params.get(), sizeof(EvalParameters), 1, fp_params);
      std::fclose(fp_params);
      std::printf("Wrote parameters to params.bin\n");

      // 交差検定を行い、棋譜の手との一致率を計算する
       std::printf("Perform the cross validation...\n");
      float accuracy = ComputeAccuracy(test_set);

      // ファイル保存＆一致率計算後は、元のパラメータに戻す
      CopyParams(current_params);

      // 学習時の統計データをログファイルに出力する
      std::FILE* fp_log = std::fopen("learning_log.txt", "a");
      if (fp_log == nullptr) {
        std::printf("Failed to open learning_log.txt.\n");
        break;
      }
      std::fprintf(fp_log,
                   "%d loss=%.0f penalty=%.1f wr_l=%.1f wr_e=%f o_l=%.1f o_e=%.1f o_s=%.0f accuracy=%f prediction=%f pos=%d moves=%d samples=%d nodes=%d\n",
                   iteration,
                   stats.loss,
                   stats.penalty,
                   stats.win_rate_loss,
                   std::sqrt(stats.win_rate_error / stats.win_rate_samples),
                   stats.oscillation_loss,
                   std::sqrt(stats.oscillation_error / stats.oscillation_samples),
                   stats.oscillation_samples,
                   accuracy,
                   (float)stats.num_right_answers / stats.num_positions,
                   stats.num_positions,
                   stats.num_moves,
                   stats.num_samples,
                   stats.num_nodes);
      std::fclose(fp_log);
    }

    // 学習途中の駒割りの値をファイルに出力する
    {
      std::FILE* fp = std::fopen("learning_material.txt", "a");
      if (fp == nullptr) {
        std::printf("Failed to open learning_material.txt.\n");
        break;
      }
      std::fprintf(fp, "%d", iteration);
      for (PieceType pt : Piece::all_piece_types()) {
        std::fprintf(fp, " %d", static_cast<int>(Material::value(pt)));
      }
      std::fprintf(fp, "\n");
      std::fclose(fp);
    }

    // 学習中の統計データを画面に表示する
    std::printf("%d loss=%.0f L1=%.0f rs_l=%.0f rs_e=%f lr_l=%.0f lr_e=%f wr_l=%.0f wr_e=%f o_l=%.0f o_e=%.1f prediction=%f pos=%d moves=%d samples=%d nodes=%d\n",
                iteration,
                stats.loss,
                stats.penalty,
                stats.rootstrap_loss,
                std::sqrt((stats.rootstrap_error) / stats.rootstrap_samples),
                stats.logistic_regression_loss,
                std::sqrt((stats.logistic_regression_error) / stats.logistic_regression_samples),
                stats.win_rate_loss,
                std::sqrt(stats.win_rate_error / stats.win_rate_samples),
                stats.oscillation_loss,
                std::sqrt(stats.oscillation_error / stats.oscillation_samples),
                (float)stats.num_right_answers / stats.num_positions,
                stats.num_positions,
                stats.num_moves,
                stats.num_samples,
                stats.num_nodes);
  }

  std::printf("Congratulations! Learning is successfully finished!\n");
}

#endif // !defined(MINIMUM)
