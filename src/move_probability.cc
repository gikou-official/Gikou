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

#include "move_probability.h"

#include <fstream>
#include <thread>
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
#include "swap.h"

namespace {

// 学習の基本設定
constexpr int kNumIterations  = 256;   // パラメタ更新の反復回数
constexpr int kNumTeacherData = 30000;  // 学習に利用する対局数
constexpr int kNumTestData    = 1000;  // 交差検定で用いるサンプル数

// 損失関数のペナルティに関する設定
constexpr float kL1Penalty = 0.0f;    // L1正則化係数
constexpr float kL2Penalty = 5.0f;    // L2正則化係数

// 棋譜の手のうち、これ以降の手数のみを学習対象とする
constexpr int kLearningStartPly = 24;

// History値収集に用いる探索の深さ
constexpr Depth kSearchDepth = 7 * kOnePly;

// AdaDeltaの設定
constexpr float kInitialStep = 0.05f; // 重みベクトルの最初の更新幅
constexpr float kDecay = 0.95f;       // AdaDelta原論文のρ
constexpr float kMomentum = 0.95f;    // モーメンタムの強さ
constexpr float kEpsilon = 1e-6f;     // AdaDelta原論文のε

struct PositionSample {
  bool in_check;
  bool gives_check;
  Move teacher_move;
  Score see_value_of_teacher_move;
  double progress;
  std::vector<MoveFeatureList> features;
};

struct AccuracyStats {
  double to_double() const {
    return all == 0.0 ? 0.0 : (good / all);
  }
  double good = 0.0;
  double all  = 0.0;
};

struct LearningStats {
  double logistic_loss = 0.0;
  double lasso_loss    = 0.0;
  double tikhonov_loss = 0.0;
  double prediction    = 0.0;
  AccuracyStats accuracy;
  Array<AccuracyStats, 3> accuracy_all;
  Array<AccuracyStats, 3> accuracy_evasions;
  Array<AccuracyStats, 3> accuracy_captures;
  Array<AccuracyStats, 3> accuracy_promotions;
  Array<AccuracyStats, 3> accuracy_checks;
  Array<AccuracyStats, 3> accuracy_good_quiet_moves;
  Array<AccuracyStats, 3> accuracy_bad_quiet_moves;
  Array<AccuracyStats, 3> accuracy_good_quiet_drops;
  Array<AccuracyStats, 3> accuracy_bad_quiet_drops;
  ArrayMap<Array<AccuracyStats, 3>, PieceType> accuracy_by_type;
  int num_moves_learned     = 0;
  int num_teacher_positions = 0;
  int num_moves_tested      = 0;
  int num_test_positions    = 0;
};

typedef Pack<float, 4> PackedWeight;
typedef std::valarray<PackedWeight> Weights;
Weights g_weights;

inline float HorizontalAdd(PackedWeight weight) {
  return weight[0] + weight[1] + weight[2] + weight[3];
}

inline PackedWeight GetProgressCoefficient(float progress) {
  const float s = 0.25f;
  float x = progress;
  float a = (1.0f - s) * std::max(-4.0f * (x * x) + 1.0f, 0.0f);
  float b = ((1.0f - s) - a) * (1.0f - x);
  float c = ((1.0f - s) - a) * x;
  float d = s;
  return PackedWeight(a, b, c, d);
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
  for (size_t move_id = 0; move_id < sample.features.size(); ++move_id) {
    PackedWeight sum(0.0f);
    const MoveFeatureList& feature_list = sample.features.at(move_id);

    // 静的な指し手の特徴に関して、重みを加算する
    for (MoveFeatureIndex feature_index : feature_list) {
      sum += g_weights[feature_index];
    }

    // 動的な指し手の特徴に関して、重みを加算する
    for (size_t i = 0; i < feature_list.continuous_values.size(); ++i) {
      float value = feature_list.continuous_values[i];
      sum += g_weights[kNumBinaryMoveFeatures + i] * value;
    }

    // 進行度に応じて内分を取る
    PackedWeight score = sum * coefficient;
    move_scores[move_id] = HorizontalAdd(score);
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

      // 最序盤の手については、学習対象から外す（定跡を使うことが多いため、探索対象となりにくい）
      if (ply < kLearningStartPly) {
        node.MakeMove(teacher_move);
        node.Evaluate(); // 評価関数の差分計算を行うために必要
        continue;
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
      HistoryStats* countermoves_history = nullptr;
      HistoryStats* followupmoves_history = nullptr;

#if 1
      {
        // １手前の手、２手前の手を取得する
        Move previous_move1 = node.move_before_n_ply(1);
        Move previous_move2 = node.move_before_n_ply(2);

        // ２手前の局面に戻って、探索を行う
        // （２手前の局面に戻るのは、カウンター手・フォローアップ手の統計をとるため）
        if (previous_move1 != kMoveNone && previous_move2 != kMoveNone) {
          // ２手前の局面にもどる
          node.UnmakeMove(previous_move1);
          node.UnmakeMove(previous_move2);

          // 探索を行う
          search.PrepareForNextSearch();
          search.set_learning_mode(true);
          search.AlphaBetaSearch(node, -kScoreInfinite, kScoreInfinite, kSearchDepth);

          // 元の局面に戻る
          node.MakeMove(previous_move2);
          node.Evaluate(); // 評価関数の差分計算を行うために必要
          node.MakeMove(previous_move1);
          node.Evaluate(); // 評価関数の差分計算を行うために必要

          // ヒストリーテーブルへのポインタをセットする
          countermoves_history = shared_data.countermoves_history[previous_move1];
          followupmoves_history = shared_data.countermoves_history[previous_move2];
        }
      }
#endif

      // すべての合法手について、指し手の特徴を求める
      PositionSample sample;
      PositionInfo pos_info(node, search.history(), search.gains(),
                            countermoves_history, followupmoves_history);
      sample.in_check = node.in_check();
      sample.gives_check = node.MoveGivesCheck(teacher_move);
      sample.teacher_move = teacher_move;
      sample.see_value_of_teacher_move = Swap::Evaluate(teacher_move, node);
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
      // 静的な特徴の勾配をアップデートする
      for (auto feature_index : feature_list) {
        g[feature_index] += delta;
      }
      // 動的な特徴の勾配をアップデートする
      for (size_t i = 0; i < feature_list.continuous_values.size(); ++i) {
        float value = feature_list.continuous_values[i];
        g[kNumBinaryMoveFeatures + i] += value * delta;
      }
    };

    // 各指し手の確率を求める
    const PositionSample& sample = position_samples.at(pos_id);
    std::valarray<double> probabilities = ComputeMoveProbabilities(sample);

    // 勾配のアップデートを行う
    PackedWeight coefficient = GetProgressCoefficient(sample.progress);
    for (size_t i = 0; i < sample.features.size(); ++i) {
      float delta = (i == 0) ? (probabilities[0] - 1.0) : probabilities[i];
      update(delta * coefficient, sample.features.at(i));
    }

    // 統計情報の計算
    num_moves += sample.features.size();
    loss += -std::log(probabilities[0]);
    prediction += (probabilities[0] == probabilities.max());
  }

  // 2. 全スレッドの勾配を集計する
  *gradients = PackedWeight(0.0f);
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
    r = kDecay * r + (1.0f - kDecay) * (gradient * gradient);
    PackedWeight eta = s.apply(std::sqrt) / (r + kEpsilon).apply(std::sqrt);
    v = kMomentum * v - (1.0f - kMomentum) * eta * gradient;
    s = kDecay * s + (1.0f - kDecay) * (v * v);
    w = w + v;

    // ペナルティをかける（FOBOS）
    // （参考文献）
    //   - John Duchi, Yoram Singer: Efficient Learning with Forward-Backward Splitting,
    //     http://web.stanford.edu/~jduchi/projects/DuchiSi09c_slides.pdf,
    //     pp.12-13, 2009.
    //   - Zachary C. Lipton, Charles Elkan: Efficient Elastic Net Regularization for Sparse Linear Models,
    //     http://zacklipton.com/media/papers/lazy-updates-elastic-net-lipton2015_1.pdf,
    //     p.9, 2015.
    auto ramp = [](float x) -> float {
      return std::max(x, 0.0f);
    };
    PackedWeight lambda1 = eta * kL1Penalty;
    PackedWeight lambda2 = eta * kL2Penalty;
    w = w.apply(math::sign) * ((w.apply(std::abs) - lambda1) / (1.0f + lambda2)).apply(ramp);
  }

  // 統計情報の記録
  stats->lasso_loss = lasso;
  stats->tikhonov_loss = tikhonov;
}

void ComputeAccuracy(const std::vector<PositionSample>& position_samples,
                     LearningStats* const stats) {
  assert(stats != nullptr);

  int num_moves = 0;

#pragma omp parallel for reduction(+:num_moves) schedule(dynamic)
  for (size_t pos_id = 0; pos_id < position_samples.size(); ++pos_id) {
    const PositionSample& sample = position_samples.at(pos_id);
    std::valarray<double> probabilities = ComputeMoveProbabilities(sample);
    double point = probabilities[0] == probabilities.max()
                 ? 1.0 / std::count(&probabilities[0], &probabilities[probabilities.size()-1], probabilities[0])
                 : 0.0;

    int index = std::min(int(3.0 * sample.progress), 3);

#pragma omp critical
    {
      // 総合正解率の調査
      stats->accuracy.all += 1.0;
      stats->accuracy.good += point;

      // 進行度ごとの正解率の調査
      stats->accuracy_all[index].all += 1.0;
      stats->accuracy_all[index].good += point;

      // 教師手の属性ごとの正解率の調査
      if (sample.in_check) {
        stats->accuracy_evasions[index].all += 1.0;
        stats->accuracy_evasions[index].good += point;
      } else {
        if (sample.teacher_move.is_capture()) {
          stats->accuracy_captures[index].all += 1.0;
          stats->accuracy_captures[index].good += point;
        }
        if (sample.teacher_move.is_promotion()) {
          stats->accuracy_promotions[index].all += 1.0;
          stats->accuracy_promotions[index].good += point;
        }
        if (sample.gives_check) {
          stats->accuracy_checks[index].all += 1.0;
          stats->accuracy_checks[index].good += point;
        }
        if (sample.teacher_move.is_quiet()) {
          if (sample.teacher_move.is_drop()) {
            if (sample.see_value_of_teacher_move >= 0) {
              stats->accuracy_good_quiet_drops[index].all += 1.0;
              stats->accuracy_good_quiet_drops[index].good += point;
            } else {
              stats->accuracy_bad_quiet_drops[index].all += 1.0;
              stats->accuracy_bad_quiet_drops[index].good += point;
            }
          } else {
            if (sample.see_value_of_teacher_move >= 0) {
              stats->accuracy_good_quiet_moves[index].all += 1.0;
              stats->accuracy_good_quiet_moves[index].good += point;
            } else {
              stats->accuracy_bad_quiet_moves[index].all += 1.0;
              stats->accuracy_bad_quiet_moves[index].good += point;
            }
          }
        }
      }

      // 駒の種類ごとの正解率
      stats->accuracy_by_type[sample.teacher_move.piece_type()][index].all += 1.0;
      stats->accuracy_by_type[sample.teacher_move.piece_type()][index].good += point;
    }
  }

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
  PositionInfo pos_info(pos, history, gains, &history, &history);
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
  g_weights = PackedWeight(0.0f);
  momentum = PackedWeight(0.0f);
  accumulated_gradients = PackedWeight(0.0f);
  accumulated_deltas = PackedWeight((1.0f - kDecay) * std::pow(kInitialStep, 2.0) / std::pow(1.0 - kMomentum, 2.0));
  for (int i = 0; i < num_threads; ++i) {
    thread_local_gradients.emplace_back(kNumMoveFeatures);
  }

  // 指し手の特徴をあらかじめ求めておく
  // （評価関数の学習とは異なり、反復中にPVが変化するといったことがないため）
  std::vector<PositionSample> teacher_position_samples;
  std::vector<PositionSample> test_position_samples;
  ComputeMoveFeatures(teacher_data, &teacher_position_samples);
  ComputeMoveFeatures(test_data, &test_position_samples);

  LearningStats last_stats;

  for (int iteration = 1; iteration <= kNumIterations; ++iteration) {
    // 統計情報を保存するための変数を準備する
    LearningStats stats;

    // 勾配をリセットする
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < thread_local_gradients.size(); ++i) {
      thread_local_gradients.at(i) = PackedWeight(0.0f);
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
                stats.accuracy.to_double(),
                stats.num_teacher_positions,
                stats.num_moves_learned);

    last_stats = stats;
  }

  // 詳細な一致率データを表示する
  auto print_accuracy = [](const char* name, const Array<AccuracyStats, 3>& s) {
    std::printf("%20s   %0.3f   %0.3f   %0.3f   %0.5f\n",
                name,
                s[0].to_double(),
                s[1].to_double(),
                s[2].to_double(),
                (s[0].good + s[1].good + s[2].good) / (s[0].all + s[1].all + s[2].all));
  };
  std::printf("%20s %7s %7s %7s %9s\n", "", "opening", "middle", "end", "total");
  print_accuracy("all", last_stats.accuracy_all);
  print_accuracy("evasions", last_stats.accuracy_evasions);
  print_accuracy("captures", last_stats.accuracy_captures);
  print_accuracy("promotions", last_stats.accuracy_promotions);
  print_accuracy("checks", last_stats.accuracy_checks);
  print_accuracy("good-quiet-moves", last_stats.accuracy_good_quiet_moves);
  print_accuracy("bad-quiet-moves", last_stats.accuracy_bad_quiet_moves);
  print_accuracy("good-quiet-drops", last_stats.accuracy_good_quiet_drops);
  print_accuracy("bad-quiet-drops", last_stats.accuracy_bad_quiet_drops);
  std::printf("\n");
  for (PieceType pt : Piece::all_piece_types()) {
    print_accuracy(Piece(kBlack, pt).ToSfen().c_str(), last_stats.accuracy_by_type[pt]);
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

ProbabilityCacheTable MoveProbability::cache_table_;

std::unordered_map<uint32_t, float> MoveProbability::ComputeProbabilities(
    const Position& pos, const HistoryStats& history, const GainsStats& gains,
    const HistoryStats* countermoves_history,
    const HistoryStats* followupmoves_history) {
  // 合法手を生成する
  SimpleMoveList<kAllMoves, true> legal_moves(pos);
  assert(legal_moves.size() >= 1);

  // 局面情報を収集する
  PositionSample sample;
  PositionInfo pos_info(pos, history, gains, countermoves_history, followupmoves_history);
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

/**
  * 指し手が指される確率を計算します（キャッシュ機能付き）.
  *
  * 一度確率を計算した局面であれば、キャッシュを用いて確率の計算が高速化されます。
  * なお、キャッシュを用いた場合であっても、探索情報については、最新のものが反映されます。
  */
 std::valarray<double> MoveProbability::ComputeProbabilitiesWithCache(
     const Position& pos, const HistoryStats& history, const GainsStats& gains,
     const HistoryStats* countermoves_history,
     const HistoryStats* followupmoves_history) {

   // 0. ソフトマックス関数の準備
   auto softmax = [](const std::valarray<double>& x) -> std::valarray<double> {
     std::valarray<double> temp = std::exp(x - x.max());
     return temp / temp.sum();
   };

   // 1. 進行度に応じた係数を求める
   const double progress = Progress::EstimateProgress(pos);
   const PackedWeight progress_coefficient = GetProgressCoefficient(progress);

   // 2. 合法手を生成する
   SimpleMoveList<kAllMoves, true> legal_moves(pos);
   assert(legal_moves.size() >= 1);

   // 3. 静的な指し手の特徴から、指し手に点数を付ける
   std::valarray<float> static_move_scores(legal_moves.size());
   const Key64 cache_key = ProbabilityCacheTable::ComputeKey(pos);
   cache_table_.Lock(cache_key);
   const ProbabilityCacheTable::Entry* entry = cache_table_.LookUp(cache_key);
   if (entry != nullptr) {
     // すでに計算済みだった場合は、キャッシュから持ってくる
     static_move_scores = entry->data;
   } else {
     // キャッシュになければ、ここで特徴抽出から始める必要があるので、いったんロックを外す
     cache_table_.Unlock(cache_key);

     // 局面情報を収集する
     PositionSample sample;
     PositionInfo pos_info(pos, history, gains, countermoves_history, followupmoves_history);

     // 各指し手ごとに、静的な特徴を抽出する
     for (size_t move_id = 0; move_id < legal_moves.size(); ++move_id) {
       const Move move = legal_moves[move_id].move;

       // 特徴を抽出する
       MoveFeatureList features = ExtractMoveFeatures(move, pos, pos_info);

       // 静的な指し手の重みを合計する
       PackedWeight sum(0.0f);
       for (MoveFeatureIndex feature_index : features) {
         sum += g_weights[feature_index];
       }

       // 連続値を取る特徴の重みも追加
       sum += g_weights[kNumBinaryMoveFeatures + kSeeValue] * features.continuous_values[kSeeValue];
       sum += g_weights[kNumBinaryMoveFeatures + kGlobalSeeValue] * features.continuous_values[kGlobalSeeValue];

       // 進行度に応じて内分を取る
       PackedWeight static_score = sum * progress_coefficient;
       static_move_scores[move_id] = HorizontalAdd(static_score);
     }

     // キャッシュに保存する
     cache_table_.Lock(cache_key);
     cache_table_.Save(cache_key, static_move_scores);
   }
   cache_table_.Unlock(cache_key);

   // 4. 探索中に動的に値が変化する特徴（ヒストリー値など）について、得点を計算する
   std::valarray<double> move_scores(legal_moves.size());
   for (size_t move_id = 0; move_id < legal_moves.size(); ++move_id) {
     const Move move = legal_moves[move_id].move;

     // 動的な特徴を抽出する
    Array<float, kNumContinuousMoveFeatures> continuous_features =
        ExtractDynamicMoveFeatures(move, history, gains, countermoves_history,
                                   followupmoves_history);


     // 動的な指し手の特徴に関して、重みを加算する
     PackedWeight sum(0.0f);
     for (size_t i = kHistoryValue; i <= kEvaluationGain; ++i) {
       float value = continuous_features[i];
       sum += g_weights[kNumBinaryMoveFeatures + i] * value;
     }

     // 進行度に応じて内分を取る
     PackedWeight score = sum * progress_coefficient;
     float dynamic_score = HorizontalAdd(score);

     // 静的な特徴の得点と、動的な特徴の得点を合計する
     move_scores[move_id] = static_move_scores[move_id] + dynamic_score;
   }

   // 5. ソフトマックス関数を適用して、それぞれの指し手の確率を求める
   return softmax(move_scores);
}

const ProbabilityCacheTable::Entry* ProbabilityCacheTable::LookUp(Key64 key) const {
   for (Entry& entry : table_[key & key_mask_]) {
     if (key == entry.key) {
       entry.age = age_;    // 情報の鮮度を更新
       entry.num_lookups++; // 参照数を１増やす
       return &entry;
     }
   }
   return nullptr;
 }

 void ProbabilityCacheTable::Save(Key64 key, const std::valarray<float>& data) {
   // 1. 保存先を探す
   Bucket& bucket = table_[key & key_mask_];
   Entry* save_point = bucket.begin();
   for (Entry& entry : bucket) {
     // a. 空きエントリや完全一致エントリが見つかった場合
     if (entry.key == Key64(0) || entry.key == key) {
       save_point = &entry;
       break;
     }
     // b. 置き換える場合（既存の情報が古い場合、参照数が少ない場合）
     if (   entry.age != age_
         || entry.num_lookups < save_point->num_lookups) {
       save_point = &entry;
     }
   }

   // 2. 情報を保存する
   save_point->key = key;
   save_point->data = data;
   save_point->num_lookups = 0;
   save_point->age = age_;
 }

 void ProbabilityCacheTable::SetSize(size_t size) {
   // 要素数をセット（必ず２の累乗になるようにする）
   size_ = static_cast<size_t>(1) << bitop::bsr64(size);
   key_mask_ = size_ - 1;

   // 要素数分のメモリ領域を確保
   table_.reset(new Bucket[size_]);

   // 初期化
   age_ = 0;
 }

 void ProbabilityCacheTable::Clear() {
   for (size_t i = 0; i < size_; ++i) {
     Bucket& bucket = table_[i];
     for (size_t j = 0; j < bucket.size(); ++j) {
       bucket[j].key = Key64(0);
       bucket[j].data.resize(0);
       bucket[j].num_lookups = 0;
       bucket[j].age = 0;
     }
   }
 }

 /**
  * 実現確率のキャッシュに用いるハッシュテーブルのキーを返します.
  * 実現確率の計算においては、局面そのものの情報のみならず、その局面に至る直近４手も考慮されているため、
  * 局面と経路の両方を考慮したハッシュ値になっています。
  */
 Key64 ProbabilityCacheTable::ComputeKey(const Position& pos) {
   auto compute_path_hash = [](Move move, int ply) -> Key64 {
     if (move == kMoveNone) {
       return Key64(0);
     } else if (move == kMoveNull) {
       return Zobrist::null_move_on_path(ply);
     } else if (move.is_drop()) {
       return Zobrist::path(move.piece(), move.to(), ply);
     } else {
       Key64 key;
       Square from = move.from(), to = move.to();
       key -= Zobrist::path(move.piece(), from, ply);
       key -= Zobrist::path(move.captured_piece(), to, ply);
       key += Zobrist::path(move.piece_after_move(), to, ply);
       return key;
     }
   };

   // ４手前まで遡ってハッシュ値を計算する（現在の実現確率の実装では、４手前までの特徴を見ているため。）
   Key64 key = pos.ComputePositionKey();
   for (int i = 1; i <= 4 && i <= pos.game_ply(); ++i) {
     key += compute_path_hash(pos.move_before_n_ply(i), pos.game_ply() - i);
   }
   return key;
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
