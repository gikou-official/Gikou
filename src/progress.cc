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

#include "progress.h"

#include <fstream>
#include <thread>
#include <omp.h>
#include "common/math.h"
#include "gamedb.h"
#include "position.h"

ArrayMap<int32_t, Square, PsqIndex> Progress::weights;

void Progress::ReadWeightsFromFile() {
  FILE* fp = std::fopen("progress.bin", "rb");
  if (fp != NULL) {
    std::fread(&weights, sizeof(weights), 1, fp);
  } else {
    std::printf("info string Failed to open progress.bin.\n");
  }
}

double Progress::EstimateProgress(const Position& pos, const PsqList& psq_list) {
  int64_t sum = 0;
  Square sq_black_king = pos.king_square(kBlack);
  Square sq_white_king = Square::rotate180(pos.king_square(kWhite));
  for (const PsqPair& psq : psq_list) {
    sum += weights[sq_black_king][psq.black()];
    sum += weights[sq_white_king][psq.white()];
  }
  return math::sigmoid(double(sum) * double(1.0 / kWeightScale));
}

double Progress::EstimateProgress(const Position& pos) {
  const PsqList psq_list(pos);
  return EstimateProgress(pos, psq_list);
}

#ifndef MINIMUM

namespace {

template <typename T>
struct WeightsBase {
  void Clear() { std::memset(this, 0, sizeof(*this)); }
  size_t size() const { return sizeof(*this) / sizeof(T); }
  T* begin() { return reinterpret_cast<T*>(this); }
  T* end()   { return begin() + size(); }
  const T* begin() const { return reinterpret_cast<const T*>(this); }
  const T* end()   const { return begin() + size(); }
  T operator[](size_t n) const { return *(begin() + n); }
  T& operator[](size_t n) { return *(begin() + n); }
  ArrayMap<T, PsqIndex> psq;
  ArrayMap<Array<T, 154>, Piece> relative_kp;
  ArrayMap<T, Square, PsqIndex> absolute_kp;
};

typedef WeightsBase<double> Weights;

void UpdateWeights(const Position& pos, double inc, Weights* const w) {
  assert(w);
  Square sq_bk = pos.king_square(kBlack);
  Square sq_wk = Square::rotate180(pos.king_square(kWhite));
  for (const PsqPair& psq : PsqList(pos)) {
    const PsqIndex i_b = psq.black();
    const PsqIndex i_w = psq.white();
    // 1. 駒の絶対位置
    w->psq[i_b] += inc;
    w->psq[i_w] += inc;
    if (i_b.square() != kSquareNone) {
      // 2. 玉との相対位置
      w->relative_kp[i_b.piece()][Square::relation(sq_bk, i_b.square())] += inc;
      w->relative_kp[i_w.piece()][Square::relation(sq_wk, i_w.square())] += inc;
    }
    // 3. 玉との絶対位置
    w->absolute_kp[sq_bk][i_b] += inc;
    w->absolute_kp[sq_wk][i_w] += inc;
  }
}

} // namespace

void Progress::LearnParameters() {
  constexpr int kNumIterations = 2048;  // パラメタ更新の反復回数
  constexpr int kNumGames = 50000;      // 学習に利用する対局数
  constexpr int kNumSamples = 1000;     // 交差検定で用いるサンプル数
  constexpr double kL1Penalty = 0.0;    // L1正則化係数
  constexpr double kL2Penalty = 256.0;  // L2正則化係数
  constexpr double kInitialStep = 1e-3; // 重みベクトルの最初の更新幅
  constexpr double kDecay = 0.95;       // AdaDelta原論文のρ
  constexpr double kMomentum = 0.95;    // モーメンタムの強さ
  constexpr double kEpsilon = 1e-6;     // AdaDelta原論文のε

  // スレッド数の設定
  const int num_threads = std::max(1U, std::thread::hardware_concurrency());
  omp_set_num_threads(num_threads);
  std::printf("Set num_threads = %d\n", num_threads);

  const Position startpos = Position::CreateStartPosition();

  // 棋譜データの準備
  std::ifstream game_db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(game_db_file);
  std::vector<Game> games, samples;
  std::printf("start reading games.\n");
  for (int i = 0; i < kNumGames; ++i) {
    Game game;
    if (   game_db.ReadOneGame(&game)
        && game.result != Game::kDraw
        && game.moves.size() < 256) {
      games.push_back(game);
    }
  }
  for (int i = 0; i < kNumSamples; ++i) {
    Game game;
    if (   game_db.ReadOneGame(&game)
        && game.result != Game::kDraw
        && game.moves.size() < 256) {
      samples.push_back(game);
    }
  }
  std::printf("finish reading games.\n");

  Weights current_weights, momentum, accumulated_gradients, accumulated_deltas;
  std::vector<Weights> thread_local_gradients(num_threads);
  momentum.Clear();
  current_weights.Clear();
  accumulated_gradients.Clear();
  std::fill(accumulated_deltas.begin(), accumulated_deltas.end(),
            (1.0 - kDecay) * std::pow(kInitialStep, 2.0) / std::pow(1.0 - kMomentum, 2.0));

  for (int iteration = 1; iteration <= kNumIterations; ++iteration) {
    std::printf("iteration=%d ", iteration);

    int num_moves = 0;
    double offset = 0, sum_square_diff = 0;
    for (Weights& g : thread_local_gradients) {
      g.Clear();
    }

    // 1. 勾配を計算する
#pragma omp parallel for reduction(+:offset, sum_square_diff, num_moves) schedule(dynamic)
    for (size_t game_id = 0; game_id < games.size(); ++game_id) {
      const Game& game = games[game_id];
      Position pos = startpos;

      for (int ply = 0, n = game.moves.size(); ply < n; ++ply) {
        double teacher = double(ply) / double(n);
        double actual = EstimateProgress(pos);
        double diff = actual - teacher;

        offset += diff;
        sum_square_diff += (diff * diff);
        num_moves += 1;

        int thread_id = omp_get_thread_num();
        double delta = diff * math::derivative_of_sigmoid(actual);
        UpdateWeights(pos, delta, &thread_local_gradients[thread_id]);

        pos.MakeMove(game.moves[ply]);
      }
    }

    // 2. 勾配を集計する
    Weights gradients;
    gradients.Clear();
    for (const Weights& g : thread_local_gradients) {
      for (size_t i = 0; i < g.size(); ++i) {
        gradients[i] += g[i];
      }
    }

    // 3. 重みを更新する
    double lasso = 0, tikhonov = 0;
#pragma omp parallel for reduction(+:lasso, tikhonov) schedule(static)
    for (size_t i = 0; i < gradients.size(); ++i) {
      double& w = current_weights[i];
      double& r = accumulated_gradients[i];
      double& s = accumulated_deltas[i];
      double& v = momentum[i];
      double gradient = gradients[i];
      // 損失を計算する
      lasso    += kL1Penalty * std::abs(w);
      tikhonov += kL2Penalty * (w * w);
      // 勾配方向に移動する（AdaDelta + Momentum）
      r = kDecay * r + (1.0 - kDecay) * (gradient * gradient);
      double eta = std::sqrt(s) / std::sqrt(r + kEpsilon);
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
      double lambda1 = eta * kL1Penalty;
      double lambda2 = eta * kL2Penalty;
      w = math::sign(w) * std::max((std::abs(w) - lambda1) / (1.0 + lambda2), 0.0);
    }

    // 4. 計算用の重みにコピーする
    for (Square king_sq : Square::all_squares()) {
      for (PsqIndex i : PsqIndex::all_indices()) {
        double w = current_weights.psq[i];
        if (i.square() != kSquareNone) {
          w += current_weights.relative_kp[i.piece()][Square::relation(king_sq, i.square())];
        }
        w += current_weights.absolute_kp[king_sq][i];
        weights[king_sq][i] = int32_t(w * kWeightScale);
      }
    }

    // 5. 交差検定を行う
    int num_samples = 0;
    double sample_square_diff = 0;
#pragma omp parallel for reduction(+:num_samples, sample_square_diff) schedule(dynamic)
    for (size_t sample_id = 0; sample_id < samples.size(); ++sample_id) {
      const Game& sample = samples[sample_id];
      Position pos = startpos;
      for (int ply = 0, n = sample.moves.size(); ply < n; ++ply) {
        double teacher = double(ply) / double(n);
        double diff = teacher - EstimateProgress(pos);
        sample_square_diff += diff * diff;
        ++num_samples;
        pos.MakeMove(sample.moves[ply]);
      }
    }

    // 6. 情報表示
    double loss = sum_square_diff + lasso;
    double stddev = std::sqrt(sum_square_diff / num_moves);
    double sample_stddev = std::sqrt(sample_square_diff / num_samples);
    std::printf("Loss=%f L1=%f L2=%f Stddev=%f Validation=%f Startpos=%0.4f moves=%d\n",
                loss, lasso, tikhonov, stddev, sample_stddev,
                EstimateProgress(startpos), num_moves);
  }

  // ファイルへの書き出し
  FILE* fout = std::fopen("progress.bin", "wb");
  std::fwrite(&weights, sizeof weights, 1, fout);
  std::fclose(fout);

  // 結果のプリント
  for (Piece piece : Piece::all_pieces()) {
    std::printf("Piece:%s\n", piece.ToSfen().c_str());
    for (Rank r = kRank1; r <= kRank9; ++r) {
      for (File f = kFile9; f >= kFile1; --f) {
        Square king_sq = kSquare8H;
        PsqIndex i = PsqIndex::OfBoard(piece, Square(f, r));
        std::printf(" %6f", double(weights[king_sq][i]) / kWeightScale);
      }
      std::printf("\n");
    }
  }
}

#endif // MINIMUM
