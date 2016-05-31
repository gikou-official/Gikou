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

#ifndef CLUSTER_H_
#define CLUSTER_H_

#if !defined(MINIMUM)

#include <memory>
#include <vector>
#include "process.h"
#include "task_thread.h"
#include "usi_protocol.h"

class Cluster;

/**
 * 疎結合並列探索のクラスタのワーカー部分です.
 *
 * 内部実装は、(1)外部プロセスを起動して、(2)USIでパイプ通信を行う、というシンプルなものです。
 *
 * （疎結合並列探索についての参考文献）
 *   - 金子知適, 田中哲朗: 最善手の予測に基づくゲーム木探索の分散並列実行,
 *     第15回ゲームプログラミングワークショップ, pp.126-133, 2010.
 *   - 山下宏: YSSの16台クラスタ探索について, http://www.yss-aya.com/csa0510.txt, 2014.
 */
class ClusterWorker : public TaskThread {
 public:
  ClusterWorker(size_t worker_id, Cluster& cluster);
  ~ClusterWorker();

  /**
   * 外部プロセス上にUSIエンジンを起動して、初期設定を行います.
   */
  void Initialize();

  /**
   * bestmoveコマンドを受信するまで、ワーカーから送られてくるコマンドを受信します.
   */
  void Run();

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

 private:
  const size_t worker_id_;
  Cluster& cluster_;
  std::mutex mutex_;
  Process external_process_;
};

/**
 * 疎結合並列探索のクラスタのマスター部分です.
 *
 * 現在の実装は、「ルート局面の上位N手に対し、各1台のワーカーに割り当てる」というシンプルなものです。
 *
 * （疎結合並列探索についての参考文献）
 *   - 金子知適, 田中哲朗: 最善手の予測に基づくゲーム木探索の分散並列実行,
 *     第15回ゲームプログラミングワークショップ, pp.126-133, 2010.
 *   - 山下宏: YSSの16台クラスタ探索について, http://www.yss-aya.com/csa0510.txt, 2014.
 */
class Cluster : public UsiProtocol {
 public:
  Cluster();
  ~Cluster() {}
  void OnIsreadyCommandEntered();
  void OnUsinewgameCommandEntered();
  void OnGoCommandEntered(const UsiGoOptions& options);
  void OnStopCommandEntered();
  void OnPonderhitCommandEntered();
  void OnQuitCommandEntered();
  void OnGameoverCommandEntered(const std::string& result);
  void UpdateInfo(int worker_id, const UsiInfo& worker_info);
  size_t master_worker_id() const {
    return num_workers_ - 1; // 最後のワーカーをマスターとして扱う
  }

 private:
  ClusterWorker& master_worker() {
    return *workers_.back();
  }

  void SendCommandToAllWorkers(const char* command) {
    for (std::unique_ptr<ClusterWorker>& worker : workers_) {
      worker->SendCommand(command);
    }
  }

  /** ワーカー数（現在は4で固定しています）. */
  size_t num_workers_ = 4;

  /** 別プロセスで動作しているワーカー. */
  std::vector<std::unique_ptr<ClusterWorker>> workers_;

  /** 各ワーカーから送られてきた最新のinfoコマンド. */
  std::vector<UsiInfo> worker_infos_;

  /** 最善手に関するinfoコマンド. */
  UsiInfo best_move_info_;

  /** 前回探索時に予想した、次回探索時のルート局面. */
  std::string predicted_position_;

  /** 前回探索時に予想した、次回探索時の最善手（予測があたった場合は、この手を優先して探索）. */
  std::string predicted_move_;

  /** 最善手を担当していたワーカエンジンのID番号. */
  size_t previous_best_worker_ = 0;

  /** 排他制御用 */
  std::mutex mutex_;
};

#endif /* !defined(MINIMUM) */
#endif /* CLUSTER_H_ */
