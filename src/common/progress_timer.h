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

#ifndef COMMON_PROGRESS_TIMER_H_
#define COMMON_PROGRESS_TIMER_H_

#include <cstdint>
#include <cstdarg>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <string>
#include <mutex>

/**
 * タスクの進行状況と経過時間を表示するためのクラスです.
 *
 * ProgressTimerの使用例：
 * @code
 * // タイマーをスタートさせる
 * ProgressTimer timer(1000);
 *
 * // 何らかの時間がかかる処理....
 * int some_data = 234;
 *
 * // 進行状況を表示。２番目以降の引数には、printf()のフォーマットが使える。
 * timer.PrintProgress(621, "some_data = %d", some_data);
 * @endcode
 *
 * 標準出力（stdout）への出力例：
 * @code
 * [ 62%( 621/1000), 10:22+6:20] some_data = 234
 * @endcode
 *
 * 標準出力へ表示される情報は、左から順に、
 *   - 進行度（％表示）：（例）"62.1%"
 *   - 終了タスク数/予定タスク数：（例）"621/1000"
 *   - 経過時間：（例）"10:22" // 10分22秒という意味です。
 *   - 残り時間：（例）"6:20" // 6分22秒という意味です。あくまで予想なので、目安です。
 *   - 任意の文字列：（例）"some_data = 234"
 * となっています。
 */
class ProgressTimer {
 public:
  /**
   * このコンストラクタが呼ばれると同時に、内部のタイマーが経過時間の計測を自動的に開始します.
   * @param expected_count 予想されるタスクの数
   */
  explicit ProgressTimer(unsigned expected_count)
      : start_time_(std::chrono::steady_clock::now()),
        count_(0),
        expected_count_(expected_count),
        last_printed_width_(0) {
  }

  /**
   * 内部のカウンタを１増やし、タスクが１つ終わったことをProgressTimerに伝えます.
   * タスクが１つ終わるたびに呼んでください。
   * スレッドセーフ実装です。
   */
  void IncrementCounter() {
    // count_std::atomic_uint object.
    count_ += 1;
  }

  /**
   * 現在の進行状況を表示します.
   * 使用方法に付いては、クラスの冒頭のコメントを参照してください。
   * @param format std::printf()関数に渡す任意メッセージのフォーマット
   * @see ProgressTimer
   */
  void PrintProgress(const char* format, ...) {
    using std::chrono::duration_cast;
    using std::chrono::seconds;

    // 進行度を求める（進行度が0から1の範囲にする処理と、ゼロ除算防止の処理を付加している）
    unsigned count = std::max(std::min(count_.load(), expected_count_), 1U);
    double progress = count / static_cast<double>(std::max(expected_count_, 1U));

    // 経過時間・残り時間を求める
    auto end_time = std::chrono::steady_clock::now();
    int elapsed = duration_cast<seconds>(end_time - start_time_).count();
    double predicted_total_time = static_cast<double>(elapsed) / progress;
    int remaining = static_cast<int>(predicted_total_time - elapsed);

    // マルチスレッド環境に対応するため、排他制御を行う
    std::lock_guard<std::mutex> lock(mutex_);

    // ターミナル画面に残っている表示を消す（前回使った文字数分だけ、空白で埋める）
    const int num_spaces = std::max(last_printed_width_, 0);
    std::printf("%s\r", std::string(num_spaces, ' ').c_str());

    // 進行度・経過時間を表示する
    int digits = static_cast<int>(std::floor(std::log10(expected_count_)) + 1);
    std::string d("%" + std::to_string(digits) + "d");
    std::string progress_format = "[%3.0f%%(" + d + "/" + d + "), %d:%02d+%d:%02d] ";
    last_printed_width_ = std::printf(progress_format.c_str(),
                                      100.0 * progress, count, expected_count_,
                                      elapsed / 60, elapsed % 60,
                                      remaining / 60, remaining % 60);

    // 任意の文字列を表示する
    va_list args;
    va_start(args, format);
    last_printed_width_ += std::vprintf(format, args);
    va_end(args);

    // fflushを呼んで、表示遅延を防止する
    std::printf("\r");
    std::fflush(stdout);

    // 最後の表示の時だけは、改行コードを出力して新しい行へ移動する
    if (count_ == expected_count_) {
      std::printf("\n");
    }
  }

  /**
   * 経過時間を秒単位で返します.
   * @return 経過時間
   */
  int elapsed_seconds() {
    using std::chrono::duration_cast;
    using std::chrono::seconds;
    auto end_time = std::chrono::steady_clock::now();
    return duration_cast<seconds>(end_time - start_time_).count();
  }

 private:
  std::mutex mutex_;
  const std::chrono::time_point<std::chrono::steady_clock> start_time_;
  std::atomic_uint count_;
  const unsigned expected_count_;
  int last_printed_width_;
};

#endif /* COMMON_PROGRESS_TIMER_H_ */
