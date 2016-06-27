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

#include "cli.h"

#include <algorithm>
#include <fstream>
#include <vector>
#include <unordered_map>
#include "common/array.h"
#include "common/simple_timer.h"
#include "book.h"
#include "cluster.h"
#include "consultation.h"
#include "gamedb.h"
#include "learning.h"
#include "mate1ply.h"
#include "mate3.h"
#include "movegen.h"
#include "move_probability.h"
#include "position.h"
#include "progress.h"
#include "search.h"
#include "thinking.h"
#include "usi.h"
#include "usi_protocol.h"

#if !defined(MINIMUM)

namespace {

void BenchmarkSearch();
void BenchmarkMoveGeneration(int num_calls);
void BenchmarkMateSearch(int num_calls, int ply);

// [2016/6/27 追加]ランダムプレーヤーによる１手詰関数のテスト
void TestMate1ByRandomPlayer();

void CreateBook(const char* output_file_name);
void ComputeStatsOfGameDatabase(const char* event_name);
void ComputeAllPossibleQuietMoves();
void ComputePlayerRatings();

} // namespace

void Cli::ExecuteCommand(int argc, char* argv[]) {
  // 特に起動オプションが指定されていない場合は、何もせずに終了する
  if (argc < 2) {
    std::printf("CLI: No command.\n");
    return;
  }

  // コマンドを取得する
  const std::string command(argv[1]);

  // コマンドを実行する
  if (command == "--bench") {
    BenchmarkSearch();
  } else if (command == "--bench-movegen") {
    int num_tries = argc >= 3 ? std::atoi(argv[2]) : 1;
    BenchmarkMoveGeneration(num_tries);
  } else if (command == "--bench-mate1") {
    int num_tries = argc >= 3 ? std::atoi(argv[2]) : 1;
    BenchmarkMateSearch(num_tries, 1);
  } else if (command == "--bench-mate3") {
    int num_tries = argc >= 3 ? std::atoi(argv[2]) : 1;
    BenchmarkMateSearch(num_tries, 3);

  // [2016/6/27 追加]ランダムプレーヤーによる１手詰関数のテスト
  } else if (command == "--random-player-mate1") {
    TestMate1ByRandomPlayer();

  } else if (command == "--cluster") {
    Cluster cluster;
    cluster.Start();
  } else if (command == "--compute-all-quiets") {
    ComputeAllPossibleQuietMoves();
  } else if (command == "--consultation") {
    Consultation consultation;
    consultation.Start();
  } else if (command == "--create-book") {
    const char* output_file_name = argc >= 3 ? argv[2] : "book.bin";
    CreateBook(output_file_name);
  } else if (command == "--db-stats") {
    const char* event_name = argc >= 3 ? argv[2] : nullptr;
    ComputeStatsOfGameDatabase(event_name);
  } else if (command == "--learn") {
    Learning::LearnEvaluationParameters();
  } else if (command == "--learn-progress") {
    Progress::LearnParameters();
  } else if (command == "--learn-probability") {
    MoveProbability::Learn();
  } else if (command == "--compute-ratings") {
    ComputePlayerRatings();
  } else {
    std::printf("CLI: No such command. %s\n", command.c_str());
  }
}

namespace {

/**
 * 探索のベンチマークを行います.
 */
void BenchmarkSearch() {
  // いわゆる「指し手生成祭り」局面
  Position pos = Position::FromSfen(
      "l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w RGgsn5p 1");
  Node node(pos);

  // ３０秒間の探索を行う
  UsiOptions usi_options;
  Thinking thinking(usi_options);
  UsiGoOptions go_options;
  go_options.byoyomi = 30000;
  thinking.Initialize();
  thinking.StartNewGame();
  thinking.StartThinking(node, go_options);
}

/**
 * 指し手生成のベンチマークを行います.
 * @param num_calls 指し手生成関数を呼び出す回数
 */
void BenchmarkMoveGeneration(const int num_calls) {
  std::printf("Start Move Generation Benchmark!\n\n");

  // 1. テスト局面を準備する
  // a. 初期局面
  Position startpos = Position::CreateStartPosition();
  // b. いわゆる「指し手生成祭り」局面
  Position festivalpos = Position::FromSfen(
      "l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w RGgsn5p 1");

  // 2. 各テスト局面について、ベンチマークテストを行います
  for (Position pos : {startpos, festivalpos}) {
    std::printf("Position=%s\n", pos.ToSfen().c_str());

    // タイマーをスタートさせる
    SimpleTimer timer;

    // 指定された回数だけ、指し手生成関数を呼び出す
    Array<ExtMove, Move::kMaxLegalMoves> stack;
    ExtMove* end = stack.begin();
    for (int i = 0; i < num_calls; ++i) {
      end = GenerateMoves<kNonEvasions>(pos, stack.begin());
    }
    double elapsed = std::max(timer.GetElapsedSeconds(), 0.001);

    // ベンチマークテストの結果を表示する
    std::printf("Iterations Finished.\n");
    std::printf("Iteration=%d, Time=%.3fsec, Speed=%.0ftimes/sec.\n",
                num_calls, elapsed, num_calls / elapsed);
    for (ExtMove* it = stack.begin(); it != end; ++it) {
      std::printf("%s ", it->move.ToSfen().c_str());
    }
    std::printf("\n\n");
  }
}

/**
 * １手詰関数のベンチマークテストを行うための、テスト局面集です.
 * テスト局面は、将棋ソフト「Blunder」（http://ak110.github.io/）と同じものを用いています.
 */
std::string g_checkmate_problems[] = {
    "4+R4/4n4/4S4/4k4/4p4/4NL3/9/9/8K b RBGSNLPb3g2sn2l16p 1",
    "4kp3/4g4/9/2N1N4/9/5L3/9/9/4+R3K b RBGSNLPb2g3sn2l16p 1",
    "4B3S/9/6+Rpk/8p/9/9/9/9/8K b RBGSNLP3g2s3n3l15p 1",
    "2S6/9/2kp+R3+R/9/9/2N6/9/9/8K b BGSNLPb3g2s2n3l16p 1",
    "4g2B+R/2Spk4/9/9/2N6/9/9/9/5L2K b RBGSNLP2g2s2n2l16p 1",
    "8S/9/6+Rpk/8p/9/9/9/9/8K b RBGSNLPb3g2s3n3l15p 1",
    "4g4/2Spk4/9/4B4/2N6/9/9/9/5L2K b RBGSNLPr2g2s2n2l16p 1",
    "4g4/1bSpk1S2/9/9/2N6/5L3/9/9/8K b 2rb3g2s3n3l17p 1",
    "4g4/3pk4/9/4B4/2N6/5L3/9/9/8K b RBGSNLPr2g3s2n2l16p 1",
    "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1",
    "l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w RGgsn5p 1",
};

/**
 * １手詰関数または３手詰関数のベンチマークテストを行います.
 * @param num_calls １手詰関数または３手詰関数を呼び出す回数
 * @param ply       「1」ならば１手詰関数のテストを、「3」ならば３手詰関数のテストを行います
 */
void BenchmarkMateSearch(const int num_calls, const int ply) {
  assert(ply == 1 || ply == 3);
  int position_id = 0;
  for (std::string sfen : g_checkmate_problems) {
    position_id += 1;
    Position pos = Position::FromSfen(sfen);
    Move mate_move = kMoveNone;
    std::printf("[%d] %s => ", position_id, sfen.c_str());

    // 実行時間を測定する
    SimpleTimer timer;
    if (ply == 1) {
      for (int j = 0; j < num_calls; j++) {
        IsMateInOnePly(pos, &mate_move);
      }
    } else if (ply == 3) {
      for (int j = 0; j < num_calls; j++) {
        Mate3Result m3result;
        IsMateInThreePlies(pos, &m3result);
      }
      Mate3Result m3result;
      if (IsMateInThreePlies(pos, &m3result)) {
        mate_move = m3result.mate_move;
      }
    }
    double elapsed = std::max(timer.GetElapsedSeconds(), 0.001);

    // 結果を表示する
    if (mate_move != kMoveNone) {
      std::printf("checkmate %s\n", mate_move.ToSfen().c_str());
    } else {
      std::printf("nomate\n");
    }
    std::printf("Iteration=%d, Time=%.3fsec, Speed=%.0fKcalls/sec.\n",
                num_calls, elapsed, (num_calls / elapsed) / 1000);
    std::printf("\n");
  }
}


// [2016/6/27 追加]
/**
 * ランダムプレーヤーにより１手詰関数のテストを行います.
 *
 * （参考文献）
 *   - やねうら王 on GitHub, https://github.com/yaneurao/YaneuraOu
 */
void TestMate1ByRandomPlayer() {

  // 最大5万局、最大256手
  const int MAX_GAMES = 50000;
  const int MAX_PLY = 256;

  // 乱数生成器
  // 同じ局面で繰り返しテストしたいので、seedは固定値にしておく
  std::mt19937_64 gen(0);

  // １手詰発見回数
  int cnt_mate_found = 0;
  // １手詰が発見できなかった回数
  int cnt_mate_missed = 0;
  // １手詰関数で詰みではない指し手が生成された回数
  int cnt_not_mate = 0;
  // １手詰関数で合法手ではない指し手が生成された回数
  int cnt_illegal = 0;

  // いずれかのカウンターがインクリメントされたか否かのフラグ
  bool flg_cntup = false;


  // 最大5万局
  for (int cnt_games = 0; cnt_games < MAX_GAMES; ++cnt_games) {
    // 初期局面
    Position pos = Position::CreateStartPosition();

    // 最大256手
    for (int ply = 0; ply < MAX_PLY; ++ply) {
      // 合法手を生成する
      SimpleMoveList<kAllMoves, true> legal_moves1(pos);

      // 合法な指し手がなかった == 詰み
      if (legal_moves1.size() == 0) {
        break;
      }

      // ----- １手詰めのテスト開始
      flg_cntup = false;

      // 王手のかかっていない局面においてテスト
      if (!pos.in_check()) {

        // １手詰関数を呼ぶ
        Move mate_move = kMoveNone;
        bool isMate1 = IsMateInOnePly(pos, &mate_move);

        // １手詰と判定された場合
        if (isMate1) {
          // １手詰の指し手が合法手ではない場合
          if (!pos.MoveIsLegal(mate_move)) {
            // １手詰関数で合法手ではない指し手が生成された
            ++cnt_illegal;
            flg_cntup = true;

            std::printf("１手詰関数で合法手ではない指し手が生成された：%s\n", mate_move.ToSfen().c_str());
            std::printf("sfen %s\n", pos.ToSfen().c_str());

            // 必要に応じてコメントアウトを外す
            //pos.Print(mate_move);
          }

          // １手詰の指し手が合法手の場合
          else {
            // これで本当に詰んでいるのかテストするため、局面を進める
            pos.MakeMove(mate_move);

            // 合法手を生成する
            SimpleMoveList<kAllMoves, true> legal_moves2(pos);

            // 合法手が存在する場合（詰みではない場合）
            if (legal_moves2.size() != 0) {
              // １手詰関数で詰みではない指し手が生成された
              ++cnt_not_mate;
              flg_cntup = true;

              // 局面を戻してから指し手を表示しないと目視で確認できない。
              pos.UnmakeMove(mate_move);

              std::printf("１手詰関数で詰みではない指し手が生成された：%s\n", mate_move.ToSfen().c_str());
              std::printf("sfen %s\n", pos.ToSfen().c_str());

              // 必要に応じてコメントアウトを外す
              //pos.Print(mate_move);
            }

            // 合法手が存在しない場合（詰みの場合）
            else {
              // １手詰発見
              ++cnt_mate_found;
              flg_cntup = true;

              // 局面を戻す
              pos.UnmakeMove(mate_move);
            }
          }
        }

        // １手詰と判定されなかった場合
        else {
          // １手詰め判定で漏れた局面を探す
          for (ExtMove ext_move : SimpleMoveList<kAllMoves, true>(pos)) {
            // 局面を進める
            Move move = ext_move.move;
            pos.MakeMove(move);

            // 合法手を生成する
            SimpleMoveList<kAllMoves, true> legal_moves2(pos);

            // 合法手が存在しない場合（詰みの場合）
            // ただし、打ち歩詰めを除く
            if (legal_moves2.size() == 0 && !move.is_pawn_drop()) {
              // １手詰が発見できなかった（１手詰め判定されなかったが実際には詰みがある）
              ++cnt_mate_missed;
              flg_cntup = true;

              // 局面を戻す
              pos.UnmakeMove(move);

              std::printf("１手詰め判定されなかったが実際には詰みがある：%s\n", move.ToSfen().c_str());
              std::printf("sfen %s\n", pos.ToSfen().c_str());

              // 必要に応じてコメントアウトを外す
              //pos.Print(move);

              // 次の指し手へ進む
              break;
            }

            // 局面を戻す
            pos.UnmakeMove(move);
          }

        }
      }

      // 統計情報の表示
      if (flg_cntup) {
        int cnt_test = cnt_mate_found + cnt_mate_missed + cnt_not_mate + cnt_illegal;
        if (cnt_test % 10000 == 0) {
          std::printf("-----\n");
          std::printf("試行回数：%d\n", cnt_test);
          std::printf("１手詰発見率：%.4f%%\n", 100.0f * cnt_mate_found / cnt_test);
          std::printf("１手詰発見回数：%d\n", cnt_mate_found);
          std::printf("１手詰が発見できなかった回数：%d\n", cnt_mate_missed);
          std::printf("１手詰関数で詰みではない指し手が生成された回数：%d\n", cnt_not_mate);
          std::printf("１手詰関数で合法手ではない指し手が生成された回数：%d\n", cnt_illegal);
          std::printf("-----\n");
        }
      }

      // ----- １手詰めのテスト終了

      // 生成された指し手のなかからランダムに選び、その指し手で局面を進める。
      std::uniform_int_distribution<uint64_t> dis(0, legal_moves1.size() - 1);
      Move move = legal_moves1.begin()[dis(gen)].move;

      pos.MakeMove(move);
    }
  }
}


/**
 * 定跡DBファイルを作成します.
 * @param output_file_name 定跡データの出力先のファイル名
 */
void CreateBook(const char* output_file_name) {
  // 棋譜DBから定跡データを作る
  Book book = Book::CreateBook();

  // 定跡手について探索を行って、評価値を付ける
  book.SearchAllBookMoves();

  // ファイルに書き出す
  book.WriteToFile(output_file_name);
}

/**
 * 棋譜DBファイルの統計データを計算して、画面に表示します.
 * @param event_name 統計データを取得する対象の棋戦名（例："名人戦"など）
 */
void ComputeStatsOfGameDatabase(const char* event_name) {
  // 1. 棋譜DBファイルを開く
  std::ifstream game_db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(game_db_file);

  // 2. 棋譜DBファイルを読み込む準備をする
  struct Stats {
    int win  = 0;
    int freq = 0;
  };
  typedef std::pair<std::string, Stats> Pair;
  std::unordered_map<std::string, Stats> openings_count;
  std::unordered_map<std::string, Stats> events_count;

  // 3. 棋譜DBから指し手を読み込む
  for (Game game; game_db.ReadOneGame(&game); ) {
    // 引き分けとなった対局をスキップする
    if (game.result == Game::kDraw) {
      continue;
    }
    // 解析対象外の棋戦をスキップする
    if (event_name != nullptr && game.event != event_name) {
      continue;
    }
    openings_count[game.opening].freq++;
    openings_count[game.opening].win += game.result == Game::kBlackWin;
    events_count[game.event].freq++;
  }

  // 4. 結果を表示する
  std::vector<Pair> openings(openings_count.begin(), openings_count.end());
  std::vector<Pair> events(events_count.begin(), events_count.end());
  std::sort(openings.begin(), openings.end(), [](const Pair& l, const Pair& r) {
    return l.second.freq > r.second.freq;
  });
  std::sort(events.begin(), events.end(), [](const Pair& l, const Pair& r) {
    return l.second.freq > r.second.freq;
  });
  int index = 0;
  for (Pair p : openings) {
    std::printf("%d, %s, %d, %d, %.1f%%\n",
                index++, p.first.c_str(), p.second.freq, p.second.win,
                100.0f * p.second.win / p.second.freq);
  }
  for (Pair p : events) {
    std::printf("%s, %d\n", p.first.c_str(), p.second.freq);
  }
}

/**
 * ありうるすべての「静かな手」を列挙する.
 *
 * ここでいう「静かな手」とは、以下の条件をすべてみたす手のことです。
 *   - 不成の手 または 銀が成る手 であること
 *   - 取る手ではないこと
 *   - 「常に損な手」（歩・角・飛が成れるのに成らない手と、２段目の香の不成）ではないこと
 *   - 反則の手ではないこと
 */
void ComputeAllPossibleQuietMoves() {
  std::vector<Move> moves;

  // 1. 不成の手
  for (Piece piece : Piece::all_pieces()) {
    const Color color = piece.color();
    Bitboard from_bb = Bitboard::board_bb();
    Bitboard target  = Bitboard::board_bb();

    // 常に損な手を除く
    switch (piece.type()) {
      case kBishop: from_bb &= rank_bb<4, 9>(color); break;
      case kRook  : from_bb &= rank_bb<4, 9>(color); break;
      default: break;
    }

    // 常に損な手 または 非合法手 を除く
    switch (piece.type()) {
      case kPawn  : target &= rank_bb<4, 9>(color); break;
      case kLance : target &= rank_bb<3, 9>(color); break;
      case kKnight: target &= rank_bb<3, 9>(color); break;
      case kBishop: target &= rank_bb<4, 9>(color); break;
      case kRook  : target &= rank_bb<4, 9>(color); break;
      default: break;
    }

    from_bb.ForEach([&](Square from) {
      Bitboard to_bb = max_attacks_bb(piece, from) & target;
      to_bb.ForEach([&](Square to) {
        moves.push_back(Move(piece, from, to));
      });
    });
  }

  // 2. 銀の不成（上記の1.と重複する手は除く）
  for (Color c : {kBlack, kWhite}) {
    for (Square from : Square::all_squares()) {
      Bitboard to_bb = max_attacks_bb(Piece(c, kSilver), from);
      to_bb.ForEach([&](Square to) {
        if (to.is_promotion_zone_of(c) || from.is_promotion_zone_of(c)) {
          moves.push_back(Move(c, kSilver, from, to, true));
        }
      });
    }
  }

  // 3. 打つ手
  for (Piece piece : Piece::all_pieces()) {
    // そもそも打つことができない駒の場合は、スキップする
    if (!piece.is_droppable()) {
      continue;
    }

    Color color = piece.color();
    Bitboard target = Bitboard::board_bb();

    // 非合法手を取り除く
    switch (piece.type()) {
      case kPawn  : target &= rank_bb<2, 9>(color); break;
      case kLance : target &= rank_bb<2, 9>(color); break;
      case kKnight: target &= rank_bb<3, 9>(color); break;
      default: break;
    }

    target.ForEach([&](Square to) {
      moves.push_back(Move(piece, to));
    });
  }

  // 指し手を整数値とみなして、昇順ソートする
  std::vector<uint32_t> values;
  for (Move move : moves) {
    values.push_back(move.ToUint32());
  }
  std::sort(values.begin(), values.end());

  // これまで計算してきた全ての「静かな手」を、８進数で表示する
  for (auto it = values.begin(); it != values.end(); ++it) {
    std::printf("%08x\n", *it);
  }
}

/**
 * 棋譜DBファイルに登場するプレイヤーのレーティングを計算します.
 */
void ComputePlayerRatings() {
  // 1. 棋譜DBファイルを開く
  std::ifstream game_db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(game_db_file);
  game_db.set_title_matches_only(true);

  // 2. 棋譜DBから全ての対局を読み込む
  std::vector<Game> games;
  for (Game game; game_db.ReadOneGame(&game);) {
    if (game.result != Game::kDraw) {
      games.push_back(game);
    }
  }
  std::sort(games.begin(), games.end(), [](const Game& l, const Game& r) {
    return l.date < r.date;
  });

  // 3. 全てのプレイヤーのレーティングを 1500 に初期化する
  std::unordered_map<std::string, int64_t> ratings;
  std::unordered_map<std::string, int64_t> sum_ratings;
  std::unordered_map<std::string, int64_t> num_played;
  for (const Game& game : games) {
    ratings[game.players[kBlack]] = 1500;
    ratings[game.players[kWhite]] = 1500;
    num_played[game.players[kBlack]] += 1;
    num_played[game.players[kWhite]] += 1;
  }

  // 4. 各プレーヤーのレーティングを計算する
  // なお、レーティングの計算式には、「将棋倶楽部２４」で使われているものを使用している。
  // 参考: https://ja.wikipedia.org/wiki/イロレーティング
  for (const Game& game : games) {
    Color winner = game.result == Game::kBlackWin ? kBlack : kWhite;
    int winner_rating = ratings[game.players[ winner]];
    int loser_rating  = ratings[game.players[~winner]];
    int delta = 16 + (loser_rating - winner_rating) / 25;
    delta = std::max(1, std::min(delta, 31));
    ratings[game.players[ winner]] += delta;
    ratings[game.players[~winner]] -= delta;
    sum_ratings[game.players[ winner]] += ratings[game.players[ winner]];
    sum_ratings[game.players[~winner]] += ratings[game.players[~winner]];
  }

  // 5. レーティングの平均を求める
  for (auto it = ratings.begin(); it != ratings.end(); ++it) {
    it->second = sum_ratings[it->first] / num_played[it->first];
  }

  // 6. 降順にソートする
  typedef std::pair<std::string, int64_t> Pair;
  std::vector<Pair> results(ratings.begin(), ratings.end());
  std::sort(results.begin(), results.end(), [](const Pair& l, const Pair& r) {
    return l.second > r.second;
  });

  // 7. 結果を表示する
  for (const Pair& pair : results) {
    std::printf("%s %" PRId64 "\n", pair.first.c_str(), pair.second);
  }
}

} // namespace

#endif // !defined(MINIMUM)
