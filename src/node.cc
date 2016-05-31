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

#include "node.h"

#include "progress.h"

void Node::Initialize() {
  // メモリの再確保による速度低下を防止する
  stack_.reserve(kMaxPly);

  // 後に(current-2)を参照するため、size() >= 3の必要がある
  assert(stack_.size() >= 3);
  auto current = stack_.end() - 1;

  // 現局面の評価値を保存
  current->psq_control_list = extended_board().GetPsqControlList();
  current->eval_detail = Evaluation::EvaluateAll(*this, psq_list_);
  current->eval_is_updated = true;

  // 現局面のハッシュキーを保存
  current->position_key = ComputePositionKey();

  // 千日手検出に用いる情報を保存
  current->board_key = ComputeBoardKey();
  current->hand      = stm_hand();
  current->continuous_checks = static_cast<int>(in_check());
  current->plies_from_null   = 0;
  assert((current-1)->plies_from_null == 0);
  assert((current-2)->continuous_checks == 0);
}

bool Node::DetectRepetition(Score* score) const {
  assert(score != nullptr);
  assert(stack_.size() >= 3); // (stack_.end()-3)を参照するため

  // 設定（何手まで遡って千日手の検出を行うか）
  const int kMaxDetectionPly = 32;

  auto current = stack_.end() - 1;
  assert(current->plies_from_null >= 0);
  const int end = std::min(current->plies_from_null, kMaxDetectionPly);

  for (int i = 4; i <= end; i += 2) {
    assert((current - i) >= stack_.begin());
    if (current->board_key != (current - i)->board_key) {
      continue;
    }
    const Hand h = (current - i)->hand;
    if (stm_hand() == h) {
      if (current->continuous_checks * 2 >= i) {
        // 相手側が連続王手の千日手により反則負け
        *score = -kScoreFoul;
        return true;
      } else if ((current-1)->continuous_checks * 2 >= i) {
        // 手番側が連続王手の千日手により反則負け
        *score = kScoreFoul;
        return true;
      } else {
        // 通常の千日手（引き分け）
        *score = kScoreDraw;
        return true;
      }
    } else if (stm_hand().Dominates(h)) {
      // 優越局面への変化
      *score = kScoreSuperior;
      return true;
    } else if (h.Dominates(stm_hand())) {
      // 劣等局面への変化
      *score = -kScoreSuperior;
      return true;
    }
  }

  return false;
}

Score Node::Evaluate(double* const progress) {
  auto current = stack_.end() - 1;

  // 必要に応じて評価値の差分計算を行う
  if (!current->eval_is_updated) {
    const auto previous = current - 1;
    current->psq_control_list = extended_board().GetPsqControlList();
    EvalDetail diff = Evaluation::EvaluateDifference(*this,
                                                     previous->eval_detail,
                                                     previous->psq_control_list,
                                                     current->psq_control_list,
                                                     &psq_list_);
    current->eval_detail = previous->eval_detail + diff;
    current->eval_is_updated = true;
  }

  Score score = current->eval_detail.ComputeFinalScore(side_to_move(), progress);

#ifndef NDEBUG
  // 双方の玉がある場合のみ、評価関数の差分計算結果のチェックを行う
  if (king_exists(kBlack) && king_exists(kWhite)) {
    // 評価値のチェック
    assert(score == Evaluation::Evaluate(*this));
    // 進行度のチェック
    if (progress != nullptr) {
      assert(*progress == Progress::EstimateProgress(*this));
    }
  }
#endif

  return score;
}

void Node::MakeMove(Move move, bool move_gives_check, Key64 key_after_move) {
  // 次のスタックに移行する
  stack_.emplace_back();
  auto current = stack_.end() - 1;

  // 新局面のハッシュキーをセットする
  current->position_key = key_after_move;

  // 新局面の盤上の駒のハッシュキーを計算する
  current->board_key = (current-1)->board_key;
  current->board_key += Zobrist::null_move(side_to_move());
  if (move.is_drop()) {
    current->board_key += Zobrist::psq(move.piece(), move.to());
  } else {
    current->board_key -= Zobrist::psq(move.captured_piece(), move.to());
    current->board_key -= Zobrist::psq(move.piece(), move.from());
    current->board_key += Zobrist::psq(move.piece_after_move(), move.to());
  }

  // 指し手を進める
  Position::MakeMove(move, move_gives_check);

  // 千日手関連の処理
  current->hand = stm_hand();
  if (in_check()) {
    current->continuous_checks = (current-2)->continuous_checks + 1;
  } else {
    current->continuous_checks = 0;
  }
  current->plies_from_null = (current-1)->plies_from_null + 1;

  // ハッシュキーが正しくセットされているかチェック
  assert(current->board_key == ComputeBoardKey());
  assert(current->position_key == ComputePositionKey());
}

void Node::UnmakeMove(Move move) {
  assert(move == last_move());

  // 評価値の差分計算が行われた場合には、その際にPsqListも更新されているので、元に戻す必要がある
  if (stack_.back().eval_is_updated) {
    psq_list_.UnmakeMove(last_move());
  }

  // 局面を元に戻す
  Position::UnmakeMove(move);

  // スタックを１つ前にもどす
  stack_.pop_back();
}

void Node::MakeNullMove() {
  // 次のスタックに移行する
  stack_.emplace_back();
  auto current = stack_.end() - 1;

  // 新局面のハッシュ値を計算する
  Key64 null_move_key = Zobrist::null_move(side_to_move());
  current->position_key = (current-1)->position_key + null_move_key;
  current->board_key    = (current-1)->board_key    + null_move_key;

  // 現在の評価関数の実装では、１手パスをしても評価値を再計算する必要はない
  current->psq_control_list = (current-1)->psq_control_list;
  current->eval_detail = (current-1)->eval_detail;
  current->eval_is_updated = true;

  // １手パスする
  Position::MakeNullMove();

  // 千日手関連の処理
  current->hand = stm_hand();
  assert(!in_check());
  current->continuous_checks = 0;
  current->plies_from_null = 0;

  // ハッシュキーが正しくセットされているかチェック
  assert(current->board_key == ComputeBoardKey());
  assert(current->position_key == ComputePositionKey());
}

void Node::UnmakeNullMove() {
  assert(last_move() == kMoveNull);
  Position::UnmakeNullMove();
  stack_.pop_back();
}

Key64 Node::ComputeKey(Move move) const {
  assert(move.IsOk());
  assert(move.is_real_move());
  assert(move.piece().color() == side_to_move());

  Piece piece = move.piece();
  Key64 result = Zobrist::null_move(piece.color());
  if (move.is_drop()) {
    result -= Zobrist::hand(piece);
    result += Zobrist::psq(piece, move.to());
  } else {
    result -= Zobrist::psq(piece, move.from());
    result += Zobrist::psq(move.piece_after_move(), move.to());
    Piece captured = move.captured_piece();
    result -= Zobrist::psq(captured, move.to());
    result += Zobrist::hand(captured.opponent_hand_piece());
  }

  return result;
}
