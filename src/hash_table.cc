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

#include "hash_table.h"

#include "common/bitop.h"
#include "node.h"

void HashTable::SetSize(size_t megabytes) {
  size_t bytes = megabytes * 1024 * 1024;
  age_  = 0;
  size_ = (static_cast<size_t>(1) << bitop::bsr64(bytes)) / sizeof(Bucket);
  key_mask_ = size_ - 1;
  hashfull_ = 0;
  table_.reset(new Bucket[size_]);
  // テーブルのゼロ初期化を行う（省略不可）
  // Moveクラスのデフォルトコンストラクタにはゼロ初期化処理がないので、ここでゼロ初期化を行わないと、
  // ハッシュムーブがおかしな手になってしまい、最悪セグメンテーションフォールトを引き起こす。
  std::memset(table_.get(), 0, sizeof(Bucket) * size_);
}

HashEntry* HashTable::LookUp(Key64 key64) const {
  const Key32 key32 = key64.ToKey32();
  for (HashEntry& tte : table_[key64 & key_mask_]) {
    if (tte.key32() == key32) {
      tte.set_age(age_); // Refresh
      return &tte;
    }
  }
  return nullptr;
}

void HashTable::Save(Key64 key64, Move move, Score score, Depth depth,
                              Bound bound, Score eval, bool skip_mate3) {
  const Key32 key32 = key64.ToKey32();
  HashEntry::Flag flag = skip_mate3 ? HashEntry::kSkipMate3 : HashEntry::kFlagNone;

  // 1. 保存先を探す
  Bucket& bucket = table_[key64 & key_mask_];
  HashEntry* replace = bucket.begin();
  for (HashEntry& tte : bucket) {
    // a. 空きエントリや完全一致エントリが見つかった場合
    if (tte.empty() || tte.key32() == key32) {
      // すでにあるハッシュ手はそのまま残す
      if (move == kMoveNone) {
        move = tte.move();
      }

      // ３手詰みをスキップ可能であるとのフラグがすでに存在するときは、そのフラグをそのまま残す
      if (skip_mate3 == false) {
        flag = static_cast<HashEntry::Flag>(tte.flags_ & HashEntry::kSkipMate3);
      }

      replace = &tte;
      hashfull_ += tte.empty(); // エントリが空の場合は、ハッシュテーブルの使用率が上がる
      break;
    }

    // b. 置き換える場合
    if (  (tte.age() == age_ || tte.bound() == kBoundExact)
        - (replace->age() == age_)
        - (tte.depth() < replace->depth()) < 0) {
      replace = &tte;
    }
  }

  // 2. メモリに保存する
  replace->Save(key64, score, bound, depth, move, eval, flag, age_);
}

void HashTable::InsertMoves(const Node& root_node,
                            const std::vector<Move>& moves) {
  // local copy
  Node node = root_node;

  for (size_t i = 0; i < moves.size(); ++i) {
    // これから挿入しようとしている指し手が合法手か否かを念のためチェックする
    Move move = moves.at(i);
    if (!node.MoveIsLegal(move)) {
      assert(0);
      break;
    }

    // エントリを参照する
    const HashEntry* entry = LookUp(node.key());

    // エントリが消えてしまっているか、別のエントリに置き換わっている場合は、指し手を挿入する
    if (entry == nullptr || entry->move() != move) {
      Save(node.key(), move, kScoreNone, kDepthNone, kBoundNone, kScoreNone, false);
    }

    // 次の局面に移動する
    node.MakeMove(move);
  }
}

std::vector<Move> HashTable::ExtractMoves(const Node& root_node,
                                          const std::vector<Move>& moves) {
  std::vector<Move> result;

  // local copy
  Node node = root_node;

  // 指し手に沿って進める
  for (Move move : moves) {
    if (!move.is_real_move() || !node.MoveIsLegal(move)) {
      return result;
    }
    node.MakeMove(move);
  }

  // 局面数を進めながら、最善手を取得していく
  for (int ply = moves.size(); ply <= kMaxPly; ++ply) {
    // 千日手になっている場合は終了する
    if (ply >= 3) {
      Score repetition_score;
      if (node.DetectRepetition(&repetition_score)) {
        break;
      }
    }

    const HashEntry* entry = LookUp(node.key());

    // エントリが見つからなければ終了する
    if (entry == nullptr) {
      break;
    }

    Move move = entry->move();

    // 指し手が非合法手であれば終了する
    if (!move.is_real_move() || !node.MoveIsLegal(move)) {
      break;
    }

    result.push_back(move);
    node.MakeMove(move);
  }

  return result;
}

Move HashTable::GetPonderMove(const Node& root_node,
                              const Move best_move) const {
  assert(best_move.IsOk());

  // 一応最善手の合法手チェックを行う
  if (   !best_move.is_real_move()
      || !root_node.MoveIsLegal(best_move)) {
    assert(0);
    return kMoveNone;
  }

  // ルート局面から１手指した局面へと移動する
  Node node = root_node; // local copy
  node.MakeMove(best_move);

  // ハッシュテーブルを参照する
  const HashEntry* entry = LookUp(node.key());

  if (entry != nullptr) {
    Move ponder_move = entry->move();
    // 合法手チェックを通ったら、先読みの手を返す
    if (   ponder_move.is_real_move()
        && node.MoveIsLegal(ponder_move)) {
      return ponder_move;
    }
  }

  return kMoveNone;
}

void HashTable::Clear() {
  std::memset(table_.get(), 0, size_ * sizeof(Bucket));
  age_ = 0;
  hashfull_ = 0;
}
