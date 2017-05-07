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

#ifndef MOVE_PROBABILITY_H_
#define MOVE_PROBABILITY_H_

#include <memory>
#include <mutex>
#include <unordered_map>
#include <valarray>
#include <vector>
#include "move_feature.h"
class Position;

/**
 * 実現確率の途中計算結果をキャッシュするためのテーブルです.
 * このテーブルを用いることにより、探索の高速化が実現できます。
 */
class ProbabilityCacheTable {
 public:
  // 標準で確保する要素数（32768は、１スレッドなら30秒程度の探索で使い切る程度の大きさを想定。）
  static constexpr size_t kDefaultSize = 32768; // 2^15

  struct Entry {
    Key64 key;
    std::valarray<float> data;
    uint32_t num_lookups = 0;
    uint32_t age = 0;
  };

  typedef Array<Entry, 4> Bucket;

  /**
   * 標準である程度の要素数を確保します.
   */
  ProbabilityCacheTable() {
    SetSize(kDefaultSize);
  }

  /**
   * 参照します。
   * @param key 実現確率参照用のキー（ComputeKey()メソッドで計算してください。）
   * @return 要素へのポインタ。見つからない場合は、nullptrが返されます。
   */
  const Entry* LookUp(Key64 key) const;

  /**
   * 保存します.
   * @param key 実現確率参照用のキー（ComputeKey()メソッドで計算してください。）
   * @param data キャッシュ対象となるデータ
   */
  void Save(Key64 key, const std::valarray<float>& data);

  /**
   * テーブルの要素数をセットします.
   * このメソッドを呼ぶことにより、初めてメモリ領域が要素数分確保されます。
   */
  void SetSize(size_t size);

  /**
   * キャッシュテーブルのageを一つ増やします.
   */
  void SetToNextAge() {
    ++age_;
  }

  /**
   * テーブルをクリアします.
   */
  void Clear();

  /**
    * 特定のキーを読み書きする場合に、排他制御を行うためのロックをします.
    */
   void Lock(Key64 key) {
     mutexes_[key % mutexes_.size()].lock();
   }

   /**
    * 特定のキーを読み書きが終わった段階で、ロックを外します.
    */
   void Unlock(Key64 key) {
     mutexes_[key % mutexes_.size()].unlock();
   }

   /**
    * このテーブルに保存するときに用いるキーを計算します.
    * @param pos キーを計算したい局面
    * @return 局面ごとに一意に割り当てられたキー
    */
   static Key64 ComputeKey(const Position& pos);

 private:
  /** テーブルのポインタ */
  std::unique_ptr<Bucket[]> table_;

  /** テーブルの要素数 */
  size_t size_;

  /** キーから、テーブルのインデックスを求めるためのビットマスク */
  size_t key_mask_;

  /** テーブルに入っている情報の古さ */
  uint32_t age_;

  /** マルチスレッド時の排他制御用（１個だけだと手待ちが発生しやすいので、複数個用意してバラけさせる。） */
  Array<std::mutex, 32> mutexes_;
};

/**
 * 指し手が指される確率を計算するためのクラスです.
 */
struct MoveProbability {
  static const Depth kAppliedDepth = 8 * kOnePly;

  /**
   * 指し手が指される確率を計算します.
   */
  static std::unordered_map<uint32_t, float> ComputeProbabilities(
      const Position& pos, const HistoryStats& history, const GainsStats& gains,
      const HistoryStats* countermoves_history,
      const HistoryStats* followupmoves_history);

  /**
   * 指し手が指される確率を計算します（キャッシュ機能付き）.
   *
   * 一度確率を計算した局面であれば、キャッシュを用いて確率の計算が高速化されます。
   * なお、キャッシュを用いた場合であっても、探索情報については、最新のものが反映されます。
   */
  static std::valarray<double> ComputeProbabilitiesWithCache(
      const Position& pos, const HistoryStats& history, const GainsStats& gains,
      const HistoryStats* countermoves_history,
      const HistoryStats* followupmoves_history);

  /**
   * キャッシュテーブルのサイズ（要素数）を設定します.
   */
  static void SetCacheTableSize(size_t size) {
    cache_table_.SetSize(size);
  }

  /**
   * キャッシュテーブルのageを一つ増やします.
   */
  static void SetCacheTableToNextAge() {
    cache_table_.SetToNextAge();
  }

  /**
   * キャッシュテーブルをゼロクリアします.
   */
  static void ClearCacheTable() {
    cache_table_.Clear();
  }

  /**
   * 指し手が指される確率を棋譜から学習します.
   *
   * 現在の実装では、激指の方法（二値分類）を発展させて、多クラスロジスティック回帰により、
   * 実現確率を計算しています。
   *
   * （実現確率についての参考文献）
   *   - 鶴岡慶雅: 「激指」の最近の改良について --コンピュータ将棋と機械学習--,
   *     『コンピュータ将棋の進歩６』, pp.72-77, 共立出版, 2012.
   *
   * （多クラスロジスティック回帰についての参考文献）
   *   - C. M. ビショップ: 『パターン認識と機械学習・上』, pp.208-209, 丸善出版, 2012.
   */
  static void Learn();

  /**
   * 確率を計算するために必要なテーブルの初期化処理を行います.
   */
  static void Init();

 private:
  /**
   * 実現確率の途中データをキャッシュして高速化するためのテーブルです.
   */
  static ProbabilityCacheTable cache_table_;
};

#endif /* MOVE_PROBABILITY_H_ */
