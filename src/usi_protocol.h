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

#ifndef USI_PROTOCOL_H_
#define USI_PROTOCOL_H_

#include "node.h"
#include "usi.h"

/**
 * USIによる探索条件を保持するクラスです.
 *
 * 各メンバ変数名は、USIプロトコルの"go"コマンドのオプション名と対応しています。
 */
struct UsiGoOptions {
  /** 先読みをするならばtrue */
  bool ponder = false;

  /** 先手・後手の持ち時間（ミリ秒） */
  ArrayMap<int64_t, Color> time{0, 0};

  /** 秒読み（ミリ秒） */
  int64_t byoyomi = 0;

  /** フィッシャークロックルールで使われる、１手ごとの加算時間（ミリ秒） */
  ArrayMap<int64_t, Color> inc{0, 0};

  /** 時間無制限に考えるならばtrue */
  bool infinite = false;

  /** 探索ノードの制限 */
  uint64_t nodes = 1152921504606846976ULL;

  /** 探索深度の制限 */
  int depth = kMaxPly - 1;

  /** 通常のαβ探索ではなく、詰み探索を行う場合はtrue */
  bool mate = false;

  /**
   * 探索を行う手を列挙された手に制限します（Universal Chess Interface (UCI)を参照）.
   *
   * ignoremovesと同時に指定することはできません。
   * このオプションを付加する場合は、全てのオプションの最後に配置してください。
   *
   * 使用例（７六歩・２六歩のみを時間無制限で探索する場合）：
   * <pre>
   * go infinite searchmoves 7g7f 2g2f
   * </pre>
   */
  std::vector<Move> searchmoves;

  /**
   * 指定された手をルート局面の探索から除外します（独自拡張）.
   *
   * searchmovesと同時に指定することはできません。
   * このオプションを付加する場合は、全てのオプションの最後に配置してください。
   *
   * 使用例（先手・後手の持ち時間各１０秒で、９八香以外を探索する場合）：
   * <pre>
   * go btime 10000 wtime 10000 ignoremoves 9i9h
   * </pre>
   */
  std::vector<Move> ignoremoves;
};

/**
 * USIエンジンが送信してくるinfoコマンドを保存するためのクラスです.
 *
 * 各メンバ変数名は、USIのinfoコマンドのサブコマンドに対応しています。
 */
struct UsiInfo {
  int depth = 0;
  int seldepth = 0;
  int64_t time = 0;
  int64_t nodes = 0;
  Score score = -kScoreInfinite;
  Bound bound = kBoundExact;
  std::string currmove;
  int hashfull = 0;
  int64_t nps = 0;
  int multipv = 0;
  std::vector<std::string> pv;
  std::string string;
  std::string ToString() const;
};

/**
 * USIプロトコルによる通信を担当するためのクラスです.
 *
 * UsiProtocolクラス自体は抽象クラスなので、具体的な実装は子クラスで行ってください。
 *
 * UsiProtocolクラスを使って実装されたクラスとしては、
 *   - Clusterクラス（疎結合並列探索のマスター）
 *   - Consultationクラス（合議アルゴリズムのマスター）
 * があります。
 */
class UsiProtocol {
 public:
  UsiProtocol(const char* program_mane, const char* author_name);
  virtual ~UsiProtocol() {}

  /**
   * USIプロトコルによる通信を開始します.
   */
  virtual void Start();

  /**
   * isreadyコマンドを受信した際の処理です.
   * 具体的な処理は、子クラスで実装してください。
   */
  virtual void OnIsreadyCommandEntered() {}

  /**
   * usinewgameコマンドを受信した際の処理です.
   * 具体的な処理は、子クラスで実装してください。
   */
  virtual void OnUsinewgameCommandEntered() {}

  /**
   * goコマンドを受信した際の処理です.
   * 具体的な処理は、子クラスで実装してください。
   */
  virtual void OnGoCommandEntered(const UsiGoOptions&) {}

  /**
   * stopコマンドを受信した際の処理です.
   * 具体的な処理は、子クラスで実装してください。
   */
  virtual void OnStopCommandEntered() {}

  /**
   * ponderhitコマンドを受信した際の処理です.
   * 具体的な処理は、子クラスで実装してください。
   */
  virtual void OnPonderhitCommandEntered() {}

  /**
   * quitコマンドを受信した際の処理です.
   * 具体的な処理は、子クラスで実装してください。
   */
  virtual void OnQuitCommandEntered() {}

  /**
   * gameoverコマンドを受信した際の処理です.
   * 具体的な処理は、子クラスで実装してください。
   */
  virtual void OnGameoverCommandEntered(const std::string&) {}

  const Node& root_node() const {
    return root_node_;
  }

  const UsiOptions& usi_options() const {
    return usi_options_;
  }

  /**
   * setoptionコマンドを解析します.
   */
  static void ParseSetoptionCommand(std::istringstream& is, UsiOptions* usi_options);

  /**
   * positionコマンドを解析します.
   */
  static void ParsePositionCommand(std::istringstream& is, Node* node);

  /**
   * goコマンドを解析します.
   */
  static UsiGoOptions ParseGoCommand(std::istringstream& is, const Position& pos);

  /**
   * infoコマンドを解析します.
   */
  static UsiInfo ParseInfoCommand(std::istringstream& is);

  /**
   * positionコマンドで送られてきたSFENです.
   */
  const std::string& position_sfen() const {
    return position_sfen_;
  }

 private:
  const char* const program_name_;
  const char* const author_name_;
  std::string position_sfen_ = "position startpos";
  Node root_node_ = Position::CreateStartPosition();
  UsiOptions usi_options_;
};

#endif /* USI_PROTOCOL_H_ */
