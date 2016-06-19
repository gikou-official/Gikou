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

#ifndef COMMON_OPERATORS_H_
#define COMMON_OPERATORS_H_

/**
 * 加算・減算に関する演算子を、一括でオーバーロードするためのマクロです.
 */
#define ENABLE_ADD_AND_SUBTRACT_OPERATORS(T)                           \
constexpr T operator-(T lhs) { return T(-int(lhs)); }                  \
constexpr T operator+(T lhs, T rhs) { return T(int(lhs) + int(rhs)); } \
constexpr T operator-(T lhs, T rhs) { return T(int(lhs) - int(rhs)); } \
constexpr T operator+(T lhs, int rhs) { return T(int(lhs) + rhs); }    \
constexpr T operator-(T lhs, int rhs) { return T(int(lhs) - rhs); }    \
constexpr T operator+(int lhs, T rhs) { return T(lhs + int(rhs)); }    \
constexpr T operator-(int lhs, T rhs) { return T(lhs - int(rhs)); }    \
inline T& operator+=(T& lhs, T rhs) { return lhs = lhs + rhs; }        \
inline T& operator-=(T& lhs, T rhs) { lhs = lhs - rhs; return lhs; }   \
inline T& operator+=(T& lhs, int rhs) { lhs = lhs + rhs; return lhs; } \
inline T& operator-=(T& lhs, int rhs) { lhs = lhs - rhs; return lhs; } \
inline T& operator++(T& lhs) { lhs = lhs + 1; return lhs; }            \
inline T& operator--(T& lhs) { lhs = lhs - 1; return lhs; }            \
inline T operator++(T& lhs, int) { T t = lhs; lhs += 1; return t; }    \
inline T operator--(T& lhs, int) { T t = lhs; lhs -= 1; return t; }

/**
 * 四則演算に関する演算子を、一括でオーバーロードするためのマクロです.
 */
#define ENABLE_ARITHMETIC_OPERATORS(T)                                 \
ENABLE_ADD_AND_SUBTRACT_OPERATORS(T)                                   \
constexpr T operator*(T lhs, int rhs) { return T(int(lhs) * rhs); }    \
constexpr T operator*(int lhs, T rhs) { return T(lhs * int(rhs)); }    \
constexpr T operator/(T lhs, int rhs) { return T(int(lhs) / rhs); }    \
inline T& operator*=(T& lhs, int rhs) { lhs = lhs * rhs; return lhs; } \
inline T& operator/=(T& lhs, int rhs) { lhs = lhs / rhs; return lhs; }

#endif /* COMMON_OPERATORS_H_ */
