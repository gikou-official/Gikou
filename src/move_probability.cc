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

#include "move_probability.h"

#include <fstream>
#include <thread>
#include <valarray>
#include <omp.h>
#include "common/math.h"
#include "common/pack.h"
#include "common/progress_timer.h"
#include "gamedb.h"
#include "movegen.h"
#include "move_feature.h"
#include "position.h"
#include "progress.h"
#include "search.h"

namespace {

// 学習の基本設定
constexpr int kNumIterations  = 512;   // パラメタ更新の反復回数
constexpr int kNumTeacherData = 5000;  // 学習に利用する対局数
constexpr int kNumTestData    = 1000;  // 交差検定で用いるサンプル数

// 損失関数のペナルティに関する設定
constexpr double kL1Penalty = 0.0;    // L1正則化係数
constexpr double kL2Penalty = 5.0;    // L2正則化係数

// History値収集に用いる探索の深さ
constexpr Depth kSearchDepth = 7 * kOnePly;

// AdaDeltaの設定
constexpr double kInitialStep = 0.05; // 重みベクトルの最初の更新幅
constexpr double kDecay = 0.95;       // AdaDelta原論文のρ
constexpr double kMomentum = 0.95;    // モーメンタムの強さ
constexpr double kEpsilon = 1e-6;     // AdaDelta原論文のε

struct PositionSample {
  double progress;
  std::vector<MoveFeatureList> features;
};

struct LearningStats {
  double logistic_loss = 0.0;
  double lasso_loss    = 0.0;
  double tikhonov_loss = 0.0;
  double prediction    = 0.0;
  double accuracy      = 0.0;
  int num_moves_learned     = 0;
  int num_teacher_positions = 0;
  int num_moves_tested      = 0;
  int num_test_positions    = 0;
};

typedef Pack<double, 2> PackedWeight;
typedef std::valarray<PackedWeight> Weights;
Weights g_weights;

inline double HorizontalAdd(PackedWeight weight) {
  return weight[0] + weight[1];
}

inline PackedWeight GetProgressCoefficient(double progress) {
  double opening  = 1.0 - progress;
  double end_game = progress;
  return PackedWeight(opening, end_game);
}

std::valarray<double> ComputeMoveProbabilities(const PositionSample& sample) {
  // 0. ソフトマックス関数の準備
  auto softmax = [](const std::valarray<double>& x) -> std::valarray<double> {
    std::valarray<double> temp = std::exp(x - x.max());
    return temp / temp.sum();
  };

  // 1. 内積を求める
  PackedWeight coefficient = GetProgressCoefficient(sample.progress);
  std::valarray<double> move_scores(sample.features.size());
  for (size_t i = 0; i < sample.features.size(); ++i) {
    PackedWeight sum(0.0);
    // 各特徴の重みを加算する
    for (MoveFeatureIndex feature_index : sample.features.at(i)) {
      sum += g_weights[feature_index];
    }
    // history値の重みを加算する
    sum += g_weights[kHistoryFeatureIndex] * sample.features.at(i).history;
    // 進行度に応じて内分を取る
    PackedWeight score = sum * coefficient;
    move_scores[i] = HorizontalAdd(score);
  }

  // 2. ソフトマックス関数を適用して、それぞれの指し手の確率を求める
  return softmax(move_scores);
}

#if !defined(MINIMUM)

void ExtractGamesFromDatabase(std::vector<Game>* teacher_data,
                              std::vector<Game>* test_data) {
  assert(teacher_data != nullptr);
  assert(test_data != nullptr);

  // 1. データベースファイルを開く
  std::ifstream game_db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(game_db_file);

  // 2. テストデータの読み込み
  // （まずテストデータから読み込むことで、教師データの数に関わらずテストデータを統一することができる）
  for (int i = 0; i < kNumTestData; ++i) {
    Game game;
    if (   game_db.ReadOneGame(&game)
        && game.moves.size() < 256) {
      test_data->push_back(game);
    }
  }

  // 3. 教師データの読み込み
  for (int i = 0; i < kNumTeacherData; ++i) {
    Game game;
    if (   game_db.ReadOneGame(&game)
        && game.moves.size() < 256) {
      teacher_data->push_back(game);
    }
  }
}

void ComputeMoveFeatures(const std::vector<Game>& games,
                         std::vector<PositionSample>* position_samples) {
  assert(position_samples != nullptr);

  const Position kStartPosition = Position::CreateStartPosition();

  ProgressTimer timer(games.size());

#pragma omp parallel for schedule(dynamic)
  for (size_t game_id = 0; game_id < games.size(); ++game_id) {
    const Game& game = games.at(game_id);
    SharedData shared_data;
    shared_data.hash_table.SetSize(16);
    Node node(kStartPosition);
    Search search(shared_data);
    timer.IncrementCounter();
    timer.PrintProgress("");

    for (int ply = 0, length = game.moves.size(); ply < length; ++ply) {
      const Move teacher_move = game.moves.at(ply);

      // 棋譜の手が、二歩などの非合法手の場合は、そこで反則負けとなるため、これ以降は学習しない
      if (!node.MoveIsLegal(teacher_move)) {
        break;
      }

      // 飛角歩の不成などは、学習の対象としない（指し手生成から除外されているため）
      if (teacher_move.IsInferior()) {
        node.MakeMove(teacher_move);
        node.Evaluate(); // 評価関数の差分計算を行うために必要
        continue;
      }

      // 合法手を生成する
      SimpleMoveList<kAllMoves, true> legal_moves(node);

      // 合法手数が１手以下ならば、学習の必要はない
      if (legal_moves.size() == 0) {
        break;
      } else if (legal_moves.size() == 1) {
        node.MakeMove(teacher_move);
        node.Evaluate(); // 評価関数の差分計算を行うために必要
        continue;
      }

      // 棋譜の手を合法手リストの先頭に持ってくる
      auto iter = std::find_if(legal_moves.begin(), legal_moves.end(),
                               [teacher_move](ExtMove em) {
        return em.move == teacher_move;
      });
      std::iter_swap(legal_moves.begin(), iter);
      assert(legal_moves[0].move == teacher_move);

      // History値と、評価値のgainを求めるために探索を行う
      search.PrepareForNextSearch();
      search.set_learning_mode(true);
      search.AlphaBetaSearch(node, -kScoreInfinite, kScoreInfinite, kSearchDepth);

      // すべての合法手について、指し手の特徴を求める
      PositionSample sample;
      PositionInfo pos_info(node, search.history(), search.gains());
      sample.progress = Progress::EstimateProgress(node);
      for (ExtMove em : legal_moves) {
        sample.features.push_back(ExtractMoveFeatures(em.move, node, pos_info));
        sample.features.back().shrink_to_fit();
      }

      // 指し手の特徴を保存する（position_samplesは共有変数なので、排他制御を行う）
#pragma omp critical
      {
        position_samples->push_back(sample);
        position_samples->back().features.shrink_to_fit();
      }

      // 棋譜の手にそって進める
      node.MakeMove(game.moves.at(ply));
      node.Evaluate(); // 評価関数の差分計算を行うために必要
    }
  }
}

void ComputeGradients(const std::vector<PositionSample>& position_samples,
                      std::vector<Weights>& thread_local_gradients,
                      Weights* const gradients,
                      LearningStats* const stats) {
  assert(gradients != nullptr);
  assert(stats != nullptr);

  double loss = 0.0;
  double prediction = 0.0;
  int num_moves = 0;

  // 1. 各局面の微分値を一つ一つ計算する（複数スレッドで分散処理を行う）
#pragma omp parallel for reduction(+:loss, prediction, num_moves) schedule(dynamic)
  for (size_t pos_id = 0; pos_id < position_samples.size(); ++pos_id) {
    // 勾配をアップデートするための関数を準備する
    auto update = [&](PackedWeight delta, const MoveFeatureList& feature_list) {
      Weights& g = thread_local_gradients.at(omp_get_thread_num());
      // 各特徴の勾配を更新する
      for (auto feature_index : feature_list) {
        g[feature_index] += delta;
      }
      // historyの重みの勾配を更新する
      g[kHistoryFeatureIndex] += delta * feature_list.history;
    };

    // 各指し手の確率を求める
    const PositionSample& sample = position_samples.at(pos_id);
    std::valarray<double> probabilities = ComputeMoveProbabilities(sample);

    // 勾配のアップデートを行う
    PackedWeight coefficient = GetProgressCoefficient(sample.progress);
    for (size_t i = 0; i < sample.features.size(); ++i) {
      double delta = (i == 0) ? (probabilities[0] - 1.0) : probabilities[i];
      update(delta * coefficient, sample.features.at(i));
    }

    // 統計情報の計算
    num_moves += sample.features.size();
    loss += -std::log(probabilities[0]);
    prediction += (probabilities[0] == probabilities.max());
  }

  // 2. 全スレッドの勾配を集計する
  *gradients = PackedWeight(0.0);
#pragma omp parallel for schedule(static)
  for (size_t i = 0; i < gradients->size(); ++i) {
    for (const Weights& g : thread_local_gradients) {
      (*gradients)[i] += g[i];
    }
  }

  // 統計情報の出力
  stats->logistic_loss = loss;
  stats->num_moves_learned = num_moves;
  stats->num_teacher_positions = position_samples.size();
  stats->prediction = prediction / static_cast<double>(position_samples.size());
}

void UpdateWeights(const Weights& gradients, Weights& accumulated_gradients,
                   Weights& accumulated_deltas, Weights& momentum,
                   LearningStats* const stats) {
  assert(stats != nullptr);

  double lasso = 0.0;
  double tikhonov = 0.0;

#pragma omp parallel for reduction(+:lasso, tikhonov) schedule(static)
  for (size_t i = 0; i < gradients.size(); ++i) {
    PackedWeight& w = g_weights[i];
    PackedWeight& r = accumulated_gradients[i];
    PackedWeight& s = accumulated_deltas[i];
    PackedWeight& v = momentum[i];
    PackedWeight gradient = gradients[i];

    // 損失を計算する
    lasso    += kL1Penalty * HorizontalAdd(w.apply(std::abs));
    tikhonov += kL2Penalty * HorizontalAdd(w * w);

    // 勾配方向に移動する（AdaDelta + Momentum）
    r = kDecay * r + (1.0 - kDecay) * (gradient * gradient);
    PackedWeight eta = s.apply(std::sqrt) / (r + kEpsilon).apply(std::sqrt);
    v = kMomentum * v - (1.0 - kMomentum) * eta * gradient;
    s = kDecay * s + (1.0 - kDecay) * (v * v);
    w = w + v;

    // ペナルティをかける（FOBOS）
    // （参考文献）
    //   - John Duchi, Yoram Singer: Efficient Learning with Forward-Backward Splitting,
    //     http://web.stanford.edu/~jduchi/projects/DuchiSi09c_slides.pdf,
    //     pp.12-13, 2009.
    //   - Zachary C. Lipton, Charles Elkan: Efficient Elastic Net Regularization for Sparse Linear Models,
    //     http://zacklipton.com/media/papers/lazy-updates-elastic-net-lipton2015_1.pdf,
    //     p.9, 2015.
    auto ramp = [](double x) -> double {
      return std::max(x, 0.0);
    };
    PackedWeight lambda1 = eta * kL1Penalty;
    PackedWeight lambda2 = eta * kL2Penalty;
    w = w.apply(math::sign) * ((w.apply(std::abs) - lambda1) / (1.0 + lambda2)).apply(ramp);
  }

  // 統計情報の記録
  stats->lasso_loss = lasso;
  stats->tikhonov_loss = tikhonov;
}

void ComputeAccuracy(const std::vector<PositionSample>& position_samples,
                     LearningStats* const stats) {
  assert(stats != nullptr);

  int num_moves = 0;
  double accuracy = 0.0;

#pragma omp parallel for reduction(+:num_moves, accuracy) schedule(dynamic)
  for (size_t pos_id = 0; pos_id < position_samples.size(); ++pos_id) {
    const PositionSample& sample = position_samples.at(pos_id);
    std::valarray<double> probabilities = ComputeMoveProbabilities(sample);
    // 棋譜の手が、最大の確率を与えられているかを調べる
    if (probabilities[0] == probabilities.max()) {
      accuracy += 1.0;
    }
  }

  stats->accuracy = accuracy / static_cast<double>(position_samples.size());
  stats->num_moves_tested = num_moves;
  stats->num_test_positions = position_samples.size();
}

void PrintMoveProbabilities(Position pos) {
  // 1. 初期局面の合法手を生成する
  SimpleMoveList<kAllMoves, true> legal_moves(pos);

  // 2. 各指し手の確率を計算する
  HistoryStats history;
  GainsStats gains;
  history.Clear();
  gains.Clear();
  PositionSample sample;
  PositionInfo pos_info(pos, history, gains);
  sample.progress = Progress::EstimateProgress(pos);
  for (ExtMove em : legal_moves) {
    sample.features.push_back(ExtractMoveFeatures(em.move, pos, pos_info));
  }
  std::valarray<double> probabilities = ComputeMoveProbabilities(sample);

  // 3. 確率の高い順（降順）で、指し手をソートする
  for (size_t i = 0; i < legal_moves.size(); ++i) {
    legal_moves[i].score = static_cast<int>(probabilities[i] * (1 << 24));
  }
  std::sort(legal_moves.begin(), legal_moves.end(), [](ExtMove lhs, ExtMove rhs) {
    return lhs.score > rhs.score;
  });

  // 4. 確率の高い順に指し手をプリントする
  for (size_t i = 0; i < legal_moves.size(); ++i) {
    std::printf(" %s=%.2f",
                legal_moves[i].move.ToSfen().c_str(),
                100.0 * legal_moves[i].score / (1 << 24));
  }
  std::printf("\n");
}

#endif /* !defined(MINIMUM) */

} // namespace

#if !defined(MINIMUM)

void MoveProbability::Learn() {
  // スレッド数の設定
  const int num_threads = std::max(1U, std::thread::hardware_concurrency());
  omp_set_num_threads(num_threads);
  std::printf("Set num_threads = %d\n", num_threads);

  const Position startpos = Position::CreateStartPosition();

  // 棋譜データの準備
  std::printf("start reading games.\n");
  std::vector<Game> teacher_data; // 教師データ（学習対象とする棋譜）
  std::vector<Game> test_data;    // テストデータ（教師データとは別の棋譜。一致率確認用。）
  ExtractGamesFromDatabase(&teacher_data, &test_data);
  std::printf("finish reading games.\n");

  // 変数の準備
  Weights gradients(kNumMoveFeatures);
  Weights momentum(kNumMoveFeatures);
  Weights accumulated_gradients(kNumMoveFeatures);
  Weights accumulated_deltas(kNumMoveFeatures);
  std::vector<Weights> thread_local_gradients;
  g_weights = PackedWeight(0.0);
  momentum = PackedWeight(0.0);
  accumulated_gradients = PackedWeight(0.0);
  accumulated_deltas = PackedWeight((1.0 - kDecay) * std::pow(kInitialStep, 2.0) / std::pow(1.0 - kMomentum, 2.0));
  for (int i = 0; i < num_threads; ++i) {
    thread_local_gradients.emplace_back(kNumMoveFeatures);
  }

  // 指し手の特徴をあらかじめ求めておく
  // （評価関数の学習とは異なり、反復中にPVが変化するといったことがないため）
  std::vector<PositionSample> teacher_position_samples;
  std::vector<PositionSample> test_position_samples;
  ComputeMoveFeatures(teacher_data, &teacher_position_samples);
  ComputeMoveFeatures(test_data, &test_position_samples);

  for (int iteration = 1; iteration <= kNumIterations; ++iteration) {
    // 統計情報を保存するための変数を準備する
    LearningStats stats;

    // 勾配をリセットする
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < thread_local_gradients.size(); ++i) {
      thread_local_gradients.at(i) = PackedWeight(0.0);
    }

    // Step 1. 勾配を計算する
    ComputeGradients(teacher_position_samples, thread_local_gradients,
                     &gradients, &stats);

    // Step 2. 勾配を利用して、重みを更新する（Gradient Descent）
    UpdateWeights(gradients, accumulated_gradients, accumulated_deltas,
                  momentum, &stats);

    // Step 3. 学習用棋譜とは別の棋譜を利用して、一致率を調べる
    ComputeAccuracy(test_position_samples, &stats);

    // Step 4. 統計情報を表示する
    std::printf("%d Loss=%f L1=%f L2=%f Prediction=%f Accuracy=%f pos=%d moves=%d\n",
                iteration,
                stats.logistic_loss + stats.lasso_loss + stats.tikhonov_loss,
                stats.lasso_loss,
                stats.tikhonov_loss,
                stats.prediction,
                stats.accuracy,
                stats.num_teacher_positions,
                stats.num_moves_learned);
  }

  // 指定局面の指し手につけられた確率を、参考のために画面に表示する
  // a. 初期局面
  Position start_position = Position::CreateStartPosition();
  start_position.Print();
  PrintMoveProbabilities(start_position);
  // b. 鶴岡（2012）の参考局面（下記文献の図１）
  // 鶴岡慶雅: 激指の誕生, 『人間に勝つコンピュータ将棋の作り方』, pp.78-85, 技術評論社, 2012.
  Position tsuruoka2012 = Position::FromSfen("ln6l/1r4gk1/p1s2gnpp/1pp1p4/9/2PPPP+p2/PPSG3PP/2G1R4/LNK5L w BS3Pbsn 1");
  tsuruoka2012.MakeMove(Move(kWhiteGold, kSquare4C, kSquare4D));
  tsuruoka2012.Print(tsuruoka2012.last_move());
  PrintMoveProbabilities(tsuruoka2012);
  std::printf("鶴岡2012: ７一角（0.28）、３四歩（0.09）、５三角（0.09）、５三銀（0.02）、５七飛（0.02）\n");

  // すべてのイテレーションが終了したら、計算結果をファイルへ書き出す
  std::FILE* fout = std::fopen("probability.bin", "wb");
  for (PackedWeight& v : g_weights) {
    std::fwrite(&v, sizeof(v), 1, fout);
  }
}

#endif /* !defined(MINIMUM) */

std::unordered_map<uint32_t, float> MoveProbability::ComputeProbabilities(const Position& pos,
                                                                          const HistoryStats& history,
                                                                          const GainsStats& gains) {
  // 合法手を生成する
  SimpleMoveList<kAllMoves, true> legal_moves(pos);

  // 局面情報を収集する
  PositionSample sample;
  PositionInfo pos_info(pos, history, gains);
  sample.progress = Progress::EstimateProgress(pos);
  for (ExtMove em : legal_moves) {
    sample.features.push_back(ExtractMoveFeatures(em.move, pos, pos_info));
  }

  // 確率を計算する
  std::valarray<double> p = ComputeMoveProbabilities(sample);

  // 指し手と確率を対応付ける
  std::unordered_map<uint32_t, float> move_probabilities;
  for (size_t i = 0; i < legal_moves.size(); ++i) {
    move_probabilities.emplace(legal_moves[i].move.ToUint32(), p[i]);
  }

  return move_probabilities;
}

void MoveProbability::Init() {
  g_weights.resize(kNumMoveFeatures);

  // ファイルを開く
  std::FILE* fin = std::fopen("probability.bin", "rb");
  if (fin == nullptr) {
    std::printf("info string Failed to open probability.bin.\n");
    return;
  }

  // 重みを読み込む
  for (size_t i = 0; i < g_weights.size(); ++i) {
    PackedWeight buf;
    if (!std::fread(&buf, sizeof(buf), 1, fin)) {
      break;
    }
    g_weights[i] = buf;
  }
}
