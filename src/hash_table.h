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

#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_
#include <memory>
#include <vector>
#include "common/array.h"
#include "hash_entry.h"
class Node;

/**
 * 探索情報を保存するためのハッシュテーブル（トランスポジションテーブル）です.
 */
class HashTable {
 public:

  /**
   * ハッシュテーブルから、特定の局面の情報を参照します.
   * @param key64 情報を取得したい局面のハッシュ値（64ビット）
   * @return 局面に関する情報が記録されているポインタ
   */
  HashEntry* LookUp(Key64 key64) const;

  /**
   * 特定の局面に関する情報を保存する.
   */
  void Save(Key64 key64, Move move, Score score, Depth depth, Bound bound,
            Score eval, bool skip_mate3);

  /**
   * 指し手をハッシュテーブルに挿入します.
   */
  void InsertMoves(const Node& root_node, const std::vector<Move>& moves);

  /**
   * 指定された指し手以降の読み筋をハッシュテーブルから取得します.
   * @param root_node ルートノード
   * @param moves     ルートノードからの指し手
   * @return 指定された指し手（moves）以降の読み筋
   */
  std::vector<Move> ExtractMoves(const Node& root_node,
                                 const std::vector<Move>& moves);

  /**
   * ハッシュテーブルの中に残っている先読み用の予想手を取得します.
   */
  Move GetPonderMove(const Node& root_node, Move best_move) const;

  /**
   * 指定されたキーに対応するエントリのプリフェッチを行います.
   */
  void Prefetch(Key64 key) const {
    __builtin_prefetch(&table_[key & key_mask_]);
  }

  /**
   * 新規に探索を行う場合に呼んでください.
   */
  void NextAge() {
    ++age_;
  }

  /**
   * ハッシュテーブルに保存されている情報を物理的にクリアします.
   */
  void Clear();

  /**
   * ハッシュテーブルの大きさを変更します.
   * @param megabytes メモリ上に確保したいハッシュテーブルの大きさ（メガバイト単位で指定）
   */
  void SetSize(size_t megabytes);

  /**
   * ハッシュテーブルの使用率をパーミル（千分率）で返します.
   * USIのinfoコマンドのhashfullにそのまま使うと便利です。
   */
  int hashfull() const {
    return (UINT64_C(1000) * hashfull_) / (kBucketSize * size_);
  }

 private:
  /** バケツ１個あたりに保存する、エントリの数. */
  static constexpr size_t kBucketSize = 4;

  /**
   * エントリを保存するためのバケツです.
   * kBucketSizeは４なので、バケツ１個につき４個のエントリを保存できます。
   */
  typedef Array<HashEntry, kBucketSize> Bucket;

  /** ハッシュテーブルのポインタ */
  std::unique_ptr<Bucket[]> table_;

  /** ハッシュテーブルの要素数 */
  size_t size_;

  /** ハッシュキーから、テーブルのインデックスを求めるためのビットマスク */
  size_t key_mask_;

  /** 使用済みのエントリの数 */
  size_t hashfull_;

  /** ハッシュテーブルに入っている情報の古さ */
  uint8_t age_;
};

#endif /* HASH_TABLE_H_ */
