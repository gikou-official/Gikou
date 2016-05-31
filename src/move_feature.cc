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

#include "move_feature.h"

#include <type_traits>
#include "common/bitfield.h"
#include "common/math.h"
#include "common/number.h"
#include "common/pack.h"
#include "common/valarraykey.h"
#include "material.h"
#include "position.h"
#include "stats.h"
#include "swap.h"

namespace {

/**
 * 指し手のカテゴリを表すクラスです.
 *
 * 現在の実装では、
 *   - 駒の種類（14通り）
 *   - SEE値が負か否か（2通り）
 * の、合計28通りに場合分けを行っています。
 *
 * もっとも、計算の効率化のため、実装上は、全部で32通りあることにして計算しています。
 */
struct MoveCategory {
 public:
  MoveCategory(PieceType piece_type, bool is_negative_see)
      : bitfield_(0) {
    bitfield_.set(kKeyPieceType, piece_type);
    bitfield_.set(kKeyNegativeSee, is_negative_see);
  }

  operator size_t() const {
    return bitfield_;
  }

  static size_t size() {
    return 32;
  }

 private:
  typedef BitField<uint32_t>::Key Key;
  static_assert(std::is_same<uint32_t, MoveFeatureIndex>::value, "");
  static constexpr Key kKeyPieceType{0, 4};
  static constexpr Key kKeyNegativeSee{4, 5};

  BitField<uint32_t> bitfield_;
};

constexpr MoveCategory::Key MoveCategory::kKeyPieceType;
constexpr MoveCategory::Key MoveCategory::kKeyNegativeSee;

const int kMinMaterialGain = -7, kMaxMaterialGain = 7;
const int kMaxControls = 3;
const int kMaxRelation = 152;

ValarrayKeyChain key_chain;

//
// 1. 指し手の基本的なカテゴリ等
//
// バイアス項（どの指し手にも必ず特徴として入れる）
const auto kBias = key_chain.CreateKey();
// 駒の損得（SEEで計算したもの。「歩何枚分か」で表す）
const auto kMaterialGain = key_chain.CreateKey<Number<kMinMaterialGain, kMaxMaterialGain>>();
// 駒の損得 [得か、損得なしか、損か][打つ手か否か]
const auto kSeeSign = key_chain.CreateKey<Number<-1, 1>, bool>();
// History値（0%~100%までを、16分割する）
const auto kHistoryValue = key_chain.CreateKey<Number<0, 15>>();
// 評価値のgain （50点刻みで、1~800点までを16分割。gainsが負のときは何もしない。）
const auto kEvaluationGain = key_chain.CreateKey<Number<0, 15>>();
// 成る手
const auto kIsPromotion = key_chain.CreateKey();
// 打つ手
const auto kIsDrop = key_chain.CreateKey();
// 打つ手の場合に、持ち駒の枚数
const auto kNumHandPieces = key_chain.CreateKey<PieceType, Number<0, 3>>();

//
// 2. 移動先・移動元のマスに関する特徴
//
// 移動先・移動元の段（移動先は、打つ手か否かで場合分け）
const auto kDstRank = key_chain.CreateKey<Rank, bool>();
const auto kSrcRank = key_chain.CreateKey<Rank>();
// 移動先・移動元のマス
const auto kDstSquare = key_chain.CreateKey<Square>();
const auto kSrcSquare = key_chain.CreateKey<Square>();
// 移動先・移動元の周囲15マス（縦5マスx横3マス）の駒の配置パターン [方向][駒][味方の利き][敵の利き]
const auto kDstPattern = key_chain.CreateKey<Number<0, 15>, Piece, Number<0, 1>, Number<0, 1>>();
const auto kSrcPattern = key_chain.CreateKey<Number<0, 15>, Piece, Number<0, 1>, Number<0, 1>>();
// 直前の相手の手との関係
const auto kDstRelationToPreviousMove1 = key_chain.CreateKey<PieceType, Number<0, kMaxRelation>>();
const auto kSrcRelationToPreviousMove1 = key_chain.CreateKey<PieceType, Number<0, kMaxRelation>>();
// ２手前の自分の手との関係
const auto kDstRelationToPreviousMove2 = key_chain.CreateKey<PieceType, Number<0, kMaxRelation>>();
const auto kSrcRelationToPreviousMove2 = key_chain.CreateKey<PieceType, Number<0, kMaxRelation>>();
// 移動先・移動元の、敵・味方の利き数の組み合わせ（移動先の場合は、打つ手か否かで区別）
const auto kDstControls = key_chain.CreateKey<bool, Number<0, kMaxControls>, Number<0, kMaxControls>>();
const auto kSrcControls = key_chain.CreateKey<Number<0, kMaxControls>, Number<0, kMaxControls>>();
// 移動先・移動元の、敵・味方の長い利きの数
const auto kDstLongControls = key_chain.CreateKey<Color, Number<0, kMaxControls>>();
const auto kSrcLongControls = key_chain.CreateKey<Color, Number<0, kMaxControls>>();
// 飛車との相対位置
const auto kDstRelationToRook = key_chain.CreateKey<Color, Number<0, kMaxRelation>>();
const auto kSrcRelationToRook = key_chain.CreateKey<Color, Number<0, kMaxRelation>>();
// 自玉・敵玉との相対位置
const auto kDstRelationToKing = key_chain.CreateKey<Color, Number<0, kMaxRelation>>();
const auto kSrcRelationToKing = key_chain.CreateKey<Color, Number<0, kMaxRelation>>();
// 移動先・移動元と、敵・味方近い方の玉との距離
const auto kDstDistanceToKing = key_chain.CreateKey<Number<0, 9>>();
const auto kSrcDistanceToKing = key_chain.CreateKey<Number<0, 9>>();
// 敵玉のいる筋と、移動先・移動元の筋の組み合わせ
const auto kDstFileAndOppKingFile = key_chain.CreateKey<File, File>();
const auto kSrcFileAndOppKingFile = key_chain.CreateKey<File, File>();
// 自玉・敵玉とのマンハッタン距離の増減
const auto kDistanceToKingDelta = key_chain.CreateKey<Color, Number<-1, 1>>();

//
// 3. 取る手に関する特徴
//
// 取る手か否か
const auto kIsCapture = key_chain.CreateKey();
// 取る手の損得
const auto kCaptureGain = key_chain.CreateKey<Number<kMinMaterialGain, kMaxMaterialGain>>();
// 取った駒の種類（SEEの符号で場合分け）
const auto kCapturedPieceType = key_chain.CreateKey<Number<-1, 1>, PieceType>();
// 取れる最高の駒を取る手（SEEの符号で場合分け）
const auto kIsCaptureOfMostValuablePiece = key_chain.CreateKey<Number<-1, 1>>();
// 直前に動いた駒の取り返し（SEEの符号で場合分け）
const auto kIsRecapture = key_chain.CreateKey<Number<-1, 1>>();
// 2手前に動いた駒で駒を取る手（SEEの符号で場合分け）
const auto kIsCaptureByLastMovedPiece = key_chain.CreateKey<Number<-1, 1>>();

//
// 4. 王手に関する特徴
//
// 王手か否か
const auto kGivesCheck = key_chain.CreateKey();
// 有効王手（相手から取り返されない位置にする王手）か否か
const auto kEffectiveCheck = key_chain.CreateKey();
// 開き王手か否か
const auto kDiscoveredCheck = key_chain.CreateKey();
// ただ捨ての王手か否か
const auto kSacrificeCheck = key_chain.CreateKey();
// 送りの手筋の王手（玉が取り返すと、相手の駒を取れるかどうか）[取れる相手の駒の種類]
const auto kSendOffCheck = key_chain.CreateKey<PieceType>();

//
// 5. 王手回避手に関する特徴
//
// 王手を回避する手か否か
const auto kIsEvasion = key_chain.CreateKey();
// 王手している駒を取る手（王手駒の種類で場合分け）
const auto kCaptureChecker = key_chain.CreateKey<PieceType>();
// 玉が逃げる手の場合に、玉が逃げるY軸方向（上か、横か、下か）
const auto kEvasionDirection = key_chain.CreateKey<Number<-1, 1>>();
// 合駒の場合に、合駒と玉との距離（1から8まで）
const auto kInterceptionDistance = key_chain.CreateKey<Number<1, 8>>();
// 合駒の場合に、王手している相手の駒の種類
const auto kCheckerType = key_chain.CreateKey<PieceType>();

//
// 6. 攻める手に関する特徴
//
// 利きを付けた駒の種類 [利きを付けた駒][味方の利きの有無][敵の利きの有無]
const auto kAttackToPiece = key_chain.CreateKey<Piece, bool, bool>();
// ピンしている駒に対する当たり [利きを付けた駒の種類]
const auto kAttackToPinnedPiece = key_chain.CreateKey<PieceType>();
// 当たりを付けた駒のうち、最も価値が高い相手の駒 [敵の利きの有無]
const auto kMostValuableAttack1 = key_chain.CreateKey<PieceType, bool>();
//　当たりを付けた駒のうち、２番目に価値が高い相手の駒 [敵の利きの有無]（駒の両取りを意識）
const auto kMostValuableAttack2 = key_chain.CreateKey<PieceType, bool>();
// そっぽ手判定 [相手玉との距離][利きを付けた駒の種類]
const auto kDistanceBetweenKingAndVictim = key_chain.CreateKey<Number<1, 9>, PieceType>();
// 空き当たり [駒の種類][相手の利きの有無]
const auto kDiscoveredAttack = key_chain.CreateKey<PieceType, bool>();
// 大駒を敵陣に打ち込む手 [min(安全に成れるマスの数, 7)]
const auto kDropToOpponentArea = key_chain.CreateKey<Number<0, 7>>();
// 駒を成る手 [相手玉との距離]
const auto kDistanceToKingWhenPromotion = key_chain.CreateKey<Number<1, 9>>();
// 相手の歩のない筋で、味方飛香の利きにある歩を５段目より前に動かす手
const auto kAdvancingPawn = key_chain.CreateKey();

//
// 7. 受ける手に関する特徴
//
// 駒取りを防ぐ
//   取られそうな最高の駒を逃げる手
const auto kDefendMostValuablePiece = key_chain.CreateKey();
//   取られそうな最高の駒に、利きを足して守る手（取られそうな駒の種類、味方の利きの有無で場合分け）
const auto kSupportMostValuablePiece = key_chain.CreateKey<PieceType, bool>();
//   直前の相手の手で当たりにされた最高の駒が逃げる手（相手が動かした駒の種類で場合分け）
const auto kDefendPiecesAttackedByLastMove = key_chain.CreateKey<PieceType>();
//   直前の相手の手で取られそうな最高の駒に、合駒をして守る手（相手が動かした駒の種類で場合分け）
const auto kInterceptAttacks = key_chain.CreateKey<PieceType>();
//   直前の相手の手で取られそうな最高の駒に、利きを足して守る手（相手が動かした駒の種類、味方の利きの有無で場合分け）
const auto kSupportPiecesAttackedByLastMove = key_chain.CreateKey<PieceType, bool>();

//
// 8. 自玉周り
//
// 自玉の８近傍に利きをつけている相手の飛角に合駒をする手 [打つ手かどうか]
const auto kInterceptAttacksToOurCastle = key_chain.CreateKey<bool>();
// 自玉の８近傍に利きを足す手（味方の利きよりも、相手の利きのほうが多い場合のみ）[打つ手かどうか]
const auto kReinforceOurCastle = key_chain.CreateKey<bool>();
// 相手が持ち駒で自玉に有効王手をかけることができるマスに、先に駒を打って埋める手
const auto kObstructOpponentEffectiveChecks = key_chain.CreateKey();

//
// 9. 相手玉周り
//
// 相手玉周りに利きをつける手（8近傍、24近傍）
const auto kAttackToKingNeighborhood8 = key_chain.CreateKey();
const auto kAttackToKingNeighborhood24 = key_chain.CreateKey();
// 相手玉の24近傍にある、相手の金・銀に当たりをかける手
const auto kAttackToGoldNearKing = key_chain.CreateKey();
const auto kAttackToSilverNearKing = key_chain.CreateKey();

//
// 10. 歩の手筋
//
// 歩交換 [min(持ち駒の歩の枚数, 3)]
const auto kExchangePawn = key_chain.CreateKey<Number<0, 3>>();
// 直射止めの歩（相手の飛・香の、自陣に対する利きを遮る手）
const auto kInterceptAttacksToOurAreaByPawn = key_chain.CreateKey();
// 垂れ歩（２〜４段目に、敵の駒に当てないで歩を打つ手）[ひとつ上の味方の利きの有無][同・敵の利きの有無]
const auto kDanglingPawn = key_chain.CreateKey<bool, bool>();
// 底歩を打って、飛車の横利きを止める手 [上にある駒]
const auto kAnchorByPawn = key_chain.CreateKey<PieceType>();
// 成り捨ての歩 [相手玉との距離]
const auto kPawnSacrificePromotion = key_chain.CreateKey<Number<1, 9>>();
// 継ぎ歩から垂れ歩（同〜と取られたら、次に垂れ歩を打てる手）
const auto kJoiningPawnBeforeDanglingPawn = key_chain.CreateKey();
// 歩を打って、飛香の利きを止める手 [何枚歩を打てば利きが止まるか] （連打の歩を意識）
const auto kSuccessivePawnDrops = key_chain.CreateKey<Number<1, 8>>();
// 銀バサミの歩
const auto kSilverPincerPawn = key_chain.CreateKey();
// 蓋歩（敵の飛車の退路を断つ歩）
const auto kCutOffTheRookRetreatByPawn = key_chain.CreateKey();
// 控えの歩（自陣において、敵駒に当てないで打つ歩）[ひとつ上の味方の利きの有無][同・敵の利きの有無]
const auto kPawnTowardsTheRear = key_chain.CreateKey<bool, bool>();
// 端歩を突く手 [玉とのX距離]
const auto kEdgePawnPush = key_chain.CreateKey<Number<0, 9>>();
// 端攻めの歩 [min(持ち駒の歩の枚数, 5)]
const auto kEdgePawnAttackWithHandPawns = key_chain.CreateKey<Number<0, 5>>();
// 端攻めの歩 [min(持ち駒の桂の枚数, 3)]
const auto kEdgePawnAttackWithHandKnights = key_chain.CreateKey<Number<0, 3>>();
// 桂頭の歩 [相手の桂馬が逃げられるマスの数(0~2)]
const auto kAttackToKnightByPawn = key_chain.CreateKey<Number<0, 2>>();
// 同〜と取られたら、次に桂頭に歩を打てる手
const auto kPawnSacrificeBeforeAttackToKnight = key_chain.CreateKey();
// 味方の飛角の利きを止めている敵の歩に対して、歩で当たりをかける手 [飛角の利きの先にある駒][その駒への味方の利きの有無][同・敵の利きの有無]
const auto kAttackToObstructingPawn = key_chain.CreateKey<Piece, bool, bool>();

//
// 11. 香の手筋
//
// 相手の駒に利きを付ける手 [相手の駒の種類][相手が歩で受けられるか否か]（底歩に対する香打ち等を意識）
const auto kAttackByLance = key_chain.CreateKey<PieceType, bool>();
// 香車の影の利きにある敵の駒 [相手の駒の種類]
const auto kXrayAttackByLance = key_chain.CreateKey<PieceType>();
// 田楽の香（歩の受けが利かない手に限る）[串刺しにした２枚の駒のうち、安い方の敵の駒]
const auto kLanceSkewer = key_chain.CreateKey<PieceType>();
// 香浮き（飛車が回り込める場合）
const auto kFloatingLance = key_chain.CreateKey();

//
// 12. 桂の手筋
//
// 控えの桂（駒損しない、打つ手のみ）[桂が跳ねれば当たりになる相手の駒]
const auto kKnightTowardsTheRear = key_chain.CreateKey<PieceType>();

} // namespace

const int kNumMoveFeatures =
      2 * Move::kPerfectHashSize // 指し手の完全ハッシュを格納する
    + MoveCategory::size() * key_chain.total_size_of_keys()
    + 1; // History値を格納する

const int kHistoryFeatureIndex = kNumMoveFeatures - 1;

template<Color kColor>
MoveFeatureList ExtractMoveFeatures(const Move move, const Position& pos,
                                    const PositionInfo& pos_info) {
  assert(pos.MoveIsPseudoLegal(move));

  MoveFeatureList feature_list;

  const ExtendedBoard& ext_board = pos.extended_board();

  const Square own_ksq = pos.king_square(kColor);
  const Square opp_ksq = pos.king_square(~kColor);

  const Score see_score = Swap::Evaluate(move, pos);
  const int see_sign = math::sign(int(see_score));
  const int see_is_negative = see_score < kScoreZero;
  const int material_gain = std::min(std::max(int(see_score / 200), kMinMaterialGain), kMaxMaterialGain);

  const Move previous_move1 = pos.last_move();
  const Move previous_move2 = pos.move_before_n_ply(2);

  // 動かした駒の利き
  const Bitboard attacks = AttacksFrom(move.piece_after_move(), move.to(), pos.pieces());

  if (move.is_quiet()) {
    // History値
    feature_list.history = double(pos_info.history[move]) / double(HistoryStats::kMax);

    // 指し手そのもの
    int negative_see_flag = see_is_negative * Move::kPerfectHashSize;
    feature_list.push_back(negative_see_flag + move.PerfectHash());
  } else {
    // History値
    feature_list.history = 0.0; // 特徴が存在しないのと同じにする
  }

  // 指し手のカテゴリを特定
  const PieceType piece_type = move.piece_type();
  const size_t key_start = 2 * Move::kPerfectHashSize;
  const MoveCategory category(piece_type, see_is_negative);
  const size_t key_offset = key_start + category * key_chain.total_size_of_keys();

  // 以後は、同じ特徴でも、[駒の種類][駒損手か否か]によって異なるインデックスを割り当てる
  auto add_feature = [&](size_t index) {
    feature_list.push_back(key_offset + index);
  };

  //
  // 1. 指し手の基本的なカテゴリ等
  //
  {
    // バイアス項（どの指し手にも必ず特徴として入れる）
    add_feature(kBias());

    // 駒の損得（SEEで計算したもの。「歩何枚分か」で表す）
    add_feature(kMaterialGain(material_gain));

    // 駒の損得 [得か、損得なしか、損か][打つ手か否か]
    add_feature(kSeeSign(see_sign, move.is_drop()));

    if (move.is_quiet()) {
      // History値（0%~100%までを、16分割する）
      int history_value = 16 * pos_info.history[move] / HistoryStats::kMax;
      assert(history_value >= 0);
      add_feature(kHistoryValue(std::min(history_value, 15)));

      // 評価値のgain （50点刻みで、1~800点までを16分割。gainsが負のときは何もしない。）
      int gain = pos_info.gains[move];
      if (gain > 0) {
        add_feature(kEvaluationGain(std::min(gain / 50, 15)));
      }
    }

    // 成る手か否か
    if (move.is_promotion()) add_feature(kIsPromotion());

    if (move.is_drop()) {
      // 打つ手か否か
      add_feature(kIsDrop());

      // 持ち駒の枚数 [持ち駒の種類][min(持ち駒の枚数,3)]
      Hand hand = pos.stm_hand();
      add_feature(kNumHandPieces(kPawn  , std::min(hand.count(kPawn  ), 3)));
      add_feature(kNumHandPieces(kLance , std::min(hand.count(kLance ), 3)));
      add_feature(kNumHandPieces(kKnight, std::min(hand.count(kKnight), 3)));
      add_feature(kNumHandPieces(kSilver, std::min(hand.count(kSilver), 3)));
      add_feature(kNumHandPieces(kGold  , std::min(hand.count(kGold  ), 3)));
      add_feature(kNumHandPieces(kBishop, hand.count(kBishop)));
      add_feature(kNumHandPieces(kRook  , hand.count(kRook  )));
    }
  }

  //
  // 2-a. 移動先のマスに関する特徴
  //
  {
    const Square to = move.to(); // 移動先
    const Square dst = to.relative_square(kColor); // 移動先（先手視点にしたもの）

    // 移動先の段 [段][打つ手か否か]
    add_feature(kDstRank(dst.rank(), move.is_drop()));

    // 移動先のマス
    add_feature(kDstSquare(dst));

    // 移動先の周囲15マス（縦5マスx横3マス）の駒の配置パターン
    FifteenNeighborhoods pieces = ext_board.GetFifteenNeighborhoodPieces(to);
    FifteenNeighborhoods own_controls = ext_board.GetFifteenNeighborhoodControls(kColor, to).LimitTo(1);
    FifteenNeighborhoods opp_controls = ext_board.GetFifteenNeighborhoodControls(~kColor, to).LimitTo(1);
    if (kColor == kWhite) {
      pieces       = pieces.Rotate180().FlipPieceColors();
      own_controls = own_controls.Rotate180();
      opp_controls = opp_controls.Rotate180();
    }
    for (size_t i = 0; i < 15; ++i) {
      Piece p(pieces.at(i));
      add_feature(kDstPattern(i, p, own_controls.at(i), opp_controls.at(i)));
    }

    // 移動先と、直前の相手の手との関係
    if (previous_move1.is_real_move()) {
      PieceType pt1 = previous_move1.piece_type_after_move();
      Square dst1 = previous_move1.to().relative_square(kColor);
      int relation1 = Square::relation(dst, dst1);
      add_feature(kDstRelationToPreviousMove1(pt1, relation1));
    }

    // 移動先と、２手前の自分の手との関係
    if (previous_move2.is_real_move()) {
      PieceType pt2 = previous_move2.piece_type_after_move();
      Square dst2 = previous_move2.to().relative_square(kColor);
      int relation2 = Square::relation(dst, dst2);
      add_feature(kDstRelationToPreviousMove2(pt2, relation2));
    }

    // 移動先の、敵・味方の利き数の組み合わせ
    int num_own_controls = std::min(ext_board.num_controls(kColor, to), kMaxControls);
    int num_opp_controls = std::min(ext_board.num_controls(~kColor, to), kMaxControls);
    add_feature(kDstControls(move.is_drop(), num_own_controls, num_opp_controls));

    // 移動先の、敵・味方の長い利きの数
    int own_long_controls = std::min(ext_board.long_controls(kColor, to).count(), kMaxControls);
    int opp_long_controls = std::min(ext_board.long_controls(~kColor, to).count(), kMaxControls);
    add_feature(kDstLongControls(kBlack, own_long_controls));
    add_feature(kDstLongControls(kWhite, opp_long_controls));

    // 移動先と味方の飛車との相対位置
    for (Bitboard own_rooks = pos.pieces(kColor, kRook, kDragon); own_rooks.any(); ) {
      Square rook_sq = own_rooks.pop_first_one().relative_square(kColor);
      add_feature(kDstRelationToRook(kBlack, Square::relation(dst, rook_sq)));
    }

    // 移動先と相手の飛車との相対位置
    for (Bitboard opp_rooks = pos.pieces(~kColor, kRook, kDragon); opp_rooks.any(); ) {
      Square rook_sq = opp_rooks.pop_first_one().relative_square(kColor);
      add_feature(kDstRelationToRook(kWhite, Square::relation(dst, rook_sq)));
    }

    // 移動先と玉との相対位置
    int relation_to_own_king = Square::relation(dst, own_ksq.relative_square(kColor));
    int relation_to_opp_king = Square::relation(dst, opp_ksq.relative_square(kColor));
    add_feature(kDstRelationToKing(kBlack, relation_to_own_king));
    add_feature(kDstRelationToKing(kWhite, relation_to_opp_king));

    // 移動先と、敵・味方近い方の玉との距離
    int distance = std::min(Square::distance(to, own_ksq), Square::distance(to, opp_ksq));
    add_feature(kDstDistanceToKing(distance));

#if 0
    // TODO 除去（一致率マイナス）
    // 敵玉のいる筋と、移動先の筋の組み合わせ
    File dst_file = dst.file();
    File opp_king_file = opp_ksq.relative_square(kColor).file();
    add_feature(kDstFileAndOppKingFile(dst_file, opp_king_file));
#endif
  }

  //
  // 2-b. 移動元のマスに関する特徴
  //
  if (!move.is_drop()) {
    const Square from = move.from(); // 移動元
    const Square src = from.relative_square(kColor); // 移動元（先手視点にしたもの）

    // 移動元の段
    add_feature(kSrcRank(src.rank()));

    // 移動元のマス
    add_feature(kSrcSquare(src));

    // 移動元の周囲15マス（縦5マスx横3マス）の駒の配置パターン
    FifteenNeighborhoods pieces = ext_board.GetFifteenNeighborhoodPieces(from);
    FifteenNeighborhoods own_controls = ext_board.GetFifteenNeighborhoodControls(kColor, from).LimitTo(1);
    FifteenNeighborhoods opp_controls = ext_board.GetFifteenNeighborhoodControls(~kColor, from).LimitTo(1);
    if (kColor == kWhite) {
      pieces       = pieces.Rotate180().FlipPieceColors();
      own_controls = own_controls.Rotate180();
      opp_controls = opp_controls.Rotate180();
    }
    for (size_t i = 0; i < 15; ++i) {
      Piece p(pieces.at(i));
      add_feature(kSrcPattern(i, p, own_controls.at(i), opp_controls.at(i)));
    }

    // 移動元と、直前の相手の手との関係
    if (previous_move1.is_real_move()) {
      PieceType pt1 = previous_move1.piece_type_after_move();
      Square src1 = previous_move1.to().relative_square(kColor);
      int relation1 = Square::relation(src, src1);
      add_feature(kSrcRelationToPreviousMove1(pt1, relation1));
    }

    // 移動元と、２手前の自分の手との関係
    if (previous_move2.is_real_move()) {
      PieceType pt2 = previous_move2.piece_type_after_move();
      Square src2 = previous_move2.to().relative_square(kColor);
      int relation2 = Square::relation(src, src2);
      add_feature(kSrcRelationToPreviousMove2(pt2, relation2));
    }

    // 移動元の、敵・味方の利き数の組み合わせ
    int num_own_controls = std::min(ext_board.num_controls(kColor, from), kMaxControls);
    int num_opp_controls = std::min(ext_board.num_controls(~kColor, from), kMaxControls);
    add_feature(kSrcControls(num_own_controls, num_opp_controls));

    // 移動元の、敵・味方の長い利きの数
    int own_long_controls = std::min(ext_board.long_controls(kColor, from).count(), kMaxControls);
    int opp_long_controls = std::min(ext_board.long_controls(~kColor, from).count(), kMaxControls);
    add_feature(kSrcLongControls(kBlack, own_long_controls));
    add_feature(kSrcLongControls(kWhite, opp_long_controls));

    // 移動元と味方の飛車との相対位置
    for (Bitboard own_rooks = pos.pieces(kColor, kRook, kDragon); own_rooks.any(); ) {
      Square rook_sq = own_rooks.pop_first_one().relative_square(kColor);
      add_feature(kSrcRelationToRook(kBlack, Square::relation(src, rook_sq)));
    }

    // 移動元と相手の飛車との相対位置
    for (Bitboard opp_rooks = pos.pieces(~kColor, kRook, kDragon); opp_rooks.any(); ) {
      Square rook_sq = opp_rooks.pop_first_one().relative_square(kColor);
      add_feature(kSrcRelationToRook(kWhite, Square::relation(src, rook_sq)));
    }

    // 移動元と玉との相対位置
    int relation_to_own_king = Square::relation(src, own_ksq.relative_square(kColor));
    int relation_to_opp_king = Square::relation(src, opp_ksq.relative_square(kColor));
    add_feature(kSrcRelationToKing(kBlack, relation_to_own_king));
    add_feature(kSrcRelationToKing(kWhite, relation_to_opp_king));

    // 移動元と、敵・味方近い方の玉との距離
    int distance = std::min(Square::distance(from, own_ksq), Square::distance(from, opp_ksq));
    add_feature(kSrcDistanceToKing(distance));

#if 0
    // TODO 除去（一致率マイナス）
    // 敵玉のいる筋と、移動元の筋の組み合わせ
    File src_file = src.file();
    File opp_king_file = opp_ksq.relative_square(kColor).file();
    add_feature(kSrcFileAndOppKingFile(src_file, opp_king_file));
#endif

    // 玉とのマンハッタン距離の増減
    int src_distance_own = Square::distance(from     , own_ksq);
    int dst_distance_own = Square::distance(move.to(), own_ksq);
    int src_distance_opp = Square::distance(from     , opp_ksq);
    int dst_distance_opp = Square::distance(move.to(), opp_ksq);
    int delta_own = math::sign(dst_distance_own - src_distance_own);
    int delta_opp = math::sign(dst_distance_opp - src_distance_opp);
    add_feature(kDistanceToKingDelta(kBlack, delta_own));
    add_feature(kDistanceToKingDelta(kWhite, delta_opp));
  }

  //
  // 3. 取る手に関する特徴
  //
  if (move.is_capture()) {
    // 取る手の損得
    add_feature(kCaptureGain(material_gain));

    // 取った駒の種類
    add_feature(kCapturedPieceType(see_sign, move.captured_piece_type()));

    // 取れる最高の駒を取る手（SEEの符号で場合分け）
    if (pos_info.most_valuable_victim.test(move.to())) {
      add_feature(kIsCaptureOfMostValuablePiece(see_sign));
    }

    // 直前に動いた駒の取り返し（SEEの符号で場合分け）
    if (previous_move1.is_real_move() && move.to() == previous_move1.to()) {
      add_feature(kIsRecapture(see_sign));
    }

#if 0
    // TODO 除去（ほんのわずかに一致率が悪化）
    // 2手前に動いた駒で駒を取る手（SEEの符号で場合分け）
    if (previous_move2.is_real_move() && move.to() == previous_move2.to()) {
      add_feature(kIsCaptureByLastMovedPiece(see_sign));
    }
#endif
  }

  //
  // 4. 王手に関する特徴
  //
  if (pos.MoveGivesCheck(move)) {
    const int num_opp_controls = ext_board.num_controls(~kColor, move.to());

    // 王手か否か
    add_feature(kGivesCheck());

    // 有効王手（相手から取り返されない位置にする王手）か否か
    if (neighborhood8_bb(opp_ksq).test(move.to())) {
      // 8近傍の場合：玉の利き以外に相手の利きがないこと
      if (num_opp_controls == 1) {
        add_feature(kEffectiveCheck());
      }
    } else {
      // 8近傍以外の場合：相手の利きが全くないこと
      if (num_opp_controls == 0) {
        add_feature(kEffectiveCheck());
      }
    }

    // 開き王手か否か
    if (   !move.is_drop()
        && pos.discovered_check_candidates().test(move.from())
        && !line_bb(move.from(), opp_ksq).test(move.to())) {
      add_feature(kDiscoveredCheck());
    }

#if 0
    // TODO 除去（一致率マイナス）
    // ただ捨ての王手か否か
    const int num_own_controls = ext_board.num_controls(kColor, move.to());
    if (move.is_drop() && num_opp_controls != 0 && num_own_controls == 0) {
      add_feature(kSacrificeCheck());
    }
#endif

    // 送りの手筋の王手（玉が取り返すと、相手の駒を取れるかどうか）[取れる相手の駒の種類]
    if (   pos_info.opponent_king_neighborhoods8.test(move.to())
        && pos.num_controls(kColor, move.to()) == 0
        && pos_info.opponent_king_neighborhoods8.test(pos.pieces(~kColor))) {
      assert(move.is_drop());
      // 玉しか利きがついていない駒を目標に定める
      EightNeighborhoods own_controls = ext_board.GetEightNeighborhoodControls(kColor, opp_ksq);
      EightNeighborhoods opp_controls = ext_board.GetEightNeighborhoodControls(~kColor, opp_ksq);
      Bitboard own_attacks = direction_bb(opp_ksq, own_controls.more_than(0));
      Bitboard opp_defenses = direction_bb(opp_ksq, opp_controls.more_than(1));
      Bitboard target = (pos.pieces(~kColor) & own_attacks).andnot(opp_defenses);
      // 玉しか利きがない駒があれば、玉の利きが外れるか否かを調べる
      if (target.any()) {
        Bitboard old_king_attacks = pos_info.opponent_king_neighborhoods8;
        Bitboard new_king_attacks = neighborhood8_bb(move.to());
        Bitboard no_king_attack = old_king_attacks.andnot(new_king_attacks);
        (target & no_king_attack).ForEach([&](Square sq) {
          add_feature(kSendOffCheck(pos.piece_on(sq).type()));
        });
      }
    }
  }

  //
  // 5. 王手回避手に関する特徴
  //
  if (pos.in_check()) {
    const Square checker_sq = pos.checkers().first_one();

    // 王手を回避する手か否か
    add_feature(kIsEvasion());

    // 王手している駒を取る手（駒の種類で場合分け）
    if (move.to() == checker_sq) {
      PieceType checker_type = pos.piece_on(checker_sq).type();
      add_feature(kCaptureChecker(checker_type));
    }

    if (piece_type == kKing) {
      // 玉が逃げる手の場合に、玉が逃げる方向
      Square dst = move.to().relative_square(kColor);
      Square src = move.from().relative_square(kColor);
      int delta_y = dst.rank() - src.rank();
      add_feature(kEvasionDirection(delta_y));
    } else if (!move.is_capture()) {
      // 合駒の場合に、合駒と玉との距離（1から8まで）
      int distance = Square::distance(own_ksq, move.to());
      add_feature(kInterceptionDistance(distance));

#if 0
      // TODO 除去？（一致率マイナス）
      // 合駒の場合に、王手している相手の駒の種類
      PieceType checker_type = pos.piece_on(checker_sq).type();
      add_feature(kCheckerType(checker_type));
#endif
    }
  }

  //
  // 6. 攻める手に関する特徴
  //
  {
    // 利きを付けた駒の種類 [利きを付けた駒][味方の利きの有無][敵の利きの有無]
    (attacks & pos.pieces()).ForEach([&](Square sq) {
      Piece piece = pos.piece_on(sq);
      if (kColor == kWhite) piece = piece.opponent_piece(); // 先手視点にする
      bool own_control = ext_board.num_controls(kColor, sq) != 0;
      bool opp_control = ext_board.num_controls(~kColor, sq) != 0;
      add_feature(kAttackToPiece(piece, own_control, opp_control));
    });

    // 利きを付けた相手の駒を調べる
    PieceType best_pt = kNoPieceType, second_best_pt = kNoPieceType;
    Score best_value = kScoreZero, second_best_value = kScoreZero;
    bool best_pt_is_supported = false, second_pt_is_supported = false;
    (attacks & pos.pieces(~kColor)).ForEach([&](Square sq) {
      PieceType attacked_pt = pos.piece_on(sq).type();
      Score attacked_piece_value = Material::exchange_value(attacked_pt);
      bool is_supported = ext_board.num_controls(~kColor, sq) > 0;

      // 最も価値の高い駒/２番めに価値の高い駒を更新する
      // 注：最も価値の高い駒の種類と、２番めに価値が高い駒の種類が同じ場合もあるので、「等号付き」で比較
      if (attacked_piece_value >= best_value) {
        second_best_pt = best_pt;
        second_best_value = best_value;
        second_pt_is_supported = best_pt_is_supported;
        best_pt = attacked_pt;
        best_value = attacked_piece_value;
        best_pt_is_supported = is_supported;
      } else if (attacked_piece_value > best_value) {
        second_best_pt = attacked_pt;
        second_best_value = attacked_piece_value;
        second_pt_is_supported = is_supported;
      }

#if 0
      // TODO 除去（一致率がマイナス）
      // そっぽ手判定 [相手玉との距離][利きを付けた駒の種類]
      int distance = Square::distance(opp_ksq, sq);
      add_feature(kDistanceBetweenKingAndVictim(distance, attacked_pt));
#endif
    });

    // 当たりを付けた駒のうち、最も価値が高い/２番めに価値が高い相手の駒（両取りを意識）
    add_feature(kMostValuableAttack1(best_pt, best_pt_is_supported));
    add_feature(kMostValuableAttack2(second_best_pt, second_pt_is_supported));

    // ピンしている駒に対する当たり [利きを付けた駒の種類]
    if (attacks.test(pos_info.opponent_pinned_pieces)) {
      Bitboard target = attacks & pos_info.opponent_pinned_pieces;
      PieceType victim = pos.piece_on(target.first_one()).type();
      add_feature(kAttackToPinnedPiece(victim));
    }

    // 空き当たり [駒の種類][相手の利きの有無]
    if (!move.is_drop()) {
      for (Direction attack_dir : ext_board.long_controls(kColor, move.from())) {
        Square delta = Square::direction_to_delta(attack_dir);
        for (Square s = move.from() + delta;
            s != move.to() && s.IsOk() && Square::distance(s, s - delta) <= 1;
            s += delta) {
          Piece attacked_piece = pos.piece_on(s);
          if (attacked_piece != kNoPiece) {
            if (attacked_piece.color() == ~kColor) {
              bool target_is_supported = ext_board.num_controls(~kColor, s) > 0;
              add_feature(kDiscoveredAttack(attacked_piece.type(), target_is_supported));
            }
            break;
          }
        }
      }
    }

    // 大駒を敵陣に打ち込む手 [min(安全に成れるマスの数, 7)]
    if (   move.is_drop()
        && (piece_type == kRook || piece_type == kBishop)
        && move.to().is_promotion_zone_of(kColor)) {
      Bitboard next_moves = attacks.andnot(pos.pieces(kColor)); // 次に移動できるマス
      Bitboard safe_moves = next_moves.andnot(pos_info.attacked_squares);
      int num_safe_moves = std::min(safe_moves.count(), 7);
      add_feature(kDropToOpponentArea(num_safe_moves));
    }

#if 0
    // FIXME 除去（-0.007%）
    // 駒を成る手 [相手玉との距離]
    if (move.is_promotion()) {
      int distance_to_king = Square::distance(opp_ksq, move.to());
      add_feature(kDistanceToKingWhenPromotion(distance_to_king));
    }
#endif

    // 相手の歩のない筋で、味方飛香の利きにある歩を５段目より前に動かす手
    if (   !move.is_drop()
        && piece_type == kPawn
        && relative_rank(kColor, move.to().rank()) < kRank5
        && !pos.pieces(~kColor, kPawn).test(file_bb(move.to().file()))) {
      DirectionSet long_controls = ext_board.long_controls(kColor, move.from());
      if (long_controls.test(kColor == kBlack ? kDirN : kDirS)) {
        add_feature(kAdvancingPawn());
      }
    }
  }

  //
  // 7. 受ける手に関する特徴
  //
  {
    if (!move.is_drop()) {
      // 取られそうな最高の駒を逃げる手
      if (pos_info.most_valuable_threatened_piece.test(move.from())) {
        add_feature(kDefendMostValuablePiece());
      }

      // 直前の相手の手で当たりにされた最高の駒が逃げる手（相手が動かした駒の種類で場合分け）
      if (pos_info.pieces_attacked_by_last_move.test(move.from())) {
        PieceType attacker_type = previous_move1.piece_type();
        add_feature(kDefendPiecesAttackedByLastMove(attacker_type));
      }
    }

#if 0
    // TODO 除去（一致率マイナス）
    // 取られそうな最高の駒に、利きを足して守る手（取られそうな駒の種類、味方の利きの有無で場合分け）
    if (attacks.test(pos_info.most_valuable_threatened_piece)) {
      Bitboard target = supported_squares & pos_info.most_valuable_threatened_piece;
      Square threatned_sq = target.first_one();
      PieceType threatened_pt = pos.piece_on(threatned_sq).type();
      bool already_supported = ext_board.num_controls(kColor, threatned_sq) != 0;
      add_feature(kSupportPiecesAttackedByLastMove(threatened_pt, already_supported));
    }
#endif

    // 直前の相手の手で取られそうな最高の駒に、合駒をして守る手（相手が動かした駒の種類で場合分け）
    if (pos_info.intercept_attacks_by_last_move.test(move.to())) {
      PieceType attacker_type = previous_move1.piece_type();
      add_feature(kInterceptAttacks(attacker_type));
    }

    // 直前の相手の手で取られそうな最高の駒に、利きを足して守る手（相手が動かした駒の種類、味方の利きの有無で場合分け）
    if (attacks.test(pos_info.pieces_attacked_by_last_move)) {
      Bitboard target = attacks & pos_info.pieces_attacked_by_last_move;
      Square attacked_sq = target.first_one();
      PieceType attacker_type = previous_move1.piece_type();
      bool already_supported = ext_board.num_controls(kColor, attacked_sq) != 0;
      add_feature(kSupportPiecesAttackedByLastMove(attacker_type, already_supported));
    }
  }

  //
  // 8. 自玉周り
  //
  {
#if 0
    // TODO 除去 一致率ほぼ変化なし
    // 自玉の８近傍に利きをつけている相手の飛角に合駒をする手 [打つ手かどうか]
    if (pos_info.intercept_attacks_to_our_castle.test(move.to())) {
      if (move.is_drop()) {
        add_feature(kInterceptAttacksToOurCastle(true));
      } else if (!neighborhood8_bb(own_ksq).test(move.from())) {
        add_feature(kInterceptAttacksToOurCastle(false));
      }
    }
#endif

    // 自玉の８近傍に利きを足す手（味方の利きよりも、相手の利きのほうが多い場合）[打つ手かどうか]
    if (attacks.test(pos_info.dangerous_king_neighborhood_squares)) {
      if (move.is_drop()) {
        add_feature(kReinforceOurCastle(true));
      } else {
        // 打つ手ではない場合には、利きを新たに追加する場合のみを選ぶ
        Bitboard old_attacks = AttacksFrom(move.piece(), move.from(), pos.pieces());
        if (!old_attacks.test(pos_info.dangerous_king_neighborhood_squares)) {
          add_feature(kReinforceOurCastle(false));
        }
      }
    }

    // 相手が持ち駒で自玉に有効王手をかけることができるマスに、先に駒を打って埋める手
    if (   pos_info.opponent_effective_drop_checks.test(move.to())
        && move.is_drop()
        && pos_info.opponent_effective_drop_checks.count() == 1) { // 唯一の有効王手の場合
      add_feature(kObstructOpponentEffectiveChecks());
    }
  }

  //
  // 9. 相手玉周り
  //
  {
    // 相手玉周りに利きをつける手（8近傍、24近傍）
    if (attacks.test(pos_info.opponent_king_neighborhoods8)) {
      add_feature(kAttackToKingNeighborhood8());
    }
    if (attacks.test(pos_info.opponent_king_neighborhoods24)) {
      add_feature(kAttackToKingNeighborhood24());
    }

    // 相手玉の24近傍にある、相手の金・銀に当たりをかける手
    if (attacks.test(pos_info.opponent_king_neighborhood_golds)) {
      add_feature(kAttackToGoldNearKing());
    }
    if (attacks.test(pos_info.opponent_king_neighborhood_silvers)) {
      add_feature(kAttackToSilverNearKing());
    }
  }

  //
  // 駒ごとの手筋
  //
  switch (piece_type) {
    //
    // 10. 歩の手筋
    //
    case kPawn: {
      const auto relative_dir = [](Direction d) -> Direction {
        return kColor == kBlack ? d : inverse_direction(d);
      };
      const Square delta_n = Square::direction_to_delta(relative_dir(kDirN));
      const Square delta_s = Square::direction_to_delta(relative_dir(kDirS));
      const Square north_sq = move.to() + delta_n;
      const Square south_sq = move.to() + delta_s;

      if (move.is_drop()) {
        const Bitboard occ = pos.pieces();
        const DirectionSet opp_long_controls = ext_board.long_controls(~kColor, move.to());

        // 直射止めの歩（相手の飛・香の、自陣に対する利きを遮る手）
        if (   opp_long_controls.test(relative_dir(kDirS))
            && move.to().rank() == relative_rank(kColor, kRank7)) {
          add_feature(kInterceptAttacksToOurAreaByPawn());
        }

#if 0
        // FIXME 一致率マイナス
        // 垂れ歩（２〜４段目に、敵の駒に当てないで歩を打つ手）[ひとつ上の味方の利きの有無][同・敵の利きの有無]
        if (   rank_bb<kColor, 2, 4>().test(move.to())
            && !step_attacks_bb(Piece(kColor, kPawn), move.to()).test(pos.pieces())) {
          bool north_supported = ext_board.num_controls(kColor, north_sq) > 0;
          bool north_attacked = ext_board.num_controls(~kColor, north_sq) > 0;
          add_feature(kDanglingPawn(north_supported, north_attacked));
        }
#endif

        // 底歩を打って、飛車の横利きを止める手 [上にある駒]
        if (   move.to().rank() == relative_rank(kColor, kRank9)
            && pos.piece_on(north_sq) != kNoPiece
            && pos.piece_on(north_sq).is(kColor)
            && (opp_long_controls.test(kDirE) || opp_long_controls.test(kDirW))) {
          add_feature(kAnchorByPawn(pos.piece_on(north_sq).type()));
        }

        // 歩を打って、飛香の利きを止める手 [何枚歩を打てば利きが止まるか] （連打の歩を意識）
        constexpr BitSet<Piece, 32> kLanceRookDragon(Piece(~kColor, kLance),
                                                     Piece(~kColor, kRook),
                                                     Piece(~kColor, kDragon));
        if (   kLanceRookDragon.test(pos.piece_on(north_sq))
            && pos.num_controls(kColor, move.to()) == 0
            && pos.hand(kColor).count(kPawn) >= 2
            && move.to().rank() != relative_rank(kColor, kRank9)) {
          Bitboard opp_attacks = lance_attacks_bb(move.to(), occ, ~kColor);
          Bitboard block_point = pos_info.defended_squares.andnot(pos.pieces());
          if (opp_attacks.test(block_point)) { // 利きを止める場所があるか
            int min_distance = 8;
            (opp_attacks & pos_info.defended_squares).ForEach([&](Square sq) {
              if (pos.num_controls(~kColor, sq) == 1) {
                int distance = Square::distance(north_sq, sq);
                if (distance < min_distance) {
                  min_distance = distance;
                }
              }
            });
            if (pos.hand(kColor).count(kPawn) >= min_distance) {
              add_feature(kSuccessivePawnDrops(min_distance));
            }
          }
        }

#if 0
        // FIXME 出現数が少なすぎて、一致率に影響なし
        // 蓋歩（敵の飛車の退路を断つ歩）
        if (   rank_bb<kColor, 5, 8>().test(move.to())
            && pos.piece_on(south_sq) == Piece(~kColor, kRook)) {
          Bitboard new_occ = pos.pieces() | square_bb(move.to());
          Bitboard rook_moves = rook_attacks_bb(south_sq, new_occ);
          Bitboard blocked = pos.pieces(~kColor) | pos_info.defended_squares;
          Bitboard rook_evasions = rook_moves.andnot(blocked);
          if (rook_evasions.none()) {
            add_feature(kCutOffTheRookRetreatByPawn());
          }
        }
#endif

#if 0
        // FIXME 一致率マイナス
        // 控えの歩（自陣において、敵駒に当てないで打つ歩）[ひとつ上の味方の利きの有無][同・敵の利きの有無]
        if (   rank_bb<kColor, 8, 9>().test(move.to())
            && pos.piece_on(north_sq) == kNoPiece) {
          bool own_control = pos.num_controls(kColor, north_sq) > 0;
          bool opp_control = pos.num_controls(~kColor, north_sq) > 0;
          add_feature(kPawnTowardsTheRear(own_control, opp_control));
        }
#endif
      } else if (move.is_promotion()) {
        // 成り捨ての歩 [相手玉との距離]
        if (   ext_board.num_controls(kColor, move.to()) == 1
            && !ext_board.long_controls(kColor, move.to()).test(relative_dir(kDirN))
            && ext_board.num_controls(~kColor, move.to()) != 0) {
          int distance_to_king = Square::distance(opp_ksq, move.to());
          add_feature(kPawnSacrificePromotion(distance_to_king));
        }
      } else { // 打つ手でも、成る手でもない場合
        if (move.to().file() == kFile1 || move.to().file() == kFile9) {
          // 端歩を突く手 [玉とのX距離]
          int x_distance = std::abs(move.to().file() - opp_ksq.file());
          add_feature(kEdgePawnPush(x_distance));

#if 0
          // FIXME 一致率 -0.02%
          // 端攻めの歩 [min(持ち駒の歩の枚数, 5)]
          int num_hand_pawns = pos.hand(kColor).count(kPawn);
          add_feature(kEdgePawnAttackWithHandPawns(std::min(num_hand_pawns, 5)));

          // FIXME 一致率 -0.03%
          // 端攻めの歩 [min(持ち駒の桂の枚数, 3)]
          int num_hand_knights = pos.hand(kColor).count(kKnight);
          add_feature(kEdgePawnAttackWithHandKnights(std::min(num_hand_knights, 3)));
#endif
        }
      }

      // 継ぎ歩から垂れ歩（同〜と取られたら、次に相手の利きのない場所に垂れ歩を打てる手）
      if (   rank_bb<kColor, 2, 4>().test(move.to())
          && pos.piece_on(north_sq) == Piece(~kColor, kPawn)
          && pos.hand(kColor).count(kPawn) >= (move.is_drop() ? 2 : 1)
          && pos.num_controls(~kColor, north_sq) == 0) {
        add_feature(kJoiningPawnBeforeDanglingPawn());
      }

      // 銀バサミの歩
      Bitboard side_bb = rank_bb(move.to().rank()) & neighborhood8_bb(move.to());
      if (   rank_bb<4, 6>().test(move.to())
          && side_bb.test(pos.pieces(~kColor, kSilver))
          && pos.num_controls(~kColor, move.to()) == 0) {
        Bitboard side_silvers = side_bb & pos.pieces(~kColor, kSilver);
        side_silvers.ForEach([&](Square sq) {
          Bitboard silver_moves = step_attacks_bb(Piece(~kColor, kSilver), sq);
          Bitboard blocked = pos.pieces(~kColor) | pos_info.defended_squares;
          if (silver_moves.andnot(blocked).none()) { // 相手の銀が逃げるマスがない場合
            add_feature(kSilverPincerPawn());
          }
        });
      }

      // 桂頭の歩 [相手の桂馬が逃げられるマスの数(0~2)]
      if (   move.to().rank() == relative_rank(kColor, kRank4)
          && pos.piece_on(north_sq) == Piece(~kColor, kKnight)) {
        Bitboard knight_moves = step_attacks_bb(Piece(~kColor, kKnight), north_sq);
        Bitboard blocked = pos.pieces(~kColor) | pos_info.defended_squares;
        int num_knight_evasions = knight_moves.andnot(blocked).count();
        add_feature(kAttackToKnightByPawn(num_knight_evasions));
      }

      // 同歩と取られたら、次に桂頭に歩を打てる手
      if (   move.to().rank() == relative_rank(kColor, kRank5)
          && pos.piece_on(north_sq + delta_n) == Piece(~kColor, kKnight)
          && pos.piece_on(north_sq) == Piece(~kColor, kPawn)
          && pos.hand(kColor).count(kPawn) >= (move.is_drop() ? 2 : 1)
          && pos.num_controls(kColor, move.to()) == 0
          && pos.num_controls(~kColor, north_sq) == 0) {
        add_feature(kPawnSacrificeBeforeAttackToKnight());
      }

      // 味方の飛角の利きを止めている敵の歩に対して、歩で当たりをかける手 [飛角の隠れた利きが付いている敵の駒][その駒への敵の利きの有無]
      if (   move.to().rank() != relative_rank(kColor, kRank1)
          && pos.piece_on(north_sq) == Piece(~kColor, kPawn)) {
        DirectionSet long_controls = pos.long_controls(kColor, north_sq);
        DirectionSet diagonal_or_vertical = long_controls.reset(kDirN).reset(kDirS);
        for (Direction dir : diagonal_or_vertical) {
          Square delta = Square::direction_to_delta(dir);
          for (Square sq = north_sq + delta;
              sq.IsOk() && Square::distance(sq, sq - delta); sq += delta) {
            Piece piece = pos.piece_on(sq);
            if (piece != kNoPiece) {
              bool own_control = pos.num_controls(kColor, sq) > 0;
              bool opp_control = pos.num_controls(~kColor, sq) > 0;
              if (kColor == kWhite) piece = piece.opponent_piece();
              add_feature(kAttackToObstructingPawn(piece, own_control, opp_control));
              break;
            }
          }
        }
      }
    }
      break;

    //
    // 11. 香の手筋
    //
    case kLance: {
      const Bitboard opp_non_pawn_pieces = pos.pieces(~kColor).andnot(pos.pieces(kPawn));

      // 香車の利きの先にある駒を調べる
      if (attacks.test(opp_non_pawn_pieces)) {
        Square target_sq1 = (attacks & opp_non_pawn_pieces).first_one();
        PieceType target_pt1 = pos.piece_on(target_sq1).type();

        // 敵の駒に利きを付ける手 [駒の種類][相手が歩で受けられるか否か]（底歩に対する香打ち等を意識）
        Bitboard pawn_files = Bitboard::FileFill(pos.pieces(~kColor, kPawn));
        bool pawn_block_possible =   pos.hand(~kColor).has(kPawn)
                                  && !pawn_files.test(move.to());
        add_feature(kAttackByLance(target_pt1, pawn_block_possible));

        // 田楽の香（歩の受けが利かない手に限る
        if (!pawn_block_possible) {
          Bitboard xray_attacks = lance_attacks_bb(target_sq1, pos.pieces(), kColor);
          if (xray_attacks.test(opp_non_pawn_pieces)) {
            // 香車の影の利きにある敵の駒 [相手の駒の種類][その駒に相手の利きがあるか]
            Square target_sq2 = (xray_attacks & opp_non_pawn_pieces).first_one();
            PieceType target_pt2 = pos.piece_on(target_sq2).type();
            add_feature(kXrayAttackByLance(target_pt2));
            // 田楽の香 [串刺しにした２枚の駒のうち、安い方の敵の駒]
            Score value1 = Material::exchange_value(target_pt1);
            Score value2 = Material::exchange_value(target_pt2);
            add_feature(kLanceSkewer(value1 < value2 ? target_pt1 : target_pt2));
          }
        }
      }

      // 香浮き（飛車が回り込める場合）
      if (   !move.is_drop()
          && move.from().relative_square(kColor).rank() >= kRank7
          && Square::distance(move.from(), move.to()) <= 1) {
        DirectionSet long_controls = pos.long_controls(kColor, move.from());
        if (long_controls.test(kDirE) || long_controls.test(kDirW)) {
          add_feature(kFloatingLance());
        }
      }
    }
      break;

    //
    // 12. 桂の手筋
    //
    case kKnight: {
      // 控えの桂（駒損しない、打つ手のみ）[桂が跳ねれば当たりになる相手の駒]
      if (move.is_drop() && see_sign >= 0) {
        attacks.ForEach([&](Square sq) {
          Bitboard next_attacks = step_attacks_bb(Piece(kColor, kKnight), sq);
          (next_attacks & pos.pieces(~kColor)).ForEach([&](Square attack_sq) {
            PieceType pt_to_be_attacked = pos.piece_on(attack_sq).type();
            add_feature(kKnightTowardsTheRear(pt_to_be_attacked));
          });
        });
      }
    }
      break;

    default:
      break;
  }

  return feature_list;
}

MoveFeatureList ExtractMoveFeatures(Move move, const Position& pos,
                                    const PositionInfo& pos_info) {
  return pos.side_to_move() == kBlack
       ? ExtractMoveFeatures<kBlack>(move, pos, pos_info)
       : ExtractMoveFeatures<kWhite>(move, pos, pos_info);
}

PositionInfo::PositionInfo(const Position& pos, const HistoryStats& history_stats,
                           const GainsStats& gains_stats)
    : history(history_stats), gains(gains_stats) {
  const Color stm = pos.side_to_move();
  const Square own_ksq = pos.king_square(stm);
  const Square opp_ksq = pos.king_square(~stm);
  const Bitboard own_pieces = pos.pieces(stm);
  const Bitboard opp_pieces = pos.pieces(~stm);

  // 利きが付いているマス
  attacked_squares = pos.extended_board().GetControlledSquares(~stm);
  defended_squares = pos.extended_board().GetControlledSquares(stm);

  auto find_most_valuable_pieces = [&](Bitboard pieces) -> Bitboard {
    Score best_value = kScoreZero;
    PieceType best_type = kNoPieceType;
    pieces.ForEach([&](Square sq) {
      PieceType pt = pos.piece_on(sq).type();
      Score piece_value = Material::exchange_value(pt);
      if (piece_value > best_value) {
        best_value = piece_value;
        best_type = pt;
      }
    });
    return best_type == kNoPieceType ? Bitboard() : (pieces & pos.pieces(best_type));
  };

  // 当たりをかけている、最も価値の高い敵の駒
  most_valuable_victim = find_most_valuable_pieces(defended_squares & opp_pieces);

  // ピンしている相手の駒
  {
    // 1. ピンしている味方の駒の候補を求める
    Bitboard pinners;
    pinners |= max_attacks_bb(kBlackRook, opp_ksq) & pos.pieces(kRook, kDragon);
    pinners |= max_attacks_bb(kBlackBishop, opp_ksq) & pos.pieces(kBishop, kHorse);
    pinners |= max_attacks_bb(Piece(stm, kLance), opp_ksq) & pos.pieces(kLance);
    pinners = (pinners & own_pieces).andnot(neighborhood8_bb(opp_ksq));

    // 2. ピンされている相手の駒を求める
    pinners.ForEach([&](Square pinner_sq) {
      Bitboard obstructing_pieces = pos.pieces() & between_bb(opp_ksq, pinner_sq);
      if (obstructing_pieces.count() == 1) {
        opponent_pinned_pieces |= (obstructing_pieces & opp_pieces);
      }
    });
  }

  // 当たりになっている味方の駒
  threatened_pieces = attacked_squares & own_pieces;

  // 当たりになっている駒で、最も価値の高い味方の駒
  most_valuable_threatened_piece = find_most_valuable_pieces(threatened_pieces);

  if (pos.last_move().is_real_move()) {
    Move last_move = pos.last_move();
    Piece attacker = last_move.piece();
    Square attacker_sq = last_move.to();;

    // 直前に動いた駒で取られそうな、最も価値の高い味方の駒
    Bitboard attacked_by_last_move = AttacksFrom(attacker, attacker_sq, pos.pieces());
    pieces_attacked_by_last_move =
        find_most_valuable_pieces(attacked_by_last_move & own_pieces);

    // 直前に動いた駒で取られそうな味方の駒を、合駒して守る手
    pieces_attacked_by_last_move.ForEach([&](Square sq) {
      intercept_attacks_by_last_move |= between_bb(attacker_sq, sq);
    });
  }

  {
    // 味方の利きについては、玉の利きを除くため、利き数から1を引く
    const ExtendedBoard& ext_board = pos.extended_board();
    EightNeighborhoods own_controls = ext_board.GetEightNeighborhoodControls(stm, own_ksq).Subtract(1);
    EightNeighborhoods opp_controls = ext_board.GetEightNeighborhoodControls(~stm, own_ksq);

    // 自玉の8近傍で、敵の利きがあり、かつ敵の利きが味方の利き数を上回っているマス
    for (int i = 0; i < 8; ++i) {
      Direction dir = static_cast<Direction>(i);
      Square delta = Square::direction_to_delta(dir);
      Square sq = own_ksq + delta;
      if (sq.IsOk() && Square::distance(own_ksq, sq) <= 1) {
        if (opp_controls.at(dir) > own_controls.at(dir)) {
          dangerous_king_neighborhood_squares.set(sq);
        }
      }
    }

    // 相手が持ち駒で自玉に有効王手をかけることができるマス
    if (pos.hand(~stm).has_any_piece_except(kKnight)) {
      Bitboard defended = direction_bb(own_ksq, own_controls.more_than(0));
      Bitboard drop_targets = attacked_squares.andnot(defended | pos.pieces());
      if (neighborhood8_bb(own_ksq).test(drop_targets)) {
        Hand h = pos.hand(~stm);
        Bitboard checks;
        if (h.has(kPawn  )) checks |= min_attacks_bb(Piece(stm, kPawn  ), own_ksq);
        if (h.has(kLance )) checks |= min_attacks_bb(Piece(stm, kLance ), own_ksq);
        if (h.has(kSilver)) checks |= min_attacks_bb(Piece(stm, kSilver), own_ksq);
        if (h.has(kGold  )) checks |= min_attacks_bb(Piece(stm, kGold  ), own_ksq);
        if (h.has(kBishop)) checks |= min_attacks_bb(Piece(stm, kBishop), own_ksq);
        if (h.has(kRook  )) checks |= min_attacks_bb(Piece(stm, kRook  ), own_ksq);
        opponent_effective_drop_checks = (drop_targets & checks);
      }
    }
    if (pos.hand(~stm).has(kKnight)) {
      Bitboard knight_checks = step_attacks_bb(Piece(stm, kKnight), own_ksq);
      Bitboard targets = attacked_squares.andnot(defended_squares | pos.pieces());
      opponent_effective_drop_checks |= (targets & knight_checks);
    }
  }

  // 敵玉の8近傍、24近傍
  opponent_king_neighborhoods8 = neighborhood8_bb(opp_ksq);
  opponent_king_neighborhoods24 = neighborhood24_bb(opp_ksq);

  // 敵玉24近傍にある敵の金・銀
  opponent_king_neighborhood_golds = pos.golds(~stm) & opponent_king_neighborhoods24;
  opponent_king_neighborhood_silvers = pos.pieces((~stm, kSilver)) & opponent_king_neighborhoods24;
}
