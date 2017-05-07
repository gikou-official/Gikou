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

#ifndef CONSULTATION_H_
#define CONSULTATION_H_

#if !defined(MINIMUM)

#include "process.h"
#include "task_thread.h"
#include "time_manager.h"
#include "usi_protocol.h"

class Consultation;

/**
 * 合議アルゴリズムのクラスタで用いられる、ワーカーです.
 *
 * 内部実装は、(1)外部プロセスを起動して、(2)USIでパイプ通信を行う、というシンプルなものです。
 *
 * （合議アルゴリズムについての参考文献）
 *   - 伊藤毅志: コンピュータ将棋における合議アルゴリズム, 『コンピュータ将棋の進歩６』,
 *     pp.85-103, 共立出版, 2012.
 */
class ConsultationWorker : public TaskThread {
 public:
  ConsultationWorker(int worker_id, Consultation& consultation);
  ~ConsultationWorker();

  void Initialize();
  void Run();
  void OnTaskFinished();

  /**
   * 外部プロセスのUSIエンジンに対し、コマンドを送信します.
   * @param format std::printf()関数と同様のフォーマット
   * @param args   フォーマットに従って出力したい引数
   */
  template<typename... Args>
  void SendCommand(const char* format, const Args&... args) {
    std::unique_lock<std::mutex> lock(mutex_);
    external_process_.Printf(format, args...);
    external_process_.Printf("\n");
  }

  /**
   * 外部プロセスのUSIエンジンから、コマンドを受信します.
   * @param line 受信したコマンドを保存するための変数
   * @return EOFを受信したらfalse。\nを受信したらtrue。
   */
  bool RecieveCommand(std::string* line) {
    return external_process_.GetLine(line);
  }

  /**
   * このワーカーのIDです.
   */
  int worker_id() const {
    return worker_id_;
  }

  bool is_alive() const {
    return alive_;
  }

  void set_alive(bool alive) {
    alive_ = alive;
  }

 private:
  /** ワーカーのID（ゼロ以上の整数）. */
  const int worker_id_;

  /** このワーカーとの通信が生きている場合は true、そうでなければ false */
  std::atomic_bool alive_{true};

  /** 合議アルゴリズムのマスターへの参照 */
  Consultation& consultation_;

  /** 排他制御用 */
  std::mutex mutex_;

  /** ワーカーエンジンを起動するのに用いる、外部プロセス */
  Process external_process_;
};

/**
 * 合議探索時の時間管理を担当するクラスです.
 */
class TimeManagerForConsultation : public TimeManager {
 public:
  TimeManagerForConsultation(const UsiOptions& usi_options_,
                             Consultation& consultation)
      : TimeManager(usi_options_),
        consultation_(consultation) {
  }
  void HandleTimeUpEvent();
 private:
  Consultation& consultation_;
};

/**
 * 複数のエンジンで合議を行うためのクラスです.
 *
 * 採用しているアルゴリズムは、
 *   1. 原則として、「多数決合議」を用いる
 *   2. ただし、投票数が同数の場合には「楽観合議」を行う
 * というものです。
 *
 * （合議アルゴリズムについての参考文献）
 *   - 伊藤毅志: コンピュータ将棋における合議アルゴリズム, 『コンピュータ将棋の進歩６』,
 *     pp.85-103, 共立出版, 2012.
 */
class Consultation : public UsiProtocol {
 public:
  Consultation();
  ~Consultation() {}
  void OnIsreadyCommandEntered();
  void OnUsinewgameCommandEntered();
  void OnGoCommandEntered(const UsiGoOptions& options);
  void OnStopCommandEntered();
  void OnPonderhitCommandEntered();
  void OnQuitCommandEntered();
  void OnGameoverCommandEntered(const std::string& result);

  /**
   * マスター側のinfoコマンドを最新の状態に更新し、必要であれば標準出力へ出力します.
   */
  void UpdateInfo();

  /**
   * ワーカーから送られてきたinfoコマンドを用いて、マスター側のinfoコマンドを最新の状態に更新し、
   * 必要であれば標準出力へ出力します.
   * @param worker_id   infoコマンドを送ってきたワーカーのID
   * @param worker_info ワーカーが送ってきたinfoコマンドの内容
   */
  void UpdateInfo(int worker_id, const UsiInfo& worker_info);

  /**
   * ワーカーが探索を終えたことを、マスター側に通知するためのメソッドです.
   * ワーカー側の探索が終わった時点で、このメソッドを呼んでください。
   */
  void NotifySearchIsFinished();

  /**
   * すべてのワーカーが探索を終えるまで待機します.
   *
   * ただし、一定時間（現在の実装では1000ミリ秒です）を経過してもワーカーからbestmoveコマンドが
   * 返ってこない場合には、マスターとワーカーとの間の通信が切れたとみなし、待機するのを終了します。
   */
  void WaitUntilWorkersFinishSearching();

  /**
   * マスターワーカーのIDを返します.
   *
   * マスターワーカーとは、「通常時は合議に参加しないが、他のすべてのワーカーとの接続が切れた場合に
   * 指し手を決める」ことを目的として、ローカルマシンで走らせておくワーカーのことです。
   */
  int master_worker_id() const {
    // マスターはワーカーとは別に用意するので、マスターのIDは、スレーブのIDの最大値に１を加えたものとする
    return num_workers_;
  }

 private:
  /**
   * コマンドをすべての合議ワーカーに送信します.
   * @param command 送信するコマンド
   */
  void SendCommandToAllWorkers(const char* command) {
    for (std::unique_ptr<ConsultationWorker>& worker : workers_) {
      worker->SendCommand(command);
    }
  }

  /**
   * USIのbestmoveコマンドを送信します.
   * @param command    送信するbestmoveコマンドの内容
   * @param go_options USIのgoコマンドで指定されていたオプション
   */
  void SendBestmoveCommand(std::string command, const UsiGoOptions& go_options);

  /** 合議アルゴリズムのワーカー */
  std::vector<std::unique_ptr<ConsultationWorker>> workers_;

  /** 各ワーカーから送られてきたinfoコマンドの内容 */
  std::vector<UsiInfo> worker_infos_;

  /** ワーカーから送られてくるinfoコマンドの処理を排他制御するためのmutex */
  std::mutex info_mutex_;

  /** 最善手のinfoコマンド */
  UsiInfo best_move_info_;

  /** 合議アルゴリズムのワーカー数 */
  size_t num_workers_ = 4;

  /** USIのbestmoveコマンド */
  std::string bestmove_command_;

  /** trueであれば、bestmoveコマンドを、後で（stop/ponderhitコマンド到着時）に送信する */
  bool send_bestmove_later_ = false;

  /** 合議アルゴリズム使用中に、時間管理を行うためのクラス */
  TimeManagerForConsultation time_manager_;

  /** ワーカーからのコマンドを待機する際の排他制御用 */
  std::mutex wait_mutex_;

  /** ワーカーからのコマンドを待機する際に用いる条件 */
  std::condition_variable wait_condition_;
};

#endif /* !defined(MINIMUM) */
#endif /* CONSULTATION_H_ */
