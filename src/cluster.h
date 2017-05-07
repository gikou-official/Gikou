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

#ifndef CLUSTER_H_
#define CLUSTER_H_

#if !defined(MINIMUM)

#include <memory>
#include <vector>
#include "node.h"
#include "process.h"
#include "task_thread.h"
#include "usi_protocol.h"

class Cluster;

/**
 * ミニマックス木の各ノードを表したクラスです.
 *
 * 使い方:
 * 1. ルートノードを作っておきます。
 * 2. Split()を使って、適宜ミニマックス木を分割していきます。
 * 3. RegisterAllLeafNodes()を使って、全てのリーフノードを取得します。
 * 4. 得られたリーフノードに、ワーカーマシンを割り当てて探索します
 * 5. ワーカーマシンからUSIのinfoコマンドを受信したら、UpdateMinimaxTree()を使ってミニマックス木を更新します。
 * 6. 探索後、最終的に得られた最善手が、ルートノードのusi_info_.pvに格納されます。
 */
class MinimaxNode {
 public:
  /**
   * ミニマックス木を分割します.
   *
   * split_movesに指定された上位N手については、1手につき1ノードを割り当て、
   * その他の手については、まとめて1ノードを割り当てます。
   *
   * なお、すでにignoremoves_が設定されているノードは、それ以上分割できないことに注意してください。
   *
   * @param split_moves ここに指定された上位N手については、1手につき1ノードを割り当てます。
   */
  void Split(const std::vector<std::string>& split_moves);

  /**
   * すべてのリーフノードを、与えられたleaf_nodesに登録します.
   *
   * @param leaf_nodes リーフノードのポインタを登録するためのコンテナ
   */
  void RegisterAllLeafNodes(std::vector<MinimaxNode*>* leaf_nodes);

  /**
   * このリーフノードに対応する、USIのpositionコマンドを取得します.
   *
   * 各ワーカーには、このメソッドで得られたpositionコマンドをそのまま送信すればOKです。
   *
   * @return このリーフノードに対応する局面の、USIのpositionコマンド
   */
  std::string GetPositionCommand() const;

  /**
   * このリーフノードに対応する、USIのgoコマンドを取得します.
   *
   * 各ワーカーには、このメソッドで得られたgoコマンドをそのまま送信すればOKです。
   *
   * @return このリーフノードに対応する局面の、USIのgoコマンド
   */
  std::string GetGoCommand() const;

  /**
   * ミニマックス木を更新します.
   *
   * リーフノードでこのメソッドが呼ばれると、ミニマックス木を順に遡って行き、
   * 最終的にはルートの情報まで更新されます。
   */
  void UpdateMinimaxTree();

  /**
   * このノードの情報を、全てデフォルト値にリセットします.
   */
  void Reset();

  /**
   * child_idに対応した子ノードを返します.
   *
   * child_idが若い子ノードほど、ミニマックス木の左側のノードになっています。
   *
   * @param child_id 取得したい子ノードのID
   * @return 子ノードへの参照
   */
  MinimaxNode& GetChild(size_t child_id) {
    return *child_nodes_.at(child_id);
  }

  /**
   * このノードがリーフノードであれば、trueを返します.
   *
   * ここでいう「リーフノード」とは、探索用のワーカーが割り当てられている末端ノードのことです。
   * リーフノード以外のノードは、「内部ノード（interior node）」と呼んでいます。
   *
   * @return リーフノードのであれば、true。そうでなければ、false。
   */
  bool is_leaf_node() const {
    return child_nodes_.empty();
  }

  const UsiInfo& usi_info() const {
    return usi_info_;
  }

  void set_usi_info(const UsiInfo& usi_info) {
    assert(is_leaf_node());

    // 1. そのままコピーする
    usi_info_ = usi_info;

    // 2. ワーカーから受信したPVの前に、ルート局面からリーフノードまでの指し手を付け足す
    // （ルート局面から見たPVに統一するため）
    usi_info_.pv.insert(usi_info_.pv.begin(),
                        path_from_root_.begin(), path_from_root_.end());

    // 3. depthとseldepthをルート局面から見たものに補正する
    usi_info_.depth += path_from_root_.size();
    usi_info_.seldepth += path_from_root_.size();

    // 4. 詰みの場合に、scoreを、ルート局面から見たものに補正する
    if (   std::abs(usi_info.score) >= kScoreMateInMaxPly
        && std::abs(usi_info.score) <= kScoreMate) {
      if (usi_info.score > 0) {
        // 詰み（勝ち）: リーフノードがルートから離れているほど、勝ちまでの手数が伸び、評価値は下がる
        usi_info_.score -= path_from_root_.size();
      } else {
        // 詰み（負け）: リーフノードがルートから離れているほど、負けまでの手数が伸び、評価値は上がる
        usi_info_.score += path_from_root_.size();
      }
    }
  }

  void set_root_position_sfen(const std::string& sfen) {
    root_position_sfen_ = sfen;
  }

 private:
  /** 親ノードへのポインタ. なお、ルートノードの場合は、親ノードがないので、nullptrになります。 */
  MinimaxNode* parent_ = nullptr;

  /** 子ノードへのポインタ. 動的に確保する必要があるので、unique_ptrを使って実装しています。 */
  std::vector<std::unique_ptr<MinimaxNode>> child_nodes_;

  /** このノードの探索情報. USIのinfoコマンドと同じ形式で、情報を保持しています。 */
  UsiInfo usi_info_;

  /** ミニマックス木のルートノードの、局面のSFEN表記. 例えば、初期局面なら、「position startpos」です。 */
  std::string root_position_sfen_;

  /** ルート局面から、このノードまでの経路（指し手）. SFEN表記の指し手をvectorで格納しています。 */
  std::vector<std::string> path_from_root_;

  /** リーフノードのワーカーマシンに送る、ignoremovesオプション. SFEN表記の指し手を登録します。 */
  std::vector<std::string> ignoremoves_;
};

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
 * 現在の実装は、主にYSSのクラスタの設計を参考にしています。
 *
 * ＜現在のクラスタの仕組みの概要＞
 * 1. 短時間のマルチPV探索を行い、上位７手までをピックアップする。
 * 2. 最善手については、３台のマシンを割り当てて探索する。
 * 3. ２番目〜７番目に良い手については、各指し手ごとに２台ずつのマシンを割り当てて探索する。
 * 4. 残りの手（８番目以降の手）については、まとめて１台のマシンで探索する。
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

  /** ワーカー数（現在は16で固定しています）. */
  size_t num_workers_ = 16;

  /** 別プロセスで動作しているワーカー. */
  std::vector<std::unique_ptr<ClusterWorker>> workers_;

  /** 各ワーカーから送られてきた最新の情報（ミニマックス木のリーフノード情報として保持）. */
  std::vector<MinimaxNode*> leaf_nodes_;

  /** ミニマックス木のルートノード */
  MinimaxNode root_of_minimax_tree_;

  /** 排他制御用 */
  std::mutex mutex_;
};

#endif /* !defined(MINIMUM) */
#endif /* CLUSTER_H_ */
