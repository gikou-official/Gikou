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

#include "book.h"

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_map>
#include <omp.h>
#include "common/progress_timer.h"
#include "gamedb.h"
#include "node.h"
#include "position.h"
#include "search.h"
#include "synced_printf.h"
#include "usi.h"

namespace {

// 定跡手を探索する最大深さ
const Depth kBookSearchDepth = 28 * kOnePly;

} // namespace

// 戦型の日本語名（タイトル戦や順位戦での出現数が多い順に並んでいます）
Array<std::string, 32> OpeningStrategy::japanese_names_ = {
    /* id_ =  0 */ "矢倉",
    /* id_ =  1 */ "四間飛車",
    /* id_ =  2 */ "中飛車",
    /* id_ =  3 */ "三間飛車",
    /* id_ =  4 */ "相掛かり",
    /* id_ =  5 */ "横歩取り",
    /* id_ =  6 */ "角換わり",
    /* id_ =  7 */ "向飛車",
    /* id_ =  8 */ "ひねり飛車",
    /* id_ =  9 */ "相振飛車",
    /* id_ = 10 */ "角交換腰掛銀",
    /* id_ = 11 */ "陽動振飛車",
    /* id_ = 12 */ "先手三間飛車",
    /* id_ = 13 */ "先手中飛車",
    /* id_ = 14 */ "角交換その他",
    /* id_ = 15 */ "右四間飛車",
    /* id_ = 16 */ "先手四間飛車",
    /* id_ = 17 */ "右玉",
    /* id_ = 18 */ "雁木",
    /* id_ = 19 */ "角換わり棒銀",
    /* id_ = 20 */ "筋違角",
    /* id_ = 21 */ "５筋位取り",
    /* id_ = 22 */ "先手向飛車",
    /* id_ = 23 */ "角換わり拒否",
    /* id_ = 24 */ "タテ歩取り",
    /* id_ = 25 */ "袖飛車",
    /* id_ = 26 */ "棒銀",
    /* id_ = 27 */ "三間飛車石田流",
    /* id_ = 28 */ "角換向飛車",
    /* id_ = 29 */ "ゴキゲン中飛車",
    /* id_ = 30 */ "風車",
    /* id_ = 31 */ "その他の戦型",
};

OpeningStrategy OpeningStrategy::of(const std::string& japanese_name) {
  for (size_t i = 0; i < japanese_names_.size(); ++i) {
    if (japanese_names_[i] == japanese_name) {
      return OpeningStrategy(i);
    }
  }
  return OpeningStrategy(31); // 一致する戦型がなければ、「その他の戦型」を返す
}

Move BookMoves::PickBest() const {
  int32_t best_importance = 0;
  Move best_move = kMoveNone;
  for (const BookMove& bm : *this) {
    if (bm.importance > best_importance) {
      best_importance = bm.importance;
      best_move = bm.move;
    }
  }
  return best_move;
}

Move BookMoves::PickRandom() const {
  struct CandidateMove {
    BookMove book_move;
    int64_t threshold;
  };

  // 1. 登録されている定跡手の中から、今回指す定跡手の候補をピックアップする
  int64_t sum_importance = 0;
  std::vector<CandidateMove> candidates;
  for (const BookMove& book_move : *this) {
    // 重要度がゼロ以下の手はスキップする
    if (book_move.importance <= 0) {
      continue;
    }
    // 重要度が正であれば、その定跡手を候補に加える
    sum_importance += book_move.importance;
    candidates.push_back({book_move, sum_importance});
  }

  // 2. 候補手が存在しない場合は、終了する
  if (candidates.size() == 0) {
    return kMoveNone;
  }

  // 3. 候補手が１手しか存在しない場合は、その手を返す
  if (candidates.size() == 1) {
    return candidates[0].book_move.move;
  }

  // 4. 候補手が複数存在する場合は、その重要度に応じて、ランダムに定跡手を選択する
#ifdef PSEUDO_RANDOM_DEVICE
  // random_deviceの代わりに、現在時刻を用いて乱数生成器を初期化する
  // （特定の環境では、std::random_deviceが非決定的乱数を返さない場合があるため）
  // 参考: http://en.cppreference.com/w/cpp/numeric/random/random_device
  static std::mt19937 gen(std::chrono::high_resolution_clock::now().time_since_epoch().count());
#else
  static std::random_device rd;
  static std::mt19937 gen(rd());
#endif
  std::uniform_int_distribution<int64_t> dis(0, sum_importance);
  int64_t threashold = dis(gen);
  for (const CandidateMove& candidate : candidates) {
    if (threashold <= candidate.threshold) {
      return candidate.book_move.move;
    }
  }

  assert(0);
  return candidates[0].book_move.move;
}

Key64 Book::ComputeKey(const Position& original_pos) const {
  Key64 key(0);
  Position pos = original_pos;

  // 後手番であれば、将棋盤を１８０度反転して、先手番として扱う
  if (pos.side_to_move() == kWhite) {
    pos.Flip();
  }

  // 盤上の駒
  for (Square s : Square::all_squares()) {
    key += hash_seeds_.psq[pos.piece_on(s)][s];
  }

  // 持ち駒
  for (Color c : {kBlack, kWhite})
    for (PieceType pt : Piece::all_hand_types())
      for (int n = pos.hand(c).count(pt); n > 0; --n) {
        key += hash_seeds_.hands[c][pt];
      }

  return key;
}

BookMoves Book::Probe(const Position& pos) const {
  assert(std::is_sorted(entries_.begin(), entries_.end()));

  // 1. 与えられた局面の定跡手を探す
  Entry key;
  key.key = ComputeKey(pos);
  auto range = std::equal_range(entries_.begin(), entries_.end(), key);

  // 2. 定跡手をBookMovesクラスに登録していく
  BookMoves book_moves;
  for (auto it = range.first; it != range.second; ++it) {
    BookMove bm;
    bm.move      = it->move;
    bm.frequency = it->frequency;
    bm.win_count = it->win_count;
    bm.opening   = it->opening;
    bm.score     = it->score == kScoreNone ? kScoreZero : it->score;

    // 後手番の局面の場合は、指し手及びその評価値を反転させる
    // この処理が必要なのは、定跡データベースの定跡手が、すべて先手番の手として登録されているため。
    if (pos.side_to_move() == kWhite) {
      bm.move.Flip();
      bm.score = -bm.score;
    }

    // 非合法手をスキップする
    // 本来なら合法手しかデータベースに登録されていないはずだが、局面のハッシュ値が衝突する可能性が
    // 一応あるため、このように合法手チェックをいれておいたほうが安全。
    if (!pos.MoveIsLegal(bm.move)) {
      assert(0);
      continue;
    }

    book_moves.push_back(bm);
  }

  return book_moves;
}

BookMoves Book::GetBookMoves(const Position& pos, const UsiOptions& usi_options) const {
  // 定跡DBから現局面の登録手を探す
  BookMoves book_moves = Probe(pos);

  // DBに手が登録されていない場合は、ここでおしまい
  if (book_moves.empty()) {
    return book_moves;
  }

  // 指し手の統計データを収集する
  Score best_book_score = -kScoreInfinite; // 定跡手の中での最高の評価値
  unsigned best_frequency = 0;   // 定跡手の中での最高の出現頻度
  double best_book_rate = 0.0;   // 定跡手の中でのレート（Bonanzaのbook.cを参照）
  double sum_win_count = 0; // 勝ち数の合計
  double sum_frequency = 0; // 出現数の合計
  for (BookMove bm : book_moves) {
    if (bm.score > best_book_score) {
      best_book_score = bm.score;
    }
    if (bm.frequency > best_frequency) {
      best_frequency = bm.frequency;
    }
    double rate = double(bm.win_count) / double(bm.frequency + 7);
    if (rate > best_book_rate) {
      best_book_rate = rate;
    }
    sum_win_count += double(bm.win_count);
    sum_frequency += double(bm.frequency);
  }

  // a. 定跡手の評価値のしきい値（手番側から見た評価値がこの値未満の定跡手は選択しない）
  int min_book_score = pos.side_to_move() == kBlack
                     ? usi_options["MinBookScoreForBlack"]
                     : usi_options["MinBookScoreForWhite"];

  // b. NarrowBook（出現頻度や勝率が低い定跡を除外する）
  bool narrow_book = usi_options["NarrowBook"];

  // c. TinyBook（勝ち数が少ない定跡を除外する）
  bool tiny_book = usi_options["TinyBook"];

  // この局面における平均的な勝率
  double average_win_rate = sum_win_count / std::max(sum_frequency, 1.0);

  // 定跡手の重要度に得点を付ける（この得点の大小により、定跡選択の確率が変わる）
  book_moves.DecideMoveImportances([&](BookMove bm) -> int32_t {
    // MinBookScoreオプション
    if (bm.score < min_book_score) {
      return -1; // この定跡手に負の得点を与えて、選択対象から除外する
    }

    // NarrowBookオプション
    if (narrow_book) {
      // レートが悪い手は除外する
      double rate = double(bm.win_count) / double(bm.frequency + 7);
      if (rate < best_book_rate * 0.85) {
        return -1; // この定跡手に負の得点を与えて、選択対象から除外する
      }
      // 勝率が悪い手を除外する
      double win_rate = double(bm.win_count) / double(std::max(bm.frequency, 1U));
      if (win_rate < average_win_rate * 0.85) {
        return -1; // この定跡手に負の得点を与えて、選択対象から除外する
      }
    }

    // TinyBookオプション（勝利数が小さすぎる手は除外する）
    if (tiny_book && bm.win_count < 15) {
      return -1; // この定跡手に負の得点を与えて、選択対象から除外する
    }

    return bm.win_count; // 勝った回数に比例して、選択確率が上がる
  });

  return book_moves;
}

Move Book::GetOneBookMove(const Position& pos, const UsiOptions& usi_options) const {
  // 定跡DBに登録されている手の中から、選別された手を取得する
  BookMoves book_moves = GetBookMoves(pos, usi_options);

  // DBに手が登録されていない場合は、ここでおしまい
  if (book_moves.empty()) {
    return kMoveNone;
  }

  // 定跡手の中からランダムに１手選ぶ
  const Move book_move = book_moves.PickRandom();

  // 定跡手を評価値が低い順にソートする（「将棋所」で、評価値の高い手を上に表示するため）
  std::sort(book_moves.begin(), book_moves.end(), [](const BookMove& lhs, const BookMove& rhs) {
    return lhs.score == rhs.score
         ? (lhs.importance < rhs.importance)
         : lhs.score < rhs.score;
  });

  // 定跡データについての情報を出力する
  int multipv = std::count_if(book_moves.begin(), book_moves.end(), [&](const BookMove& bm) {
    return bm.importance > 0;
  });
  int book_move_number = 0;
  for (const BookMove& bm : book_moves) {
    if (bm.importance > 0) {
      SYNCED_PRINTF("info time 0 depth %d nodes %d score cp %d multipv %d pv %s\n",
                    kBookSearchDepth / kOnePly,
                    int(bm.win_count),
                    int(bm.score),
                    multipv,
                    bm.move.ToSfen().c_str());
      if (bm.move == book_move) {
        book_move_number = multipv;
      }
      multipv--;
    }
  }

  // 定跡手を指す場合は、その定跡手についてのデータを出力する
  if (book_move != kMoveNone && book_move_number >= 1) {
    const BookMove& bm = *std::find_if(book_moves.begin(), book_moves.end(),
                                       [&](const BookMove& bm) {
      return bm.move == book_move;
    });
    SYNCED_PRINTF("info time 0 depth %d nodes %d score cp %d multipv %d pv %s\n",
                  kBookSearchDepth / kOnePly,
                  int(bm.win_count),
                  int(bm.score),
                  book_move_number,
                  book_move.ToSfen().c_str());
  }

  return book_move;
}

OpeningStrategySet Book::DetermineOpeningStrategy(const Position& pos) const {
  BookMoves bookmoves = Probe(pos);
  OpeningStrategySet opening_strategies;
  for (BookMove bookmove : bookmoves) {
    opening_strategies |= bookmove.opening;
  }
  return opening_strategies;
}

void Book::ReadFromFile(const char* file_name) {
  // 1. 定跡ファイルを開く
  std::FILE* file = std::fopen(file_name, "rb");
  if (file == NULL) {
    std::printf("info string Failed to Open %s.\n", file_name);
    return;
  }

  // 2. ハッシュ関数のシードを読み込む
  if (std::fread(&hash_seeds_, sizeof(hash_seeds_), 1, file) < 1) {
    std::printf("info string Failed to read the hash seeds of the book.\n");
    std::fclose(file);
    return;
  }

  // 3. 定跡手のエントリを読み込む
  entries_.clear();
  for (Entry buf; std::fread(&buf, sizeof(buf), 1, file);) {
    entries_.push_back(buf);
  }

  // 4. ファイルを閉じる
  std::fclose(file);
}

#if !defined(MINIMUM)

void Book::WriteToFile(const char* file_name) const {
  // 1. 保存先のファイルを開く
  std::FILE* file = std::fopen(file_name, "wb");
  if (file == NULL) {
    std::printf("info string Failed to Open %s.\n", file_name);
    return;
  }

  // 2. データを書き込む
  std::fwrite(&hash_seeds_, sizeof(hash_seeds_), 1, file);
  for (const Entry& entry : entries_) {
    std::fwrite(&entry, sizeof(entry), 1, file);
  }

  // 3. 保存先のファイルを閉じる
  std::fclose(file);
}

void Book::SearchAllBookMoves() {
  const int kMaxBookPly = 50; // 初手から最大50手まで定跡として登録する

  // 棋譜DBを準備する
  std::ifstream game_db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(game_db_file);
  game_db.set_title_matches_only(true);

  // 棋譜DBから棋譜を全て読み込む
  std::vector<Game> all_games;
  for (Game game; game_db.ReadOneGame(&game); ) {
    all_games.push_back(game);
  }

  // USIオプションを使い、得点を付加する対象の手を特定する
  UsiOptions usi_options;
  usi_options["NarrowBook"] = std::string("false");
  usi_options["TinyBook"] = std::string("false");

  // 進行状況を表示するためのタイマーを準備する
  ProgressTimer progress_timer(entries_.size());
  std::atomic_long num_searched_moves(0); // 探索して評価値をつけた指し手の数

  // 全ての棋譜について、順番に探索して評価値をつけていく
#pragma omp parallel for schedule(dynamic)
  for (size_t game_id = 0; game_id < all_games.size(); ++game_id) {
    const Game& game = all_games.at(game_id);

    // 各種データを初期化する
    Position pos = Position::CreateStartPosition();
    Node node(pos);
    SharedData shared_data;
    shared_data.hash_table.SetSize(512);
    Search search(shared_data);

    for (size_t ply = 0; ply < game.moves.size(); ++ply) {
      Move move = game.moves.at(ply);

      if (ply >= kMaxBookPly || !node.MoveIsLegal(move)) {
        break;
      }

      // 定跡DBに登録されている手を調べる
      Entry entry_as_key;
      entry_as_key.key = ComputeKey(node);
      auto range = std::equal_range(entries_.begin(), entries_.end(), entry_as_key);

      // 指し手が登録されていなければ、この局面はスキップして次へと進む
      if (range.first == range.second) {
        node.MakeMove(move);
        node.Evaluate(); // 評価値の差分計算に必要
        continue;
      }

      // 定跡のデータを取得する
      BookMoves book_moves = GetBookMoves(node, usi_options);

      // 定跡DBに登録されている手があれば、順に探索していく
      for (auto entry = range.first; entry != range.second; ++entry) {
        bool skip_this_move = false;

        // マルチスレッド処理するので、重複して探索しないように、排他制御を行う
        #pragma omp critical
        {
          if (entry->score != kScoreNone) {
            // 定跡手のscoreがkScoreNone以外になっていたら、既に探索済みということなのでスキップする
            skip_this_move = true;
          } else {
            // kScoreZeroに値を変更しておく。これにより、この手を他スレッドが重複して探索することはなくなる
            entry->score = kScoreZero;
          }
        }

        if (skip_this_move) {
          continue;
        }

        // 進行状況を表示する
        progress_timer.IncrementCounter();
        progress_timer.PrintProgress("num_searched_moves=%ld",
                                     num_searched_moves.load());

        // 定跡手をエントリから取り出す（後手番の手も、先手視点で保存されていることに注意）
        Move book_move = entry->move;
        if (node.side_to_move() == kWhite) {
          book_move = book_move.Flip();
        }

        if (!node.MoveIsLegal(book_move)) {
          continue;
        }

        // 定跡DBにおいてimportanceが負の手はスキップする
        auto iter = std::find_if(book_moves.begin(), book_moves.end(), [&](const BookMove& bm) {
          return bm.move == book_move;
        });
        if (iter != book_moves.end() && iter->importance < 0) {
          continue;
        }

        // 定跡手以下の探索を行う
        search.PrepareForNextSearch();
        shared_data.hash_table.Clear();
        node.MakeMove(book_move);
        Score inf = kScoreInfinite;
        Score score = -search.AlphaBetaSearch(node, -inf, inf, kBookSearchDepth);
        node.UnmakeMove(book_move);
        num_searched_moves += 1;

        // 評価値を保存する（上で排他制御済みなので、ここでは他スレッドと競合しないので排他制御不要）
        // 注意：後手の場合は、先手視点の得点に変換しておく
        entry->score = node.side_to_move() == kBlack ? score : -score;
      }

      // 次の局面に進む
      node.MakeMove(move);
      node.Evaluate(); // 評価値の差分計算に必要
    }
  }

  // 初期局面等の定跡手の評価値が何点になっているかを表示する
  {
    Position pos = Position::CreateStartPosition();

    // a. 初期局面
    std::printf("startpos\n");
    for (BookMove bm : Probe(pos)) {
      std::printf("%-5s score %4d win_rate %.4f (%5d/%5d)\n",
                  bm.move.ToSfen().c_str(),
                  (int)bm.score,
                  (double)bm.win_count / bm.frequency,
                  (int)bm.win_count,
                  (int)bm.frequency);
    }
    std::printf("startpos moves 7g7f\n");

    // b. 初期局面から７六歩と指した局面
    pos.MakeMove(Move(kBlackPawn, kSquare7G, kSquare7F));
    for (BookMove bm : Probe(pos)) {
      std::printf("%-5s score %4d win_rate %.4f (%5d/%5d)\n",
                  bm.move.ToSfen().c_str(),
                  (int)bm.score,
                  (double)bm.win_count / bm.frequency,
                  (int)bm.win_count,
                  (int)bm.frequency);
    }
  }
}

Book Book::CreateBook() {
  Book book;

  // 1. 局面のハッシュ値のシードを乱数生成器を用いて生成する
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis;
  for (Square sq : Square::all_squares()) {
    for (Piece p : Piece::all_pieces()) {
      book.hash_seeds_.psq[p][sq] = Key64(dis(gen));
    }
    book.hash_seeds_.psq[kNoPiece][sq] = Key64(0);
  }
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      book.hash_seeds_.hands[c][pt] = Key64(dis(gen));
    }
  }

  // 2. 対局データを読み込む準備をする
  const int kMaxBookPly = 50; // 初手から最大50手まで定跡として登録する
  std::ifstream game_db_file(GameDatabase::kDefaultDatabaseFile);
  GameDatabase game_db(game_db_file);
  game_db.set_title_matches_only(true);
  struct MapKey {
    bool operator==(const MapKey& rhs) const {
      return key == rhs.key && move == rhs.move;
    }
    Key64 key;
    Move move;
  };
  struct Hasher {
    size_t operator()(const MapKey& key) const {
      size_t pos_key = static_cast<size_t>(key.key);
      size_t move_key = std::hash<uint32_t>()(key.move.ToUint32());
      return pos_key ^ move_key;
    }
  };
  std::unordered_map<MapKey, Entry, Hasher> entries;
  int game_count = 0;
  const Position startpos = Position::CreateStartPosition();

  // 3. 対局データを読み込む
  for (Game game; game_db.ReadOneGame(&game); ) {
    // 引き分けとなった対局はスキップする
    if (game.result == Game::kDraw) {
      continue;
    }

    // 現在の進捗状況をプリントする
    if (++game_count % 1000 == 0) {
      std::printf("%d games finished.\n", game_count);
    }

    // 急戦定跡を除外する
    bool is_quick_attack_opening = false;
    for (size_t i = 0; i < 20 && i < game.moves.size(); ++i) {
      Move move = game.moves.at(i);
      // 1. 20手以内に成駒ができる定跡は、急戦定跡とみなす（ただし、角換わりの角交換は対象外）
      if (   move.is_promotion()
          && !(   game.opening == "角換わり"
               && move.piece_type() == kBishop
               && move.captured_piece_type() == kBishop)) {
        is_quick_attack_opening = true;
        break;
      }
      // 2. 20手以内に飛車交換をする手は、急戦定跡とみなす
      if (move.piece_type() == kRook && move.captured_piece_type() == kRook) {
        is_quick_attack_opening = true;
        break;
      }
    }
    if (is_quick_attack_opening) {
      continue;
    }

    int ply = 0;
    Position pos = startpos;
    Color winner = game.result == Game::kBlackWin ? kBlack : kWhite;
    OpeningStrategy opening_strategy = OpeningStrategy::of(game.opening);

    for (Move move : game.moves) {
      if (ply >= kMaxBookPly || !pos.MoveIsLegal(move)) {
        break;
      }

      // std::mapに用いるキーを作成する
      Key64 position_key = book.ComputeKey(pos);
      Move relative_move = move;
      if (pos.side_to_move() == kWhite) {
        relative_move.Flip();
      }
      MapKey map_key{position_key, relative_move};

      // 情報を保存する
      Entry& entry = entries[map_key];
      entry.key  = position_key;
      entry.move = relative_move;
      entry.frequency++;
      entry.win_count += (pos.side_to_move() == winner);
      entry.opening.set(opening_strategy);

      // 棋譜の手に沿って局面を進める
      pos.MakeMove(move);
    }
  }

  // 4. 登録する価値のある手のみ、データベースに登録する
  std::vector<Entry> temp;
  for (const auto& pair : entries) {
    temp.push_back(pair.second);
  }
  auto end = std::remove_if(temp.begin(), temp.end(), [](const Entry& e) {
    // 登録する条件１：その手を指した側が、２回以上勝っている
    // 登録する条件２：その手を指した側の勝率が、40%以上である
    return (e.win_count < 2) || (5 * e.win_count < 2 * e.frequency);
  });
  for (auto it = temp.begin(); it != end; ++it) {
    book.entries_.push_back(*it);
  }

  // 5. 後にProbe()メソッドを呼ぶ際に必要なので、予め局面のハッシュ値でソートしておく
  std::sort(book.entries_.begin(), book.entries_.end());

  // 6. 定跡データの作成結果を画面にプリントする
  std::printf("total games=%d\n", game_count);
  std::printf("total moves=%zu, registered=%zu\n",
              entries.size(), book.entries_.size());

  return book;
}

#endif // !defined(MINIMUM)
