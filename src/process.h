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

#ifndef PROCESS_H_
#define PROCESS_H_

#if !defined(MINIMUM)

#include <cstdio>
#include <string>
#include <unistd.h>

/**
 * プロセス間通信を行うためのクラスです.
 * unistd.hヘッダを利用しているため、原則としてUNIX系OSでのみ使用可能です。
 */
class Process {
 public:
  /**
   * 外部プロセスを起動します.
   * @param file 外部プロセスのファイル名
   * @param argv 外部プロセスに渡す引数の配列（配列の最後の要素は必ずNULLにする。execvp()のマニュアル参照。）
   * @return 外部プロセスの起動に成功したときは、プロセスID。失敗したときは、-1。
   */
  int StartProcess(const char* file, char* const argv[]);

  /**
   * 外部プロセスの標準出力から１行読み込みます.
   * @param line 外部プロセスの標準出力から読み込んだ行
   * @return EOFまで読み込んだときは、false。まだ残りの行があるときは、true。
   */
  bool GetLine(std::string* line);

  /**
   * 外部プロセスの標準入力に対し、フォーマット指定して書き込みます.
   */
  template<typename... Args>
  void Printf(const char* format, const Args&... args) {
    std::fprintf(stream_to_child_, format, args...);
  }

  /**
   * 外部プロセスの標準入力に対し、１行書き込みます.
   */
  void PrintLine(const char* str) {
    std::fprintf(stream_to_child_, "%s\n", str);
  }

  /**
   * 外部プロセスが終了するまで待機します.
   * @return 正常終了した場合は0を、異常終了した場合は-1を返します。
   */
  int WaitFor();

  /**
   * 起動している外部プロセスのプロセスIDを返します.
   */
  pid_t process_id() const {
    return process_id_;
  }

 private:
  /** 外部プロセスのプロセスID */
  pid_t process_id_;

  /** 外部プロセスの標準入力につながれたストリーム（外部プロセスへの送信用） */
  std::FILE* stream_to_child_;

  /** 外部プロセスの標準出力につながれたストリーム（外部プロセスからの受信用） */
  std::FILE* stream_from_child_;
};

#endif /* !defined(MINIMUM) */
#endif /* PROCESS_H_ */
