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

#ifndef TIME_CONTROL_H_
#define TIME_CONTROL_H_

#include <cinttypes>
#include "types.h"
#include "usi.h"

class Position;
class UsiGoOptions;
class UsiOptions;

/**
 * 時間管理を行うクラスの枠組みを定めた、基底クラスです.
 */
class TimeControl {
 public:

  /**
   * 時間管理を行うために必要な統計データです.
   */
  struct Stats {
    /**
     * 統計データをリセットします.
     */
    void Reset() {
      num_iterations_finished = 0;
      singular_margin = -kScoreInfinite;
      agreement_rate = -1.0;
      pv_instability = -1.0;
      search_insufficiency = -1.0;
    }

    /**
     * 終了したイテレーション数です.
     */
    int num_iterations_finished = 0;

    /**
     * 最善手の評価値と、次善手の評価値の差を表します.
     *
     * TODO 今後実装する
     *
     * 値域は、0 <= x <= 2 * kScoreInfinite です。
     * なお、負の値がセットされていると、この統計データは無視されます。
     */
    Score singular_margin = -kScoreInfinite;

    /**
     * 合議を行っているときに、指し手が一致しているワーカーの割合です（idea from 文殊）.
     *
     * agreement_rate = (指し手が一致しているワーカー数) / (全ワーカー数) と定義されます。
     *
     * 値域は、0.0 < x <= 1.0 です。
     * 例えば、4台の合議ワーカーがある場合には、
     * 　　2台の意見が一致したとき、agreement_rate = 0.5
     *    4台の意見が一致したとき、agreement_rate = 1.0
     * となります。
     *
     * なお、負の値がセットされていると、この統計データは無視されます。
     *
     * （参考文献）
     *   - 伊藤毅志: コンピュータ将棋における合議アルゴリズム, 『コンピュータ将棋の進歩６』,
     *     pp.100-101, 共立出版, 2012.
     */
    double agreement_rate = -1.0;

    /**
     * PVの不安定性を表します.
     *
     * TODO 今後実装する
     *
     * pv_instability = 1.0 + (最善手が変化した回数) と定義されます。
     * 値域は、1.0 <= x です。
     *
     * なお、負の値がセットされていると、この統計データは無視されます。
     */
    double pv_instability = -1.0;

    /**
     * 最善手以下の探索が、どの程度不十分なのかを表す指標です（idea from YSS）.
     *
     * search_insufficiency = (総探索ノード数) / (最善手以下の探索ノード数) と定義されます。
     * 値域は、1.0 <= x です。
     * 値が大きければ大きいほど、最善手以下の探索が不十分であることを示します。
     *
     * なお、負の値がセットされていると、この統計データは無視されます。
     *
     * （参考文献）
     *   - 山下宏: YSS--『コンピュータ将棋の進歩２』以降の改良点, 『コンピュータ将棋の進歩５』,
     *     pp.29-31, 共立出版, 2005.
     */
    double search_insufficiency = -1.0;
  };

  TimeControl(const Position& position, const UsiGoOptions& go_options,
              const UsiOptions& usi_options);
  virtual ~TimeControl() {}

  /**
   * 最小思考時間を返します.
   * 実際の実装は、子クラスで行ってください。
   */
  virtual int64_t minimum_time() const { return INT64_MAX; }

  /**
   * 最大思考時間を返します.
   * 実際の実装は、子クラスで行ってください。
   */
  virtual int64_t maximum_time() const { return INT64_MAX; }

  /**
   * 目標思考時間を返します.
   * 実際の実装は、子クラスで行ってください。
   */
  virtual int64_t target_time() const { return INT64_MAX; }

  /**
   * １手ごとに増加する時間を返します.
   * フィッシャールールであればincの値を、秒読みルールであればbyoyomiの値を返します。
   */
  int64_t time_per_move() const;

  /**
   * 手番側の残り時間を返します.
   */
  int64_t remaining_time() const;

  /** 時間管理に用いる統計データ */
  Stats stats;

 protected:
  const Position& position_;
  const UsiGoOptions& go_options_;
  const UsiOptions& usi_options_;
  int num_legal_moves_ = 0;
};

/**
 * １手の時間が固定されている場合の、時間管理を担当するクラスです.
 *
 * このクラスにおいては、
 *   minimum_time() == maximum_time() == target_time() ==　(固定された時間)
 * となります。
 */
class FixedTimeControl : public TimeControl {
 public:
  FixedTimeControl(const Position& position, const UsiGoOptions& go_options,
                   const UsiOptions& usi_options);
  int64_t minimum_time() const {
    return fixed_time_;
  }
  int64_t maximum_time() const {
    return fixed_time_;
  }
  int64_t target_time() const {
    return fixed_time_;
  }
 private:
  int64_t fixed_time_;
};

/**
 * 動的な時間管理を行うための、ベースクラスです.
 */
class DynamicTimeControl : public TimeControl {
 public:
  DynamicTimeControl(const Position& position, const UsiGoOptions& go_options,
                     const UsiOptions& usi_options);

  int64_t minimum_time() const {
    return minimum_time_;
  }

  int64_t maximum_time() const;

  int64_t target_time() const;

 protected:
  const int64_t byoyomi_margin_;
  int64_t base_time_;
  int64_t maximum_time_;
  int64_t minimum_time_;
};

/**
 * フィッシャールールの場合の、時間管理を担当するクラスです.
 */
class FischerTimeControl : public DynamicTimeControl {
 public:
  FischerTimeControl(const Position& position, const UsiGoOptions& go_options,
                     const UsiOptions& usi_options);
};

/**
 * 秒読みルールの場合の、時間管理を担当するクラスです.
 */
class ByoyomiTimeControl : public DynamicTimeControl {
 public:
  ByoyomiTimeControl(const Position& position, const UsiGoOptions& go_options,
                     const UsiOptions& usi_options);
};

/**
 * 切れ負けルールの場合の、時間管理を担当するクラスです.
 */
class SuddenDeathTimeControl : public DynamicTimeControl {
 public:
  SuddenDeathTimeControl(const Position& position, const UsiGoOptions& go_options,
                         const UsiOptions& usi_options);
};

#endif /* TIME_CONTROL_H_ */
