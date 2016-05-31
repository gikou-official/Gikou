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

#ifndef COMMON_SIMPLE_TIMER_H_
#define COMMON_SIMPLE_TIMER_H_

#include <chrono>

/**
 * 時間計測を行うための、簡易なタイマーです.
 */
class SimpleTimer {
 public:
  /**
   * タイマーを作成し、開始時刻を記録します.
   */
  SimpleTimer()
      : start_time_(std::chrono::steady_clock::now()) {
  }

  /**
   * 経過時間を秒単位で取得します.
   */
  double GetElapsedSeconds() const {
    return GetElapsedMilliseconds() * 0.001;
  }

  /**
   * 経過時間をミリ秒単位で取得します.
   */
  double GetElapsedMilliseconds() const {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(end_time - start_time_);
    return static_cast<double>(elapsed.count());
  }

 private:
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
};

#endif /* COMMON_SIMPLE_TIMER_H_ */
