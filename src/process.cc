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

#if !defined(MINIMUM)

#include <signal.h>
#include <sys/wait.h>

#include "process.h"

bool Process::GetLine(std::string* const line) {
  line->clear();
  for (char c; (c = std::fgetc(stream_from_child_)) != EOF;) {
    // 改行コードが来たら終了する
    if (c == '\n') return true;
    // 末尾に一文字ずつ付け足していく
    line->push_back(c);
  }
  return false;
}

int Process::StartProcess(const char* const file, char* const argv[]) {
  const int kRead = 0, kWrite = 1;

  int pipe_from_child[2];
  int pipe_to_child[2];

  // パイプの作成（親プロセス->子プロセス）
  if (pipe(pipe_from_child) < 0) {
    std::perror("failed to create pipe_from_chlid.\n");
    return -1;
  }

  // パイプの作成（子プロセス->親プロセス）
  if (pipe(pipe_to_child) < 0) {
    std::perror("failed to create pipe_to_child.\n");
    close(pipe_from_child[kRead]);
    close(pipe_from_child[kWrite]);
    return -1;
  }

  // 子プロセスの作成
  pid_t process_id = fork();
  if (process_id < 0) {
    std::perror("fork() failed.\n");
    close(pipe_from_child[kRead]);
    close(pipe_from_child[kWrite]);
    close(pipe_to_child[kRead]);
    close(pipe_to_child[kWrite]);
    return -1;
  }

  // 子プロセスの場合、process_id はゼロとなる
  if (process_id == 0) {
    // 子プロセス側で使わないパイプを閉じる
    close(pipe_to_child[kWrite]);
    close(pipe_from_child[kRead]);

    // 親->子への出力を、標準入力に割り当てる
    dup2(pipe_to_child[kRead], STDIN_FILENO);

    // 子->親への入力を、標準出力に割り当てる
    dup2(pipe_from_child[kWrite], STDOUT_FILENO);

    // すでに標準入出力に割り当てたファイルディスクリプタを閉じる
    close(pipe_to_child[kRead]);
    close(pipe_from_child[kWrite]);

    // 子プロセスにおいて、子プログラムを起動する
    if (execvp(file, argv) < 0) {
      // プロセス起動時にエラーが発生した場合
      std::perror("execvp() failed\n");
      close(pipe_to_child[kRead]);
      close(pipe_from_child[kWrite]);
      return -1;
    }
  }

  // プロセスIDを記憶させる
  process_id_ = process_id;

  // パイプをファイルストリームとして開く
  stream_to_child_ = fdopen(pipe_to_child[kWrite], "w");
  stream_from_child_ = fdopen(pipe_from_child[kRead], "r");

  // バッファリングをオフにする
  // 注意：バッファリングをオフにしないと、外部プロセスとの通信が即時に行われない場合がある
  std::setvbuf(stream_to_child_, NULL, _IONBF, 0);
  std::setvbuf(stream_from_child_, NULL, _IONBF, 0);

  return process_id;
}

int Process::WaitFor() {
  // 子プロセスの終了を待つ
  int status;
  pid_t process_id = waitpid(process_id_, &status, 0);

  if (process_id < 0) {
    // 子プロセス終了時にエラーが発生した場合
    std::perror("waitpid\n");
    return -1;
  }

  if (WIFEXITED(status)) {
    // 子プロセスが正常に終了した場合
    return 0;
  } else {
    // 子プロセスが異常終了した場合
    return -1;
  }
}

#endif /* !defined(MINIMUM) */
