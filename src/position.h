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

#ifndef POSITION_H_
#define POSITION_H_

#include <string>
#include <vector>
#include "common/arraymap.h"
#include "bitboard.h"
#include "extended_board.h"
#include "hand.h"
#include "move.h"
#include "piece.h"
#include "square.h"

/**
 * 局面を表すクラスです.
 */
class Position {
 public:
  /**
   * デフォルトコンストラクタです.
   *
   * デフォルトコンストラクタで初期化した場合、以下のように初期化されます。
   *   - 将棋盤: 何も駒がない
   *   - 駒台: 何も駒がない
   *   - 手番: 先手
   */
  Position();

  /**
   * コピーコンストラクタです.
   * イテレータの付け替え処理を行う必要があるため、オーバーロードしています。
   */
  Position(Position&&);

  /**
   * コピーコンストラクタです.
   * イテレータの付け替え処理を行う必要があるため、オーバーロードしています。
   */
  Position(const Position&);

  /**
   * assign演算子のオーバーロードです.
   * イテレータの付け替え処理を行う必要があるため、オーバーロードしています。
   */
  Position& operator=(const Position& rhs);

  // 比較演算子のオーバーロード
  bool operator==(const Position& rhs) const;
  bool operator!=(const Position& rhs) const;

  // ビットボードをまとめてコピーするためのアクセサ
  const ArrayMap<Bitboard, Color>& color_bb() const;
  const ArrayMap<Bitboard, PieceType>& type_bb() const;

  /**
   * 盤上のすべての駒を返します.
   */
  Bitboard pieces() const;

  /**
   * 指定された手番の、すべての駒を返します.
   */
  Bitboard pieces(Color c) const;

  /**
   * 指定された駒の種類の、すべての駒を返します.
   */
  Bitboard pieces(Piece p) const;

  /**
   * 指定された駒の種類の、すべての駒を返します.
   */
  Bitboard pieces(PieceType pt) const;

  /**
   * 指定された駒の種類の、すべての駒を返します.
   *
   * 可変長引数テンプレートを使っているので、２個以上の駒を指定することができます。
   *
   * @code
   * Bitboard bb1 = pieces(kPawn) | pieces(kLance) | pieces(kKnight);
   * Bitboard bb2 = pieces(kPawn, kLance, kKnight);
   * // この場合、bb1 == bb2 となる
   * @endcode
   */
  template<typename ...Args>
  Bitboard pieces(PieceType pt1, Args... pt2) const;

  /**
   * 指定された駒の種類の、すべての駒を返します.
   *
   * 可変長引数テンプレートを使っているので、２個以上の駒を指定することができます。
   *
   * @code
   * Bitboard bb1 = pieces(kBlack) & (pieces(kPawn) | pieces(kLance));
   * Bitboard bb2 = pieces(kBlack, kPawn, kLance);
   * // この場合、bb1 == bb2 となる
   * @endcode
   */
  template<typename ...Args>
  Bitboard pieces(Color c, PieceType pt1, Args... pt2) const;

  /**
   * 金と各種成金の、すべての駒を返します.
   */
  Bitboard golds() const;

  /**
   * 指定された手番の、すべての金と成金を返します.
   */
  Bitboard golds(Color c) const;

  /**
   * 指定されたマスの上にある駒を返します.
   */
  Piece piece_on(Square s) const;

  /**
   * 指定された手番側の玉が存在する場合は、trueを返します.
   *
   * 指将棋では、どちらの手番も、常にtrueを返すことになりますが、
   * 片玉の詰将棋では、攻め方の玉は存在しないので、攻め方の手番ではfalseを返すことになります。
   */
  bool king_exists(Color c) const;

  /**
   * 手番側の玉がいるマスを返します.
   */
  Square stm_king_square() const;

  /**
   * 指定された手番側の玉がいるマスを返します.
   */
  Square king_square(Color c) const;

  /**
   * 指定された駒種の駒の枚数を返します.
   * @return 駒の枚数（盤上の駒も、駒台の駒も含んだ枚数）
   */
  int num_pieces(PieceType p) const;

  /**
   * 指定された手番・駒種の駒の枚数を返します.
   * @return 駒の枚数（盤上の駒も、駒台の駒も含んだ枚数）
   */
  int num_pieces(Color c, PieceType p) const;

  /**
   * 指定されたマスが、空きマス（駒がないマス）であれば、trueを返します.
   */
  bool is_empty(Square s) const;

  /**
   * 指定された手番側の歩が、指定された筋に存在する場合は、trueを返します.
   */
  bool pawn_on_file(File f, Color c) const;

  /**
   * 現局面の利き情報等を参照するためのクラスを、参照で返します.
   */
  const ExtendedBoard& extended_board() const;

  /**
   * 指定されたマスに、指定された手番側の利きがついていれば、trueを返します.
   */
  bool square_is_attacked(Color c, Square s) const;

  /**
   * 指定されたマスに付けられた、指定された手番側の利き数を返します.
   */
  int num_controls(Color c, Square s) const;

  /**
   * 指定されたマスに付けられた、指定された手番側の長い利きの方向を返します.
   */
  DirectionSet long_controls(Color c, Square s) const;

  /**
   * 手番側の持ち駒を返します.
   */
  Hand stm_hand() const;

  /**
   * 指定された手番側の持ち駒を返します.
   */
  Hand hand(Color c) const;

  /**
   * 現局面における手番を返します.
   */
  Color side_to_move() const;

  /**
   * 現局面の手番をセットします.
   */
  void set_side_to_move(Color c);

  /**
   * 指定されたマスにいる駒が、利きをつけているマスを求めます.
   */
  template<Color kC, PieceType kPt> Bitboard AttacksFrom(Square s) const;

  /**
   * 指定されたマスに利きを付けている駒を求めます.
   */
  template<Color kC, PieceType kPt> Bitboard AttackersTo(Square s) const;

  /**
   * 指定されたマスに利きを付けている駒を求めます.
   */
  template<Color kC> Bitboard AttackersTo(Square s , Bitboard occ) const;

  /**
   * 指定されたマスに利きを付けている駒を求めます.
   */
  Bitboard AttackersTo(Square s, Bitboard occ) const;

  /**
   * 指定されたマスに利きを付けている駒を求めます.
   */
  Bitboard AttackersTo(Square s, Bitboard occ, Color c) const;

  /**
   * 指定されたマスに利きを付けている飛び駒を求めます.
   */
  Bitboard SlidersAttackingTo(Square s, Bitboard occ) const;

  /**
   * 指定されたマスに利きを付けている飛び駒を求めます.
   */
  Bitboard SlidersAttackingTo(Square s, Bitboard occ, Color c) const;

  /**
   * 現局面において、王手がかかっている場合は、trueを返します.
   */
  bool in_check() const;

  /**
   * 現局面において、王手をかけている駒の枚数を返します.
   * なお、王手がかかっていない場合は、0を返します.
   */
  int num_checkers() const;

  /**
   * 現局面において、王手をかけている駒をすべて返します.
   */
  Bitboard checkers() const;

  /**
   * 現局面において、ピンされている駒をすべて返します.
   */
  Bitboard pinned_pieces() const;

  /**
   * 現局面において、動かせば「開き王手」になる可能性がある駒の候補をすべて返します.
   */
  Bitboard discovered_check_candidates() const;

  /**
   * 現局面において、王手をかけている駒をすべて求めます.
   */
  Bitboard ComputeCheckers() const;

  /**
   * 現局面において、ピンされている駒をすべて求めます.
   */
  Bitboard ComputePinnedPieces() const;

  /**
   * 現局面において、動かせば「開き王手」になる可能性がある駒の候補をすべて求めます.
   */
  Bitboard ComputeDiscoveredCheckCandidates() const;

  /**
   * 直前に指した手を返します.
   */
  Move last_move() const;

  /**
   * n手前に指した手を返します.
   */
  Move move_before_n_ply(unsigned ply) const;

  /**
   * 開始局面から現局面までの手数を返します.
   *
   * 開始局面の場合は、まだ何も手を指していないので、ゼロを返します.
   */
  int game_ply() const;

  /**
   * 指し手が合法手であれば、trueを返します.
   */
  bool MoveIsLegal(Move move) const;

  /**
   * 指し手が擬似合法手であれば、trueを返します.
   * キラー手などのチェックに使います.
   */
  bool MoveIsPseudoLegal(Move move) const;

  /**
   * 指定された擬似合法手が、合法手であれば、trueを返します.
   * MoveIsPseudoLegal()のチェックを省略しているため、MoveIsLegal()よりも高速に動作します。
   */
  bool PseudoLegalMoveIsLegal(Move move) const;

  /**
   * 指定された駒打以外の手が、合法手であれば、trueを返します.
   * @param move 駒打以外の手
   * @return 合法手であれば、true
   */
  bool NonDropMoveIsLegal(Move move) const;

  /**
   * 指定された手が王手になっている場合は、trueを返します.
   */
  bool MoveGivesCheck(Move move) const;

  /**
   * 指定された指し手に沿って局面を進めます.
   */
  void MakeMove(Move move);

  /**
   * 指定された指し手に沿って局面を進めます.
   */
  void MakeMove(Move move, bool move_gives_check);

  /**
   * １手前の局面に戻します.
   */
  void UnmakeMove(Move move);

  /**
   * １手「パス」をしたことにして、局面を進めます.
   * 将棋にパスは本来存在しませんが、null move pruningなどで利用されます。
   */
  void MakeNullMove();

  /**
   * 局面をパスをする前の局面に戻します.
   */
  void UnmakeNullMove();

  /**
   * 「玉の８近傍への駒打」と「その打った駒を玉で取る手」の２手を指したことにして、局面を進めます.
   * ３手詰関数（mate3.cpp）を高速化するためのメソッドです。
   */
  void MakeDropAndKingRecapture(Move move);   // @see mate3.cpp

  /**
   * 「玉の８近傍への駒打」と「その打った駒を玉で取る手」を指す前の、２手前の局面に戻します.
   * ３手詰関数（mate3.cpp）を高速化するためのメソッドです。
   */
  void UnmakeDropAndKingRecapture(Move move); // @see mate3.cpp

  /**
   * 指定されたマスに、指定された駒を配置します.
   */
  void PutPiece(Piece p, Square s);

  /**
   * 指定されたマスから駒を取り除きます.
   */
  Piece RemovePiece(Square s);

  /**
   * 指定された手番の持ち駒に、指定された種類の駒を、１枚加えます.
   */
  void AddOneToHand(Color c, PieceType pt);

  /**
   * 現局面において、宣言勝ちができる場合は、trueを返します.
   * @param is_csa_rule trueであればCSAルールを、falseであれば24点法を適用する
   * @return 宣言勝ちができる場合は、true
   */
  bool WinDeclarationIsPossible(bool is_csa_rule) const;

  /**
   * 盤上の駒のみを考慮して、ハッシュ値を計算します.
   */
  Key64 ComputeBoardKey() const;

  /**
   * 盤上の駒、持ち駒、手番をすべて考慮して、ハッシュ値を計算します.
   */
  Key64 ComputePositionKey() const;

  /**
   * 探索ノード数（MakeMove() または MakeNullMove() を呼んだ回数）を返します.
   */
  uint64_t nodes_searched() const;

  /**
   * 探索ノード数をセットします.
   */
  void set_nodes_searched(uint64_t nodes);

  /**
   * 未使用の駒の枚数を返します（主にデバッグ用）.
   *   - 通常の対局では、40枚の駒をすべて使用しているため、常に 0 を返します。
   *   - 駒落ち対局等では、使っていない駒があるため、未使用の駒の枚数 > 0 となる場合があります。
   */
  int num_unused_pieces(PieceType pt) const;

  /**
   * 局面の状態に関する情報を初期化します.
   */
  void InitStateInfo();

  /**
   * 局面のSFEN表記を返します.
   */
  std::string ToSfen() const;

  /**
   * SFENを読み込んで、それに対応する局面を返します.
   */
  static Position FromSfen(const std::string& sfen);

  /**
   * 初期局面を作成します.
   */
  static Position CreateStartPosition();

  /**
   * 将棋盤を180度回転させます.
   *
   * このメソッドを使うと、以下の操作が行われます。
   *   - 将棋盤を180度回転する
   *   - 先手の持ち駒を後手の持駒にし、後手の持駒を先手の持ち駒にする
   *   - 手番を入れ替える
   */
  Position& Flip();

  /**
   * 内部状態が正しければ、trueを返します.
   */
  bool IsOk(std::string* error_message = nullptr) const;

  /**
   * 現局面を画面に表示します（主にデバッグ用）.
   */
  void Print(Move move = kMoveNone) const;

 private:

  struct StateInfo {
    Bitboard checkers;
    Bitboard pinned_pieces;
    Bitboard discovered_check_candidates;
    Move last_move;
    int num_checkers;
    ExtendedBoard extended_board;
  };

  // ComputePinnedPieces() / ComputeDiscoveredCheckCandidates() の内部実装です
  Bitboard ComputeObstructingPieces(Color king_color) const;

  // pieces() の内部実装です
  Bitboard type_bb(PieceType pt) const;
  template<typename ...Args> Bitboard type_bb(PieceType pt1, Args... pt2) const;

  ArrayMap<Hand, Color> hand_;
  ArrayMap<Square, Color> king_square_{kSquareNone, kSquareNone};

  std::vector<StateInfo> state_infos_;
  std::vector<StateInfo>::iterator current_state_info_;

  uint64_t nodes_searched_ = 0;
  Color side_to_move_ = kBlack;

  Bitboard occupied_bb_;
  ArrayMap<Bitboard, Color> color_bb_;
  ArrayMap<Bitboard, PieceType> type_bb_;
  ArrayMap<Piece, Square> piece_on_;

  ArrayMap<int, PieceType> num_unused_pieces_;
};

inline const ArrayMap<Bitboard, Color>& Position::color_bb() const {
  return color_bb_;
}

inline const ArrayMap<Bitboard, PieceType>& Position::type_bb() const {
  return type_bb_;
}

inline Bitboard Position::pieces() const {
  return occupied_bb_;
}

inline Bitboard Position::pieces(Color c) const {
  return color_bb_[c];
}

inline Bitboard Position::pieces(Piece p) const {
  return color_bb_[p.color()] & type_bb_[p.type()];
}

inline Bitboard Position::pieces(PieceType pt) const {
  return type_bb_[pt];
}

template<typename ...Args>
inline Bitboard Position::pieces(PieceType pt1, Args... pt2) const {
  return type_bb(pt1, pt2...);
}

template<typename ...Args>
inline Bitboard Position::pieces(Color c, PieceType pt1, Args... pt2) const {
  return color_bb_[c] & type_bb(pt1, pt2...);
}

inline Bitboard Position::golds() const {
  return type_bb(kGold, kPPawn, kPLance, kPKnight, kPSilver);
}

inline Bitboard Position::golds(Color c) const {
  return color_bb_[c] & golds();
}

inline Piece Position::piece_on(Square s) const {
  return piece_on_[s];
}

inline bool Position::king_exists(Color c) const {
  return king_square_[c] != kSquareNone;
}

inline Square Position::stm_king_square() const {
  assert(king_exists(side_to_move_));
  return king_square_[side_to_move_];
}

inline Square Position::king_square(Color c) const {
  assert(king_exists(c));
  return king_square_[c];
}

inline int Position::num_pieces(PieceType pt) const {
  int num = num_pieces(kBlack, pt) + num_pieces(kWhite, pt);
  assert(0 <= num && num <= 18);
  return num;
}

inline int Position::num_pieces(Color c, PieceType pt) const {
  int num = pieces(c, pt).count();
  if (IsDroppablePieceType(pt))
    num += hand_[c].count(pt);
  assert(0 <= num && num <= 18);
  return num;
}

inline bool Position::is_empty(Square s) const {
  return piece_on_[s] == kNoPiece;
}

inline bool Position::pawn_on_file(File f, Color c) const {
  return (pieces(c, kPawn) & file_bb(f)).any();
}

inline const ExtendedBoard& Position::extended_board() const {
  return current_state_info_->extended_board;
}

inline bool Position::square_is_attacked(Color c, Square s) const {
  return current_state_info_->extended_board.num_controls(c, s) != 0;
}

inline int Position::num_controls(Color c, Square s) const {
  return current_state_info_->extended_board.num_controls(c, s);
}

inline DirectionSet Position::long_controls(Color c, Square s) const {
  return current_state_info_->extended_board.long_controls(c, s);
}

inline Hand Position::stm_hand() const {
  return hand_[side_to_move_];
}

inline Hand Position::hand(Color c) const {
  return hand_[c];
}

inline Color Position::side_to_move() const {
  return side_to_move_;
}

inline void Position::set_side_to_move(Color c) {
  side_to_move_ = c;
}

template<Color kC, PieceType kPt>
inline Bitboard Position::AttacksFrom(Square s) const {
  return attacks_from<kC, kPt>(s, pieces());
}

template<Color kC, PieceType kPt>
inline Bitboard Position::AttackersTo(Square s) const {
  return attackers_to<kC, kPt>(s, pieces()) & pieces(kC, kPt);
}

template<Color kC>
Bitboard Position::AttackersTo(Square to, Bitboard occ) const {
  Bitboard hdk = pieces(kHorse, kDragon, kKing);
  Bitboard rd  = pieces(kRook, kDragon);
  Bitboard bh  = pieces(kBishop, kHorse);
  Bitboard attackers = (attackers_to<kC, kKing  >(to, occ) & hdk            )
                     | (attackers_to<kC, kRook  >(to, occ) & rd             )
                     | (attackers_to<kC, kBishop>(to, occ) & bh             )
                     | (attackers_to<kC, kGold  >(to, occ) & golds()        )
                     | (attackers_to<kC, kSilver>(to, occ) & pieces(kSilver))
                     | (attackers_to<kC, kKnight>(to, occ) & pieces(kKnight))
                     | (attackers_to<kC, kLance >(to, occ) & pieces(kLance ))
                     | (attackers_to<kC, kPawn  >(to, occ) & pieces(kPawn  ));
  return attackers & pieces(kC);
}

inline Bitboard Position::AttackersTo(Square s, Bitboard occ, Color c) const {
  return AttackersTo(s, occ) & color_bb_[c];
}

inline bool Position::in_check() const {
  return current_state_info_->num_checkers != 0;
}

inline int Position::num_checkers() const {
  return current_state_info_->num_checkers;
}

inline Bitboard Position::checkers() const {
  return current_state_info_->checkers;
}

inline Bitboard Position::pinned_pieces() const {
  return current_state_info_->pinned_pieces;
}

inline Bitboard Position::discovered_check_candidates() const {
  return current_state_info_->discovered_check_candidates;
}

inline Bitboard Position::ComputeCheckers() const {
  // そもそも味方の玉がいなければ、相手から王手されることはない
  if (!king_exists(side_to_move_)) {
    return Bitboard();
  }
  return AttackersTo(stm_king_square(), pieces(), ~side_to_move_);
}

inline Move Position::last_move() const {
  return current_state_info_->last_move;
}

inline Move Position::move_before_n_ply(unsigned ply) const {
  assert(ply >= 1);
  size_t size = current_state_info_ - state_infos_.begin() + 1;
  return ply < size ? (current_state_info_ - ply + 1)->last_move : kMoveNone;
}

inline int Position::game_ply() const {
  return static_cast<int>(current_state_info_ - state_infos_.begin());
}

inline bool Position::PseudoLegalMoveIsLegal(Move move) const {
  assert(move.IsOk());
  assert(move.is_real_move());
  assert(MoveIsPseudoLegal(move));
  return move.is_drop() || NonDropMoveIsLegal(move);
}

inline void Position::UnmakeNullMove() {
  assert(IsOk());
  assert(!in_check());
  side_to_move_ = ~side_to_move_;
  --current_state_info_;
  assert(IsOk());
}

inline uint64_t Position::nodes_searched() const {
  return nodes_searched_;
}

inline void Position::set_nodes_searched(uint64_t nodes) {
  nodes_searched_ = nodes;
}

inline int Position::num_unused_pieces(PieceType pt) const {
  return num_unused_pieces_[pt];
}

inline Bitboard Position::ComputePinnedPieces() const {
  return ComputeObstructingPieces(side_to_move_);
}

inline Bitboard Position::ComputeDiscoveredCheckCandidates() const {
  return ComputeObstructingPieces(~side_to_move_);
}

inline Bitboard Position::type_bb(PieceType pt) const {
  return type_bb_[pt];
}

template<typename ...Args>
inline Bitboard Position::type_bb(PieceType pt1, Args... pt2) const {
  return type_bb_[pt1] | type_bb(pt2...);
}

#endif /* POSITION_H_ */
