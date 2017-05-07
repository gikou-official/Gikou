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

#ifndef BOOK_H_
#define BOOK_H_

#include <string>
#include <vector>
#include "common/arraymap.h"
#include "common/bitset.h"
#include "move.h"
#include "types.h"
class GameDatabase;
class Node;
class Position;
class UsiOptions;

/**
 * 将棋の序盤の戦型を表すクラスです.
 */
class OpeningStrategy {
 public:
  /**
   * 対応している戦型の数
   */
  static constexpr int kNumStrategies = 9;

  explicit constexpr OpeningStrategy(int id = 0) : id_(id) {}

  /**
   * この戦型のIDを返します.
   * @return 戦型のID（0から8までの整数値）
   */
  operator int() const {
    return id_;
  }

  /**
   * この戦型のIDを返します.
   * @return 戦型のID（0から8までの整数値）
   */
  int id() const {
    return id_;
  }

  /**
   * この戦型の日本語名を返します.
   * @return 戦型の日本語名（例："矢倉"）
   */
  const std::string& japanese_name() const {
    return japanese_names_[id_];
  }

  /**
   * 戦型の日本語名に対応する、OpeningStrategyのオブジェクトを返します.
   * @param japanese_name 戦型の日本語名（"矢倉", "四間飛車"など、全9種に対応）
   * @return 指定した日本語名に対応するオブジェクト（該当する戦型が見つからないときは、"その他の戦型"）
   */
  static OpeningStrategy of(const std::string& japanese_name);

  /**
    * 戦型の日本語名に対応する、OpeningStrategyのオブジェクトを返します.
    * @param japanese_name 戦型の日本語名（"矢倉", "四間飛車"など、全9種に対応）
    * @return 指定した日本語名に対応するオブジェクト（該当する戦型が見つからないときは、"その他の戦型"）
    */
  static OpeningStrategy of(std::string&& japanese_name) {
    std::string temp(japanese_name);
    return of(temp);
  }

  /**
   * 全戦型が指定されたビットセット（OpeningStrategySet）を返します.
   * 範囲for文に渡すのに便利です。
   * @return 全戦型を要素とする擬似コンテナ
   */
  static BitSet<OpeningStrategy, kNumStrategies, uint32_t> all_strategies() {
    return BitSet<OpeningStrategy, kNumStrategies, uint32_t>().set();
  }

  static constexpr OpeningStrategy min() {
    return OpeningStrategy(0);
  }

  static constexpr OpeningStrategy max() {
    return OpeningStrategy(kNumStrategies - 1);
  }

 private:
  int id_;
  static Array<std::string, kNumStrategies> japanese_names_;
};

/**
 * 戦型のビットセットです.
 * １つの指し手が複数の戦型に属するということがありえるので、BitSetを使うのが便利です。
 */
typedef BitSet<OpeningStrategy, OpeningStrategy::kNumStrategies, uint32_t> OpeningStrategySet;

/**
 * 定跡手１手を表します.
 */
struct BookMove {
  bool operator<(const BookMove& rhs) const {
    return importance < rhs.importance;
  }

  bool operator>(const BookMove& rhs) const {
    return importance > rhs.importance;
  }

  /** 定跡の指し手. */
  Move move;

  /**
   * 定跡の重要度です.
   * BookMoves::PickRandom()関数は、この重要度に比例した確率で定跡手を選択する実装になっています。
   */
  int32_t importance;

  /** 定跡DB中に、この手が何回出現したかを表します. */
  uint32_t frequency;

  /** この指し手を指した場合に、手番側が勝った回数. */
  uint32_t win_count;

  /** この指し手を探索によって評価したときの評価値. */
  Score score;

  /**
   * この定跡手の戦型.
   * １つの指し手が複数の戦型に属するということがありえるので、BitSetを使って実装しています。
   */
  OpeningStrategySet opening;
};

/**
 * ある局面における複数の定跡手を格納するためのコンテナです.
 */
class BookMoves : public std::vector<BookMove> {
 public:
  /**
   * 指し手の重要度に得点を付与します.
   * @param scorer 指し手の重要度を評価し、得点化するための関数（ファンクタ）
   */
  template<typename T> void DecideMoveImportances(T scorer) {
    for (BookMove& bm : *this) {
      bm.importance = scorer(bm);
    }
  }

  /**
   * 登録されている定跡手の中で、最も重要度（importance）が高い指し手を返します.
   */
  Move PickBest() const;

  /**
   * 登録されている指し手の中から、重要度（importance）に応じて、指し手をランダムに返します.
   * 指し手が返される確率は、その指し手の重要度（importance）の値に比例しています。
   */
  Move PickRandom() const;
};

/**
 * 定跡データベースとして用いるクラスです.
 */
class Book {
 public:
  /**
   * 空の定跡データベースを作成します.
   */
  Book() {}

  /**
   * 予め作成しておいた、定跡データベースを開きます.
   */
  Book(const char* file_name) {
    ReadFromFile(file_name);
  }

  /**
   * 特定の局面における定跡手の一覧を取得します.
   * 取得される定跡手は、USIオプションを変えると変化します。
   * @param pos 定跡手の一覧を取得したい局面
   * @param usi_options USIオプション
   * @return 定跡手の一覧
   */
  BookMoves GetBookMoves(const Position& pos, const UsiOptions& usi_options) const;

  /**
   * 特定の局面における定跡手を、１手だけ取得します.
   * 取得される定跡手は、USIオプションに従い、ランダムに選択されます。
   * @param pos 定跡手を取得したい局面
   * @param usi_options USIオプション
   * @return ランダムに選択された定跡手
   */
  Move GetOneBookMove(const Position& pos, const UsiOptions& usi_options) const;

  /**
   * その局面における戦型を特定します.
   * @param pos 戦型を特定したい局面
   * @return その局面以降に、出現する可能性のある戦型
   */
  OpeningStrategySet DetermineOpeningStrategy(const Position& pos) const;

  /**
   * ファイルから定跡データを読み込みます.
   */
  void ReadFromFile(const char* file_name);

  /**
   * ファイルに定跡データを書き込みます.
   */
  void WriteToFile(const char* file_name) const;

  /**
   * 登録されている全ての定跡手についてミニマックス探索を行い、評価値を付与します.
   *
   * このように予め評価値を付与しておいて、定跡選択時には、評価値が一定以下の指し手を選択しないようにします。
   * こうすることで、自ら不利な定跡を選んでしまい、挽回不能になることを避けることができます。
   *
   * （参考文献）
   *   - 磯崎元洋: 技巧敗退の原因, やねうら王公式サイト,
   *     http://yaneuraou.yaneu.com/2015/11/27/技巧敗退の原因/, 2015.
   *   - 平岡拓也: Apery on GitHub, https://github.com/HiraokaTakuya/apery.
   */
  void SearchAllBookMoves();

  /**
   * 棋譜から定跡データベースを作成します.
   * @param opening_strategies 定跡データベースの作成時に、作成対象とする戦型を指定します
   * @return 作成された定跡データベース
   */
  static Book CreateBook(const OpeningStrategySet& opening_strategies);

 private:
  /**
   * 局面を一意に特定するための、ハッシュ値を計算します.
   * @param pos ハッシュ値を計算したい局面
   * @return その局面のハッシュ値
   */
  Key64 ComputeKey(const Position& pos) const;

  /**
   * 特定の局面における定跡手の一覧を、データベースから取得します.
   * @param pos 定跡手を取得したい局面
   * @return データベースに登録された、その局面における定跡手
   */
  BookMoves Probe(const Position& pos) const;

  /**
   * 局面のハッシュ値を計算するための乱数シードを保管しておくクラスです.
   */
  struct HashSeeds {
    ArrayMap<Key64, Piece, Square> psq;
    ArrayMap<Key64, Color, PieceType> hands;
  };

  /**
   * 定跡データベースに登録される、定跡手のエントリです.
   * 定跡手１手につき、１つのエントリを使います。
   */
  struct Entry {
    bool operator<(const Entry& rhs) const { return key < rhs.key; }
    Key64 key;
    Move move;
    uint32_t frequency = 0;
    uint32_t win_count = 0;
    OpeningStrategySet opening;
    Score score = kScoreNone;
  };

  HashSeeds hash_seeds_;
  std::vector<Entry> entries_;
};

#endif /* BOOK_H_ */
