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

#ifndef COMMON_BITOP_H_
#define COMMON_BITOP_H_

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace bitop
{
int bsf32(uint32_t);
int bsf64(uint64_t);
int bsr32(uint32_t);
int bsr64(uint64_t);
int popcnt32(uint32_t);
int popcnt64(uint64_t);
template<typename T> constexpr T reset_first_bit(T value);
template<typename T> constexpr bool more_than_one_bit(T value);
} // namespace bitop

inline int bitop::bsf32(uint32_t value) {
  assert(value != 0);
  return __builtin_ctz(value);
}

inline int bitop::bsf64(uint64_t value) {
  assert(value != 0);
  return __builtin_ctzll(value);
}

inline int bitop::bsr32(uint32_t value) {
  assert(value != 0);
  return 31 - __builtin_clz(value);
}

inline int bitop::bsr64(uint64_t value) {
  assert(value != 0);
  return 63 - __builtin_clzll(value);
}

inline int bitop::popcnt32(uint32_t value) {
  return __builtin_popcount(value);
}

inline int bitop::popcnt64(uint64_t value) {
  return __builtin_popcountll(value);
}

template<typename T>
constexpr T bitop::reset_first_bit(T value) {
  static_assert(std::is_signed<T>::value || std::is_unsigned<T>::value, "");
  return value & (value - T(1));
}

template<typename T>
constexpr bool bitop::more_than_one_bit(T value) {
  static_assert(std::is_signed<T>::value || std::is_unsigned<T>::value, "");
  return value & (value - T(1));
}

#endif /* COMMON_BITOP_H_ */
