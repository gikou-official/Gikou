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

#include "gamedb.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include "notations.h"

#if !defined(MINIMUM)

namespace {
const std::unordered_set<std::string> g_title_matches = {
    "竜王戦", "名人戦", "王位戦", "王座戦", "棋王戦", "王将戦", "棋聖戦", "順位戦",
};
} // namespace

bool GameDatabase::ReadOneGame(Game* game) {
  assert(game != nullptr);

START:
  std::string line;

  // Step 1. ヘッダー部分を１行読み込む
  if (!std::getline(input_stream_, line)) {
    return false;
  }

  // Step 2. ヘッダー部分を解析する
  std::istringstream header_input(line);
  int game_length, game_id, game_result;
  header_input >> game_id >> game->date;
  header_input >> game->players[kBlack] >> game->players[kWhite];
  header_input >> game_result >> game_length >> game->event >> game->opening;
  game->result = static_cast<Game::Result>(game_result);

  // Step 3. 指し手が記録されている１行を読み込む
  if (!std::getline(input_stream_, line)) {
    return false;
  }

  // 指定があれば、７大棋戦＋順位戦以外の対局をスキップする
  if (title_matches_only_ && g_title_matches.count(game->event) == 0) {
    goto START;
  }

  // Step 4. 指し手をパースする
  std::istringstream moves_input(line);
  Position pos = Position::CreateStartPosition();
  game->moves.clear();
  for (int ply = 0; ply < game_length; ++ply) {
    std::string move_str;
    moves_input >> std::setw(6) >> move_str;
    if (!moves_input) {
      break; // 指し手を正しく読めなかった場合は、ここで中断する
    }
    Move move = Csa::ParseMove(move_str, pos);
    if (!pos.MoveIsLegal(move)) {
      break;
    }
    game->moves.push_back(move);
    pos.MakeMove(move);
  }

  return true;
}

#endif // !defined(MINIMUM)
