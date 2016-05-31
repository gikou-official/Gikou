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

#include "bitboard.h"
#include "cli.h"
#include "cluster.h"
#include "consultation.h"
#include "evaluation.h"
#include "extended_board.h"
#include "mate1ply.h"
#include "material.h"
#include "move_probability.h"
#include "progress.h"
#include "psq.h"
#include "search.h"
#include "square.h"
#include "usi.h"
#include "zobrist.h"

#ifdef UNIT_TEST
# include "gtest/gtest.h"
#endif

int main(int argc, char **argv) {
  // テーブル等の初期化を行う
  Square::Init();
  Bitboard::Init();
  ExtendedBoard::Init();
  Zobrist::Init();
  InitMateInOnePly();
  Search::Init();
  PsqPair::Init();
  Progress::ReadWeightsFromFile();
  Evaluation::Init();
  Material::Init();
  MoveProbability::Init();

#ifdef UNIT_TEST
  // Google Test によるユニットテストを行う
  std::printf("Running main() from main.cc\n");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
#endif

#ifdef CLUSTER
  // 疎結合並列探索のマスターを起動する
  Cluster cluster;
  cluster.Start();
  return 0;
#endif

#ifdef CONSULTATION
  // 合議アルゴリズムのマスターを起動する
  Consultation consultation;
  consultation.Start();
  return 0;
#endif

  if (argc <= 1) {
    // USIプロトコルを用いたエンジンを起動する
    Usi::Start();
  } else {
#ifndef MINIMUM
    // オプションを解析して、コマンドを実行
    Cli::ExecuteCommand(argc, argv);
#endif
  }

  return 0;
}
