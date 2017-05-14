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

#ifndef SIGNALS_H_
#define SIGNALS_H_

#include <atomic>

/**
 * USI等と探索部との間で情報をやりとりするためのシグナルです.
 */
struct Signals {
  void Reset() {
    stop                 = false;
    ponderhit            = false;
    first_move_completed = false;
    limit_reached        = false;
  }

  /** USIのstopコマンドを受信した場合、true. */
  std::atomic_bool stop{false};

  /** USIのponderhitコマンドを受信した場合、true. */
  std::atomic_bool ponderhit{false};

  /** ルート局面において最初の１手を読み終えたら、true. */
  std::atomic_bool first_move_completed{false};

  /** 探索ノード数または探索深さの制限を超えたら、true. */
  std::atomic_bool limit_reached{false};
};

#endif /* SIGNALS_H_ */
