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

#ifndef COMMON_MATH_H_
#define COMMON_MATH_H_

#include <cmath>

namespace math {

/**
 * 与えられた値の「符号」を返します.
 * 具体的には、与えられた値が正であれば+1を、負であれば-1を、それ以外であれば0を返します。
 */
template <typename T>
inline T sign(T x) {
  return x > T(0) ? T(1) : (x < T(0) ? T(-1) : T(0));
}

/**
 * シグモイド関数です.
 * 参考: https://ja.wikipedia.org/wiki/シグモイド関数
 */
inline double sigmoid(double x) {
  return 1.0 / (1.0 + std::exp(-x));
}

/**
 * シグモイド関数の導関数です.
 * 参考: https://ja.wikipedia.org/wiki/シグモイド関数
 */
inline double derivative_of_sigmoid(double x) {
  return sigmoid(x) * (1.0 - sigmoid(x));
}

} // namespace math

#endif /* COMMON_MATH_H_ */
