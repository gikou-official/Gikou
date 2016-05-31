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

#ifndef NOTATIONS_H_
#define NOTATIONS_H_

#include "position.h"

/**
 * CSA表記の読み書きを行うためのクラスです.
 */
class Csa {
 public:
  static Position ParsePosition(const std::string&);
  static Move ParseMove(const std::string&, const Position&);
};

/**
 * KIF表記の読み書きを行うためのクラスです.
 * TODO 現在のところ未実装です
 */
class Kif {
 public:
  static Position ParsePosition(const std::string&);
  static Move ParseMove(const std::string&, const Position&);
};

#endif /* NOTATIONS_H_ */
