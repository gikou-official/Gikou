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

#ifndef COMMON_LIMITS_H_
#define COMMON_LIMITS_H_

#include <type_traits>

/**
 * 特定の型やクラスについて、最小値・最大値を定義するためのクラスです.
 *
 * 予めLimitsクラスを用いて最小値・最大値を定義しておくと、その型の最小値・最大値を任意の場所から
 * 取得できるようになります。
 *
 * 例えば、ArrayMapクラスでは、Limitsクラスから得られる情報を利用して、
 * ArrayMapクラス内部で確保する配列のサイズを自動的に決定しています。
 *
 * @see ArrayMap
 */
template <typename T>
struct Limits {
  static_assert(std::is_class<T>::value, "");
  static_assert(std::is_same<decltype(T::min()), decltype(T::max())>::value, "");
  static_assert(std::is_constructible<T, decltype(T::min())>::value, "");
  static_assert(std::is_convertible<T, decltype(T::min())>::value, "");
  static_assert(T::min() < T::max(), "");
  static constexpr decltype(T::min()) min() {
    return T::min();
  }
  static constexpr decltype(T::max()) max() {
    return T::max();
  }
};

// bool型の最小値・最大値を参照するための特殊化です
template <>
struct Limits<bool> {
  static constexpr int min() { return 0; }
  static constexpr int max() { return 1; }
};

// enum型について、特殊化されたLimitsクラスを簡潔に定義するためのマクロです
#define DEFINE_SPECIALIZED_LIMITS_CLASS_FOR_ENUM(T, kMin, kMax) \
  static_assert(std::is_enum<T>::value, "T must be an enum."); \
  static_assert(kMin < kMax, "must be kMin < kMax."); \
  template <> \
  struct Limits<T> { \
    static constexpr T min() { return static_cast<T>(kMin); } \
    static constexpr T max() { return static_cast<T>(kMax); } \
  }; \
  static_assert(Limits<T>::min() < Limits<T>::max(), "must be min() < max()");

#endif /* COMMON_LIMITS_H_ */
