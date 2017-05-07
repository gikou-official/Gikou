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

#include "teacher_data.h"

#include <fstream>
#include <random>
#include <omp.h>
#include "common/progress_timer.h"
#include "position.h"
#include "mate3.h"
#include "movegen.h"
#include "move_probability.h"
#include "search.h"
#include "task_thread.h"

namespace {

/**
 * 時間計測を行うためのスレッドです.
 */
class TimerThread : public TaskThread {
 public:
  explicit TimerThread(Signals& signals)
      : signals_(signals) {
  }

  void Run() {
    std::chrono::milliseconds stop_time(time_limit_);
    std::this_thread::sleep_for(stop_time);
    signals_.stop = true;
  }

  void set_time_limit(int time) {
    time_limit_ = time;
  }

 private:
  Signals& signals_;
  int time_limit_ = 100;
};

/**
 * ランダムに局面を作成します.
 *
 * ランダム局面の生成方法は、(1)実戦における出現可能性と、(2)局面の多様性の両方を担保するため、
 *   1. 初期局面から(ply - 1)手目までは、実現確率に従って指し手を決める
 *   2. 最後の１手については、一様乱数で指してを決める
 * という方式を採用しています（idea from AlphaGo）。
 *
 * （参考文献）
 *   - David Silver, et al.: Mastering the game of Go with deep neural networks
 *     and tree search, Nature 529, pp.484-489, 2016.
 *
 * @param ply 初期局面から数えて何手目の局面を生成するか
 * @param rng 乱数生成器（Random Number Generator）
 * @return ランダムな局面
 */
Position GenerateRandomPosition(const int ply, std::mt19937& rng) {
  assert(ply >= 0);

  HistoryStats history;
  GainsStats gains;
  Position pos;

restart:
  pos = Position::CreateStartPosition();

  // Step 1. 初期局面から(ply - 1)手目までは、実現確率に従って指し手を決める
  for (int i = 0; i < (ply - 1); ++i) {
    // 合法手がなくなってしまった場合は、もう１度最初から始める
    if (SimpleMoveList<kAllMoves, true>(pos).empty()) {
      goto restart;
    }

    // 各指し手の実現確率を計算する
    HistoryStats* cmh = nullptr;
    HistoryStats* fmh = nullptr;
    auto probabilities = MoveProbability::ComputeProbabilities(pos, history, gains, cmh, fmh);

    // 実現確率に従って、ランダムに１手選ぶ
    Move selected_move = Move::FromUint32(probabilities.begin()->first);
    float sum = 0.0;
    float threashold = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    for (const std::pair<uint32_t, float>& pair : probabilities) {
      Move move = Move::FromUint32(pair.first);
      float probability = pair.second;
      sum += probability;
      if (sum >= threashold) {
        selected_move = move;
        break;
      }
    }

    // 選択された指し手に従って、局面を進める
    pos.MakeMove(selected_move);
  }

  // Step 2. 最後の１手は、一様乱数を用いてランダムに選ぶ
  SimpleMoveList<kAllMoves, true> all_moves(pos);
  if (all_moves.size() == 0) {
    goto restart;
  }
  int move_id = std::uniform_int_distribution<int>(0, all_moves.size() - 1)(rng);
  pos.MakeMove(all_moves[move_id].move);

  // Step 3. 勝ち負けがはっきりした局面が生成された場合は、再度生成し直す
  Mate3Result mate3;
  if (   SimpleMoveList<kAllMoves, true>(pos).size() == 0
      || (!pos.in_check() && IsMateInThreePlies(pos, &mate3))) {
    goto restart;
  }

  return pos;
}

} // namespace

bool TeacherPv::ReadFromFile(std::FILE* stream) {
  std::fread(&huffman_code, sizeof(huffman_code), 1, stream);
  std::fread(&progress, sizeof(progress), 1, stream);
  std::fread(&game_result, sizeof(game_result), 1, stream);
  int pv_data_size;
  std::fread(&pv_data_size, sizeof(pv_data_size), 1, stream);
  pv_data.resize(pv_data_size);
  std::fread(&pv_data[0], sizeof(pv_data[0]), pv_data.size(), stream);
  pv_data.shrink_to_fit(); // メモリ使用量を節約する
  return !std::feof(stream);
}

void TeacherPv::WriteToFile(std::FILE* stream) const {
  if (!pv_data.empty()) {
    std::fwrite(&huffman_code, sizeof(huffman_code), 1, stream);
    std::fwrite(&progress, sizeof(progress), 1, stream);
    std::fwrite(&game_result, sizeof(game_result), 1, stream);
    int pv_data_size = pv_data.size();
    std::fwrite(&pv_data_size, sizeof(pv_data_size), 1, stream);
    std::fwrite(&pv_data[0], sizeof(pv_data[0]), pv_data.size(), stream);
  }
}

std::vector<std::vector<Move>> TeacherPv::GetPvList() const {
  std::vector<std::vector<Move>> pv_list;
  for (size_t i = 0; i < pv_data.size(); ++i) {
    pv_list.emplace_back();
    for (Move m; (m = pv_data[i]) != kMoveNone; ++i) {
      pv_list.back().push_back(m);
    }
  }
  return pv_list;
}

void TeacherData::GenerateTeacherPositions() {
  // 生成する教師局面の数
  const int kNumPositions = 30 * 1000 * 1000;

  // スレッド数の設定
  const int num_threads = std::max(1U, std::thread::hardware_concurrency());
  omp_set_num_threads(num_threads);
  std::printf("Set num_threads = %d\n", num_threads);

  // スレッドごとの乱数生成器や置換表などを準備する
  std::random_device rd;
  std::vector<std::mt19937> random_number_generators;
  std::vector<SharedData> shared_datas(num_threads);
  std::vector<std::unique_ptr<TimerThread>> timer_threads;
  for (int i = 0; i < num_threads; ++i) {
    random_number_generators.emplace_back(rd());
    shared_datas.at(i).hash_table.SetSize(2);
    timer_threads.emplace_back(new TimerThread(shared_datas.at(i).signals));
    timer_threads.back()->StartNewThread();
    timer_threads.back()->WaitForReady();
  }

  std::FILE* teacher_data_file = std::fopen("teacher_positions.bin", "wb");
  ProgressTimer progress_timer(kNumPositions);

  // 教師局面の生成を繰り返す
#pragma omp parallel for schedule(dynamic)
  for (int pos_id = 0; pos_id < kNumPositions; ++pos_id) {
    int thread_id = omp_get_thread_num();
    SharedData& shared = shared_datas.at(thread_id);
    std::mt19937& rng = random_number_generators.at(thread_id);
    std::unique_ptr<TimerThread>& timer_thread = timer_threads.at(thread_id);

    // 初期局面から数えて何手目の局面を生成するかを決定する
    // プロの棋譜の終局手数をグラフ化してみると、概ね対数正規分布で近似できることが分かったので、
    // ここでは、生成する局面の上限（game_length）を、対数正規分布に従った乱数で決定している。
    std::lognormal_distribution<double> length_distribution(4.717, 0.249);
    int game_length = std::max(static_cast<int>(length_distribution(rng)), 1);
    int ply = std::uniform_int_distribution<int>(1, game_length)(rng);

    // ランダムに局面を作成する（実現確率を用いて、初期局面からply手ランダムに動かす）
    Position pos = GenerateRandomPosition(ply, rng);

    // 探索の準備をする
    Search search(shared);
    shared.Clear();
    search.PrepareForNextSearch();

    // 別スレッドでの時間計測を開始する
    timer_thread->set_time_limit(100);
    timer_thread->ExecuteTask();

    // 探索を行う
    std::pair<Move, Score> pair = search.SimpleIterativeDeepening(pos);

    // 教師データを保存する
    TeacherPosition teacher;
    teacher.huffman_code = HuffmanCode::EncodePosition(pos);
    teacher.move = pair.first;
    teacher.score = pair.second;

    // 時間計測用のスレッドの処理が終了するのを待つ
    timer_thread->WaitUntilTaskIsFinished();

    // 生成した教師局面をファイルに保存する（排他制御を行う）
#pragma omp critical
    std::fwrite(&teacher, sizeof(teacher), 1, teacher_data_file);

    // 進行状況を表示する
    progress_timer.IncrementCounter();
    progress_timer.PrintProgress("");
  }

  std::fclose(teacher_data_file);
}

void TeacherData::GenerateTeacherGames() {
  // 生成する自己対戦データの数（何回自己対戦を行うか）
  const int kNumGames = 3 * 1000 * 1000;

  // スレッド数の設定
  const int num_threads = std::max(1U, std::thread::hardware_concurrency());
  omp_set_num_threads(num_threads);

  // スレッドごとの乱数生成器や置換表などを準備する
  std::random_device rd;
  std::vector<std::mt19937> random_number_generators;
  std::vector<SharedData> shared_datas(num_threads);
  std::vector<std::unique_ptr<TimerThread>> timer_threads;
  for (int i = 0; i < num_threads; ++i) {
    random_number_generators.emplace_back(rd());
    shared_datas.at(i).hash_table.SetSize(8);
    timer_threads.emplace_back(new TimerThread(shared_datas.at(i).signals));
    timer_threads.back()->StartNewThread();
    timer_threads.back()->WaitForReady();
  }

  std::FILE* teacher_games_file = std::fopen("teacher_games.bin", "wb");
  ProgressTimer progress_timer(kNumGames);

  // 教師局面の生成を繰り返す
#pragma omp parallel for schedule(dynamic)
  for (int pos_id = 0; pos_id < kNumGames; ++pos_id) {
    int thread_id = omp_get_thread_num();
    SharedData& shared = shared_datas.at(thread_id);
    std::mt19937& rng = random_number_generators.at(thread_id);
    std::unique_ptr<TimerThread>& timer_thread = timer_threads.at(thread_id);

    // ランダムに開始局面を作成する
    int start_ply = std::uniform_int_distribution<int>(16, 31)(rng);
    const Position start_position = GenerateRandomPosition(start_ply, rng);
    Node node(start_position);
    Search search(shared);

    // 自己対戦を行う（最大256手まで）
    std::vector<TeacherPosition> teacher_positions;
    Game::Result game_result = Game::kDraw;
    for (int ply = start_ply; ply < 256; ++ply) {
      // 探索の準備をする
      shared.Clear();
      search.PrepareForNextSearch();

      // 別スレッドでの時間計測を開始する
      timer_thread->set_time_limit(std::uniform_int_distribution<>(8, 12)(rng));
      timer_thread->ExecuteTask();

      // 探索を行う
      std::pair<Move, Score> pair = search.SimpleIterativeDeepening(node);
      Move best_move = pair.first;
      Score score = pair.second;

      // 時間計測用のスレッドの処理が終了するのを待つ
      timer_thread->WaitUntilTaskIsFinished();

      // 勝ち負けがはっきりしたときは、終了する
      if (score >= kScoreKnownWin) {
        game_result = node.side_to_move() == kBlack ? Game::kBlackWin : Game::kWhiteWin;
        break;
      } else if (score <= -kScoreKnownWin) {
        game_result = node.side_to_move() == kBlack ? Game::kWhiteWin : Game::kBlackWin;
        break;
      }

      // 千日手を検出したときは、終了する
      Score repetition_score;
      if (node.DetectRepetition(&repetition_score)) {
        game_result = Game::kDraw;
        break;
      }

      // 一定の確率（現在は10%の確率）で、自己対戦中の局面をサンプリングする
      if (std::uniform_int_distribution<int>(0, 9)(rng) == 0) {
        TeacherPosition teacher;
        teacher.huffman_code = HuffmanCode::EncodePosition(node);
        teacher.move = best_move;
        teacher.score = score;
        teacher_positions.push_back(teacher);
      }

      // 探索で得られた最善手を用いて、局面を進める
      node.MakeMove(best_move);
    }

    // ランダムにサンプリングされた教師局面をファイルに保存する
    if (game_result == Game::kBlackWin || game_result == Game::kWhiteWin) {
      // 教師局面に対局結果（どちらの手番が勝ったか）を保存する
      Color winner = game_result == Game::kBlackWin ? kBlack : kWhite;
      for (TeacherPosition& teacher : teacher_positions) {
        Position pos = HuffmanCode::DecodePosition(teacher.huffman_code);
        teacher.score = pos.side_to_move() == winner ? kScoreKnownWin : -kScoreKnownWin;
      }

      // 教師局面をファイルに保存する（排他制御を行う）
#pragma omp critical
      for (const TeacherPosition& teacher : teacher_positions) {
        std::fwrite(&teacher, sizeof(teacher), 1, teacher_games_file);
      }
    }

    // 進行状況を表示する
    progress_timer.IncrementCounter();
    progress_timer.PrintProgress("");
  }

  std::fclose(teacher_games_file);
}

void TeacherData::GenerateTeacherPvs() {
  const int kNumGames = 30000;
  const int kMinSearchDepth = 1;
  const int kMaxSearchDepth = 2;

  // スレッド数の設定
  const int num_threads = std::max(1U, std::thread::hardware_concurrency());
  omp_set_num_threads(num_threads);
  std::printf("Set num_threads = %d\n", num_threads);

  // データベースから棋譜を読み込む（学習用と、交差検定用の2つがある）
  std::printf("Extract the games from the database.\n");
  std::vector<Game> games;
  std::ifstream db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(db_file);
  game_db.set_title_matches_only(true);
  for (Game game; game_db.ReadOneGame(&game); ) {
    games.push_back(game);
    if (games.size() >= kNumGames) break;
  }

  // 乱数生成器をスレッドの数だけ準備する
  std::random_device random_device;
  std::vector<std::mt19937> random_number_generators;
  for (int i = 0; i < num_threads; ++i) {
    random_number_generators.emplace_back(random_device());
  }

  // PVを保存するファイルを開く
  std::FILE* pv_data_file = std::fopen("pv_data.bin", "wb");

  ProgressTimer progress_timer(games.size());

  // 対局を１つずつ調べていく
#pragma omp parallel for schedule(dynamic)
  for (size_t game_id = 0; game_id < games.size(); ++game_id) {
    const Game& game = games.at(game_id);
    Node node(Position::CreateStartPosition());
    SharedData shared_data;
    shared_data.hash_table.SetSize(128);
    std::vector<TeacherPv> teacher_pvs;

    // 棋譜中の局面を１つずつ調べていく
    for (size_t ply = 0; ply < game.moves.size(); ++ply) {
      const Move teacher_move = game.moves.at(ply);

      // 棋譜の手が非合法手または劣等手の場合には、この手以降の以降の棋譜の手を無視する
      if (teacher_move.IsInferior() || !node.MoveIsLegal(teacher_move)) {
        break;
      }

      // 合法手を生成する
      SimpleMoveList<kAllMoves, true> moves(node);

      if (moves.size() == 0) {
        break;
      } else if (moves.size() == 1) {
        continue;
      }

      // 棋譜の手を合法手リストの先頭に持ってくる
      auto iter = std::find_if(moves.begin(), moves.end(), [&](ExtMove em) {
        return em.move == teacher_move;
      });
      std::iter_swap(moves.begin(), iter);

      // 探索の準備をする
      Search search(shared_data);
      search.PrepareForNextSearch();
      std::mt19937& rng = random_number_generators.at(omp_get_thread_num());

      // 各合法手以下のPVを求める
      int num_pvs = 0;
      TeacherPv teacher;
      for (size_t i = 0; i < moves.size(); ++i) {
        const Move move = moves[i].move;

        // 探索深さは、指し手ごとに乱数で決定する
        std::uniform_int_distribution<int> dist(kMinSearchDepth, kMaxSearchDepth);
        Depth depth = dist(rng) * kOnePly;

        // 子局面に移動して、PVを求めるための探索を行う
        node.MakeMove(move);
        node.Evaluate(); // 評価関数の差分計算に必要
        Score score = -search.AlphaBetaSearch(node, -kScoreKnownWin, kScoreKnownWin, depth);
        node.UnmakeMove(move);

        // 詰みのスコアが出たときは、PVを保存せずにスキップする
        if (std::abs(score) >= kScoreKnownWin) {
          if (move == teacher_move)
            break;
          else
            continue;
        }

        // PVを保存する
        teacher.pv_data.push_back(move);
        for (Move m : search.pv_table()) {
          teacher.pv_data.push_back(m);
        }
        teacher.pv_data.push_back(kMoveNone);
        ++num_pvs;
      }

      if (num_pvs >= 2) {
        teacher.huffman_code = HuffmanCode::EncodePosition(node);
        teacher.progress = float(ply) / float(game.moves.size());
        teacher.game_result = game.result;
        teacher_pvs.push_back(teacher);
      }

      // 棋譜の指し手に沿って局面を進める
      node.MakeMove(teacher_move);
      node.Evaluate(); // 評価関数の差分計算に必要
    }

    // ファイルにPVを書き出す
#pragma omp critical
    for (const TeacherPv& t : teacher_pvs) {
      t.WriteToFile(pv_data_file);
    }

    // 進行状況を画面に表示する
    progress_timer.IncrementCounter();
    progress_timer.PrintProgress("Generating PVs...");
  }

  std::fclose(pv_data_file);
}

#endif // !defined(MINIMUM)
