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

#ifndef GAMEDB_H_
#define GAMEDB_H_

#include <istream>
#include <string>
#include <vector>
#include "common/arraymap.h"
#include "move.h"

/**
 * 対局１局分のデータを保持するためのクラスです.
 */
struct Game {
  /**
   * 対局結果を表す値です.
   */
  enum Result {
    /** 引き分け */
    kDraw     = 0,

    /** 先手勝ち */
    kBlackWin = 1,

    /** 後手勝ち */
    kWhiteWin = 2,
  };

  /** 対局者 */
  ArrayMap<std::string, Color> players;

  /** 対局結果 */
  Result result;

  /** 対局日 */
  std::string date;

  /** 棋戦の名前 */
  std::string event;

  /** 戦型の名前 */
  std::string opening;

  /** 棋譜の指し手 */
  std::vector<Move> moves;
};

/**
 * 対局DBファイルから対局データを読み出すためのクラスです.
 *
 * 読み込みに対応している棋譜DBファイルの形式は、
 * <pre>
 * 1行目: <棋譜番号> <対局開始日> <先手名> <後手名> <勝敗(0:引き分け,1:先手勝ち,2:後手勝ち)> <手数> <棋戦> <戦型>
 * 2行目: <CSA形式の指し手(1手6文字)を一行に並べたもの>
 * </pre>
 * を対局の数だけ並べたものです。
 *
 * 日本語が含まれる棋譜ファイルの場合は、エンコーディングはUTF-8にしておいてください.
 */
class GameDatabase {
 public:
  /**
   * デフォルトで読み込む棋譜DBファイルの場所.
   */
  static constexpr const char* kDefaultDatabaseFile = "kifu_db.txt";

  /**
   * コンストラクタで、読み出しを行うDBファイルのストリームを指定してください.
   * なお、読み出すファイルは、utf-8でエンコーディングされている必要があります。
   */
  GameDatabase(std::istream& is)
      : input_stream_(is) {
  }

  /**
   * 対局１局分のデータを読み出します.
   * @param game 読み出した対局データの保存先のポインタ
   * @return まだDBに残りの対局がある場合はtrue
   */
  bool ReadOneGame(Game* game);

  /**
   * 読み出す対局データを、７大棋戦＋順位戦の対局に限定するか否かを設定します.
   * @param title_matches_only trueであれば、読み出す棋譜を７台棋戦＋順位戦の対局に限定する
   */
  void set_title_matches_only(bool title_matches_only) {
    title_matches_only_ = title_matches_only;
  }

 private:
  std::istream& input_stream_;
  bool title_matches_only_ = false;
};

#endif /* GAMEDB_H_ */
