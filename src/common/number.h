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

#ifndef COMMON_NUMBER_H_
#define COMMON_NUMBER_H_

#include <cassert>

/**
 * kMinからkMaxまでの値をとる整数を表します.
 *
 * 値域の範囲外の値（kMin未満の値またはkMaxより大きい値）をNumber型の変数に代入しようとすると、
 * assertマクロにひっかかるため、特定の範囲外の値をとりえない整数を扱いたい場合に便利です。
 *
 * 使用例：
 * @code
 * // int型とは相互に（暗黙の）型変換が可能です。
 * Number<0, 3> num = 2;
 * int n = num; // n == 2となります。
 *
 * // デバッグモードでは、値域の範囲外の数を代入しようとすると、assertマクロに引っかかります。
 * Number<0, 10> n = 30; // assertに引っかかる
 *
 * // Limitsクラスを利用して、最小値・最大値を取得できます。
 * int min = Limits<Number<0, 3>>::min(); // min == 0
 * int max = Limits<Number<0, 3>>::max(); // max == 3
 * @endcode
 */
template<int kMin, int kMax>
class Number {
 public:
  /**
   * デフォルトコンストラクタでは、int型と同様、初期化処理は行いません.
   * これは初期化にかかるコストをなくすためです。
   */
  Number() {}

  Number(int number)
      : number_(number) {
    assert(kMin <= number && number <= kMax);
  }

  Number& operator=(int number) {
    assert(kMin <= number && number <= kMax);
    number_ = number;
    return *this;
  }

  operator int() const {
    assert(kMin <= number_ && number_ <= kMax);
    return number_;
  }

  static constexpr int min() {
    return kMin;
  }

  static constexpr int max() {
    return kMax;
  }

 private:
  int number_;
};

#endif /* COMMON_NUMBER_H_ */
