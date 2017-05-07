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

#ifndef MATE1PLY_H_
#define MATE1PLY_H_

class Move;
class Position;

/**
 * １手詰関数の実行に必要なテーブルを初期化します.
 */
void InitMateInOnePly();

/**
 * 与えられた局面を調べてみて、１手詰が存在すればtrueを返します.
 *
 * この関数は、以下の特性を持っていることに注意してください。
 *   - この関数がtrueを返した場合、１手詰が必ず存在する
 *   - しかし、この関数がfalseを返した場合であっても、１手詰が存在する可能性がある
 *
 * １手詰関数の実装上の技術については、以下の文献を参照してください。
 * （参考文献）
 *   - 金子知適, et al.: 新規節点で固定深さの探索を併用するdf-pnアルゴリズム,
 *     第10回ゲームプログラミングワークショップ, pp.1-8, 2005.
 *   - 保木邦仁: Bonanza - The Computer Shogi Program,
 *     http://www.geocities.jp/bonanza_shogi/.
 *
 * @param pos       １手詰の有無を調べたい局面
 * @param mate_move １手詰となる王手
 * @return １手詰が存在すれば、true
 */
bool IsMateInOnePly(const Position& pos, Move* mate_move);

#endif /* MATE1PLY_H_ */
