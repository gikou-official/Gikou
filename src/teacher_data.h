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

#ifndef TEACHER_DATA_H_
#define TEACHER_DATA_H_

#if !defined(MINIMUM)

#include <cstdio>
#include <vector>
#include "gamedb.h"
#include "huffman_code.h"
#include "move.h"

/**
 * 教師局面のデータです.
 */
struct TeacherPosition {
  /** ハフマン符号により圧縮された局面 */
  HuffmanCode huffman_code;

  /** その局面をルート局面として探索したときの最善手 */
  Move move;

  /** その局面をルート局面として探索したときの評価値 */
  Score score;
};

/**
 * 教師として用いるために予め探索を済ませた、PVのデータです.
 */
struct TeacherPv {
  /**
   * ファイルにPVデータを書き込みます.
   */
  bool ReadFromFile(std::FILE* stream);

  /**
   * ファイルからPVデータを読み込みます.
   */
  void WriteToFile(std::FILE* stream) const;

  /**
   * PVデータをstd::vectorの２次元ジャグ配列にして返します.
   */
  std::vector<std::vector<Move>> GetPvList() const;

  /** ハフマン符号により圧縮された局面 */
  HuffmanCode huffman_code;

  /** その局面の進行度（初期局面を0、投了局面を1とする連続値） */
  float progress;

  /** その対局の最終的な結果（先手勝ち・後手勝ち・引き分け） */
  Game::Result game_result;

  /** 指し手のPVデータ（指し手ごとにkMoveNoneで区切る） */
  std::vector<Move> pv_data;
};

/**
 * 教師データを作成するためのクラスです.
 */
class TeacherData {
 public:

  /**
   * 教師局面を生成します.
   */
  static void GenerateTeacherPositions();

  /**
   * 自己対戦の勝敗データを生成します.
   *
   * 具体的には、以下のようにして、教師データを生成します（idea from 激指）。
   *   1. 短い持ち時間の自己対戦を行い、自己対戦棋譜を生成する
   *   2. 各自己対戦棋譜からランダムに局面をサンプリングする
   *   3. サンプリングされた各局面を、ハフマン符号で圧縮して、バイナリデータ化する
   *
   * （参考文献）
   *   - 鶴岡慶雅, 横山大作, 丸山孝志, 高瀬亮, 大内拓実: 「激指」アピール文書（WCSC26）,
   *     http://www.computer-shogi.org/wcsc26/appeal/Gekisashi/appeal.txt, 2016.
   */
  static void GenerateTeacherGames();

  /**
   * 教師となるPVデータを生成します.
   */
  static void GenerateTeacherPvs();
};

#endif // !defined(MINIMUM)
#endif /* TEACHER_DATA_H_ */
