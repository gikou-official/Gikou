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

typedef Number<0, 152> SquareRelation;

ValarrayKeyChain key_chain;

//
// 探索中の情報 or 連続値を取る特徴
//
// killer move
// history value（-1から+1までの連続値）
// counter move history value（-1から+1までの連続値）
// follow-up move history value（-1から+1までの連続値）
// evaluation gain（-1＜龍損＞から+1＜龍得＞までの連続値）
// SEE値（-1＜龍損＞から+1＜龍得＞までの連続値）
// Global SEE値（-1＜龍損＞から+1＜龍得＞までの連続値）
// 駒取りの脅威（-1＜龍損＞から0＜損なし＞までの連続値）

//
// バイアス項
//
// 全ての手用のバイアス項
const auto kBiasTerm = key_chain.CreateKey();
// 静かな手用のバイアス項
const auto kBiasTermOfQuietMove = key_chain.CreateKey();

//
// 全ての駒種に共通する特徴
//
// 手を指した後、相手にタダ取りされる最高の駒
const auto kCaptureThreatAfterMove = key_chain.CreateKey<PieceType>();
// 手を指した後、飛車にタダで成りこまれる
const auto kRookPromotionThreatAfterMove = key_chain.CreateKey();
// 手を指した後、角にタダで成りこまれる
const auto kBishopPromotionThreatAfterMove = key_chain.CreateKey();
// 手を指した後の、敵玉の自由度
const auto kOppKingFreedomAfterMove = key_chain.CreateKey<Number<1, 6>>();

//
// 取る手
//
// 取れる最高の駒を取る手（SEE>=0）
const auto kIsCaptureOfMostValuablePiece = key_chain.CreateKey();
// 直前に動いた駒の取り返し（SEE>=0）
const auto kIsRecapture = key_chain.CreateKey();
// 2手前に動いた駒で駒を取る手（SEE>=0）
const auto kIsCaptureWithLastMovedPiece = key_chain.CreateKey();
// 駒得の取る手 [歩何枚分の得か]
const auto kGoodCapture = key_chain.CreateKey<Number<0, 7>>();
// 駒交換 [交換する駒]
const auto kEqualCapture = key_chain.CreateKey<PieceType>();
// 駒損の取る手 [歩何枚分の損か]
const auto kBadCapture = key_chain.CreateKey<Number<0, 7>>();
// [取った駒]
const auto kCapturedPiece = key_chain.CreateKey<PieceType>();
// [動かした駒]
const auto kMovedPieceToCapture = key_chain.CreateKey<PieceType>();
// [動かした駒][取った駒]
const auto kMovedPieceAndCapturedPiece = key_chain.CreateKey<PieceType, PieceType>();
// [取った駒][段]
const auto kCapturedPieceAndRank = key_chain.CreateKey<PieceType, Rank>();
// [取った駒][自玉との距離]
const auto kCapturedPieceAndDistanceToOwnKing = key_chain.CreateKey<PieceType, Number<1,8>>();
// [取った駒][敵玉との距離]（歩・香・桂を取る手について）
const auto kCapturedPieceAndDistanceToOppKing = key_chain.CreateKey<PieceType, Number<1,8>>();
// [取った駒][取った駒を持ち駒に何枚持っているか]
const auto kNumHandPiecesBeforeCapture = key_chain.CreateKey<PieceType, Number<0, 1>>();

//
// 成る手
//
// 成る手 [動かした駒]
const auto kIsPromotion = key_chain.CreateKey<PieceType>();
// [動かした駒][敵玉との距離]
const auto kDistanceToOppKingAfterPromotion = key_chain.CreateKey<PieceType, Number<1, 8>>();
// [動かした駒][移動元の段]（SSE>=0のみ）
const auto kRankBeforePromotion = key_chain.CreateKey<PieceType, Rank>();
// [動かした駒][移動先の段]（SEE>=0のみ）
const auto kRankAfterPromotion = key_chain.CreateKey<PieceType, Rank>();
// 駒損の成る手 [歩何枚分の損か]
const auto kMaterialLossAfterPromotion = key_chain.CreateKey<Number<0, 7>>();

//
// 王手（打つ手か否かで場合分け？）
//
// a. 開き王手（SEE値で場合分けしない） [王手した駒]
const auto kIsDiscoveredCheck = key_chain.CreateKey<PieceType>();
// b. SEE>=0の王手 [王手した駒]
const auto kIsGoodCheck = key_chain.CreateKey<PieceType>();
// c. SEE<0の王手 [王手した駒]
const auto kIsBadCheck = key_chain.CreateKey<PieceType>();
// d. タダ取りされる王手 [王手した駒]
const auto kIsSacrificeCheck = key_chain.CreateKey<PieceType>();
// 駒損の王手 [歩何枚分の損か]
const auto kMaterialLossAfterCheck = key_chain.CreateKey<Number<0, 7>>();
// 王手後の敵玉の自由度 [敵玉の自由度]
const auto kOppKingFreedomAfterGoodCheck = key_chain.CreateKey<Number<1, 6>>();
const auto kOppKingFreedomAfterBadCheck = key_chain.CreateKey<Number<1, 6>>();
const auto kOppKingFreedomAfterSacrificeCheck = key_chain.CreateKey<Number<1, 6>>();

//
// 王手回避手
//
// 取る手の場合に、動かす駒
const auto kIsCaptureOfChecker = key_chain.CreateKey<PieceType>();
// 玉が逃げる手の場合に、移動後の玉の自由度（1から6まで）
const auto kKingFreedomAfterEvasion = key_chain.CreateKey<Number<1, 6>>();
// 合駒の場合に、合駒する駒の種類
const auto kIsInterception = key_chain.CreateKey<PieceType>();
// 合駒の場合に、合駒と玉との距離（1から3まで）
const auto kDistanceBetweenKingAndInterceptor = key_chain.CreateKey<Number<1, 3>>();
// 中合いの場合に、合駒する駒の種類
const auto kIsChuai = key_chain.CreateKey<PieceType>();
// 中合いの場合に、合駒と玉との距離（2から4まで）
const auto kDistanceBetweenKingAndChuaiPiece = key_chain.CreateKey<Number<2, 4>>();

//
// 攻める手
//
// 長い利きによる当たり [動かした駒][利きを付けた駒][味方の利きの有無][敵の利きの有無]
const auto kDstLongAttack = key_chain.CreateKey<PieceType, Piece, Number<0, 1>, Number<0, 1>>();
const auto kSrcLongAttack = key_chain.CreateKey<PieceType, Piece, Number<0, 1>, Number<0, 1>>();
// 空き当たり [動かした駒][利きを付けた駒][味方の利きの有無][敵の利きの有無]
const auto kDiscoveredAttack = key_chain.CreateKey<PieceType, Piece, Number<0, 1>, Number<0, 1>>();
// ピンしている駒に対する当たり [動かした駒][利きを付けた駒]
const auto kAttackToPinnedPieces = key_chain.CreateKey<PieceType, PieceType>();
// 両取りの手 [指した駒][当たりを付けた駒のうち、２番目に価値が高い相手の駒]
const auto kDoubleAttack = key_chain.CreateKey<PieceType, PieceType>();
// 同〜と取り返してきたら取れる最高の駒 [犠牲にする駒][取れる最高の相手駒]
const auto kCaptureAfterSacrifice = key_chain.CreateKey<PieceType, PieceType>();

//
// 受ける手
//
// 取られそうな最高の駒を逃げる手（SSE>=0）
const auto kEscapeMoveOfMostValuablePiece = key_chain.CreateKey();
// 取られそうな駒を逃げる手（SEE>=0）[動かした駒]
const auto kEscapeMove = key_chain.CreateKey<PieceType>();
// 駒が取られそうな場合に、合駒をして守る手 [合駒した駒][守った駒]
const auto kInterceptionDefense = key_chain.CreateKey<PieceType, PieceType>();

//
// 自玉周り
//
// 自玉の８近傍への飛び利きを遮る手（打つ手のみ） [駒の種類]
const auto kBlockOfLongAttacksToOwnCastle = key_chain.CreateKey<PieceType>();
// 自玉の８近傍に利きを足す手（打つ手のみ）（味方の利きより相手の利きが多いマスに利きを足す）[駒の種類]
const auto kReinforcementOfOurCastle = key_chain.CreateKey<PieceType>();

//
// 敵玉周り
//
// 敵玉８近傍に利きを足す手（飛び駒のみ）
const auto kAttackToOppKingNeighbor8 = key_chain.CreateKey();

//
// 利き関係
//
// ＜ExtendedBoardを使ったGlobal Evaluation＞
// 新たに利きを付けた相手の駒
const auto kNewAttackToOppPiece = key_chain.CreateKey<PieceType>();
// 新たにひもを付けた味方の駒
const auto kNewTieToOwnPiece = key_chain.CreateKey<PieceType>();
// 利きが外れた相手の駒
const auto kRemovedAttackToOppPiece = key_chain.CreateKey<PieceType>();
// ひもが外れた味方の駒
const auto kRemovedTieToOwnPiece = key_chain.CreateKey<PieceType>();
// 手を指した後、相手がパスした場合、タダ取りできる最高の駒
const auto kNextGoodCapture = key_chain.CreateKey<PieceType>();

//
// 移動手の特徴
//
// 移動方向（前か、横か、後ろか） [指した駒][移動元の段][Y座標の増減]
const auto kDeltaCoordinateY = key_chain.CreateKey<PieceType, Rank, Number<-1, 1>>();
// 5筋とのX距離の増減 [指した駒][5筋とのX距離の増減]
const auto kDeltaDistanceFromCenter = key_chain.CreateKey<PieceType, Number<-1, 1>>();
// そっぽ手判定（自陣の駒） [指した駒][移動元と自玉とのX距離][自玉とのX距離の増減]
const auto kIsGoingAwayFromOwnKing = key_chain.CreateKey<PieceType, Number<0, 7>, Number<-1, 1>>();
// そっぽ手判定（敵陣の駒） [指した駒][移動元と敵玉とのX距離][敵玉とのX距離の増減]
const auto kIsGoingAwayFromOppKing = key_chain.CreateKey<PieceType, Number<0, 7>, Number<-1, 1>>();
// 動くと取られてしまう最高の駒（原因：ピン） [指した駒][取られそうな駒]
const auto kOpponentXrayThreat = key_chain.CreateKey<PieceType, PieceType>();
// 駒の自由度 [指した駒][相手の利きがなく、移動可能なマス]
const auto kDegreeOfLibertyAfterMove = key_chain.CreateKey<PieceType, Number<0, 12>>();
// 2手前に動かした駒を動かす手 [指した駒][元いたマスに戻る手か否か]（SEE=0のみ）
const auto kSuccessiveMoveOfSamePiece = key_chain.CreateKey<PieceType>();
// 自陣の駒打ちのスキを増やしてしまう手 [指す駒][相手の持ち駒][スキが増えるか否か]
const auto kMakesHolesInOurArea = key_chain.CreateKey<PieceType, PieceType, bool>();

//
// 打つ手の特徴
//
// 持ち駒の枚数 [打つ駒][持ち駒の枚数]
const auto kNumHandPieces = key_chain.CreateKey<PieceType, Number<1, 3>>();
// 安全に成れるマスの数 [打つ駒][安全に成れるマスの数]
const auto kNumSafePromotions = key_chain.CreateKey<PieceType, Number<0, 4>>();
// 駒の自由度 [打つ駒][相手の利きがなく、移動可能なマス]
const auto kDegreeOfLibertyAfterDrop = key_chain.CreateKey<PieceType, Number<0, 12>>();

//
// パターン
//
// 駒が安全に移動できるマスのパターン [指した駒][段][安全に移動できるマスのパターン]
const auto kPatternOfSafeMoves = key_chain.CreateKey<PieceType, Rank, DirectionSet>();

//
// 移動先・移動元に関する特徴
// （移動元：old_extended_boardで評価、移動先：new_extended_boardで評価）
//
// 段 [指した駒][段]
const auto kDropRank = key_chain.CreateKey<PieceType, Rank>();
// マス
// 周囲15マス（縦5マスx横3マス）の駒の配置 [指した駒][方向（左右対称）][近傍の駒]
const auto kSrcNeighbors = key_chain.CreateKey<PieceType, Number<0, 9>, Piece, Number<0, 1>, Number<0, 1>>();
const auto kDstNeighbors = key_chain.CreateKey<PieceType, Number<0, 9>, Piece, Number<0, 1>, Number<0, 1>>();
const auto kDropNeighbors = key_chain.CreateKey<PieceType, Number<0, 9>, Piece, Number<0, 1>, Number<0, 1>>();
// 周囲8マス（縦3マスx横3マス）の駒の配置（段ごとに場合分け） [指した駒][段][方向（左右対称）][近傍の駒]
const auto kSrcNeighborsAndRank = key_chain.CreateKey<PieceType, Rank, Number<0, 5>, Piece>();
const auto kDstNeighborsAndRank = key_chain.CreateKey<PieceType, Rank, Number<0, 5>, Piece>();
const auto kDropNeighborsAndRank = key_chain.CreateKey<PieceType, Rank, Number<0, 5>, Piece>();
// 敵・味方の利き数の組み合わせ [指した駒][味方の利き数（最大2）][敵の利き数（最大2）]
const auto kSrcControls = key_chain.CreateKey<PieceType, Number<0, 2>, Number<0, 2>>();
const auto kDstControls = key_chain.CreateKey<PieceType, Number<0, 2>, Number<0, 2>>();
const auto kDropControls = key_chain.CreateKey<PieceType, Number<0, 2>, Number<0, 2>>();
// 駒を打ったマスの、敵・味方の長い利きの数（0から2まで） [打った駒][利き数（最大2）]
const auto kDropOwnLongControls = key_chain.CreateKey<PieceType, Number<0, 2>>();
const auto kDropOppLongControls = key_chain.CreateKey<PieceType, Number<0, 2>>();
// 飛車・龍との相対位置
const auto kSrcRelationToOwnRook = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kSrcRelationToOppRook = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDstRelationToOwnRook = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDstRelationToOppRook = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDropRelationToOwnRook = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDropRelationToOppRook = key_chain.CreateKey<PieceType, SquareRelation>();
// 自玉との相対位置
const auto kSrcRelationToOwnKing = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDstRelationToOwnKing = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDropRelationToOwnKing = key_chain.CreateKey<PieceType, SquareRelation>();
// 敵玉との相対位置
const auto kSrcRelationToOppKing = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDstRelationToOppKing = key_chain.CreateKey<PieceType, SquareRelation>();
const auto kDropRelationToOppKing = key_chain.CreateKey<PieceType, SquareRelation>();
// 自玉との絶対位置 [自玉の位置][指した駒][マス]
const auto kSrcAbsRelationToOwnKing = key_chain.CreateKey<Square, PieceType, Square>();
const auto kDstAbsRelationToOwnKing = key_chain.CreateKey<Square, PieceType, Square>();
const auto kDropAbsRelationToOwnKing = key_chain.CreateKey<Square, PieceType, Square>();
// 敵玉との絶対位置 [敵玉の位置][指した駒][マス]
const auto kSrcAbsRelationToOppKing = key_chain.CreateKey<Square, PieceType, Square>();
const auto kDstAbsRelationToOppKing = key_chain.CreateKey<Square, PieceType, Square>();
const auto kDropAbsRelationToOppKing = key_chain.CreateKey<Square, PieceType, Square>();

//
// 過去の手との関係
//
// 1手前との関係 [今回指した駒][１手前に指した駒][1手前の移動先との位置関係]
const auto kSrcRelationToPreviousMove1 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDstRelationToPreviousMove1 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDropRelationToPreviousMove1 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
// 2手前との関係 [今回指した駒][2手前に指した駒][2手前の移動先との位置関係]
const auto kSrcRelationToPreviousMove2 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDstRelationToPreviousMove2 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDropRelationToPreviousMove2 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
// 3手前との関係 [今回指した駒][3手前に指した駒][3手前の移動先との位置関係]
const auto kSrcRelationToPreviousMove3 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDstRelationToPreviousMove3 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDropRelationToPreviousMove3 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
// 4手前との関係 [今回指した駒][4手前に指した駒][4手前の移動先との位置関係]
const auto kSrcRelationToPreviousMove4 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDstRelationToPreviousMove4 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();
const auto kDropRelationToPreviousMove4 = key_chain.CreateKey<PieceType, PieceType, SquareRelation>();

//
// 悪形手（分類性能確認済み）
//
// 敵飛先の敵歩前に歩を突く
const auto kPawnPushToOppRook = key_chain.CreateKey();

//
// 悪形手（KFEnd）
//
// 1. 歩の成り捨て
// 2. 端歩の突き捨て
// 3. 自玉の頭の歩突き
// 4. 敵飛先の敵歩前に歩を突く
// 5. 自らの飛筋を止める歩を打つ
// 6. 中盤における自陣1段目への歩打ち -> 「N段目への駒打ち」に一般化
// 7. 香の不成 -> 各駒ごとの「不成」に一般化
// 8. 桂の高跳び
// 9. 自陣1、2段目に桂を打つ -> 「N段目への駒打ち」に一般化
// 10. 序中盤における銀の後退 -> 「N段目が移動元の場合の，Y座標の増減」に一般化
// 11. 金の敵陣1段目への打ち込み ->　「N段目への駒打ち」に一般化
// 12. 自玉から2ます離れた金を玉から離れるように移動する -> そっぽ手
// 13. 金銀の自陣1段目への後退 -> 「N段目が移動元の場合の，Y座標の増減」として一般化
// 14. 玉に近接する金銀を玉から離れるように移動する -> そっぽ手
// 15. 歩の叩きに対して自玉のそばの金銀が逃げる -> 逃げる手[自玉との距離]
// 16. 自玉周辺への過剰な金銀打ち
// 17. 双方の玉から離れた位置に金銀を打つ -> そっぽ手
// 18. 自歩先の飛 -> どの段かで意味が違いそう。　段に意味ないなら3x3パターンでも可？
// 19. 飛を自陣3段目に移動する -> 「移動後の飛車の自由度ないし利き」に一般化？
// 20. 振飛車時、敵飛先の角交換に備えた飛を他の所に移動する
// 21. 飛の隠居
//     自陣で飛が追われたとき、なるべく敵陣に利きが通りやすいところに逃げるようにする。
//     -> 「移動後の飛車の自由度ないし利き」に一般化？
// 22. 振飛車時、敵飛前での角の後退
// 23. 自陣への飛、角の打ち込み -> 「N段目への駒打ち」に一般化
// 24. 玉を自陣3段目に上がる -> 「N段目が移動元の場合の，Y座標の増減」に一般化
// 25. 玉が自陣2段目から1段目に移動する
//     -> 「N段目が移動元の場合の，Y座標の増減」に一般化
//     -> これを入れる場合，穴熊だけは例外扱いしたほうがよさそう。
// 26. 玉が中央に移動する -> 「５筋とのX距離の増減」として一般化
// 27. 囲いを崩す（矢倉、美濃、穴熊、銀冠、金無双、舟囲いに対応）
// 28. 自陣の端に移動する
// 29. 壁形を作る
// 30. 壁銀を残す桂、金の移動


//
// 10. 歩の手筋
//
// 直射止めの歩（相手の飛・香の、自陣または味方の駒への直射を止める歩を打つ手）
const auto kPawnDropInterception = key_chain.CreateKey();
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
const auto kRookRetreatCutOffByPawn = key_chain.CreateKey();
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
// 味方の飛角の利きを止めている敵の歩に対して、歩で当たりをかける手 [タダ取りされる手か否か]
const auto kAttackToObstructingPawn = key_chain.CreateKey<bool>();
// 相手の歩のない筋で、味方飛香の利きにある歩を５段目より前に動かす手

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

// 玉の手筋
// 玉を動かす手の場合に、5筋とのX距離の増減

template<typename T>
PieceType GetMostValuablePieceType(Bitboard pieces, const T& board) {
  PieceType most_valuable_pt = kNoPieceType;
  Score max_value = kScoreZero;
  pieces.ForEach([&](Square s) {
    PieceType pt = board.piece_on(s).type();
    Score value = Material::exchange_value(pt);
    if (value > max_value) {
      most_valuable_pt = pt;
      max_value = value;
    }
  });
  return most_valuable_pt;
}

template<typename T>
PieceType GetLeastValuablePieceType(Bitboard pieces, const T& board) {
  PieceType least_valuable_pt = kNoPieceType;
  Score min_value = kScoreInfinite;
  pieces.ForEach([&](Square s) {
    PieceType pt = board.piece_on(s).type();
    Score value = Material::exchange_value(pt);
    if (value < min_value) {
      least_valuable_pt = pt;
      min_value = value;
    }
  });
  return least_valuable_pt;
}

ExtendedBoard GetNewExtendedBoard(const ExtendedBoard& old_eb, Move move) {
  ExtendedBoard new_eb = old_eb;

  if (move.is_capture()) {
    new_eb.MakeCaptureMove(move);
  } else if (move.is_drop()) {
    new_eb.MakeDropMove(move);
  } else {
    new_eb.MakeNonCaptureMove(move);
  }

  return new_eb;
}

int GetDegreeOfFreedom(Square ksq, Bitboard attacker_controls) {
  Bitboard edges = file_bb(kFile1) | file_bb(kFile9) | rank_bb(kRank1) | rank_bb(kRank9);
  Bitboard king_neighbors = neighborhood8_bb(ksq);
  Bitboard safe_neighbors = king_neighbors.andnot(attacker_controls);
  return safe_neighbors.andnot(edges).count() + int((safe_neighbors & edges).any());
}

template<typename T>
inline T min_max(T value, T min, T max) {
  return std::min(std::max(value, min), max);
}

bool IsSacrificeMove(Move move, const Position& pos) {
  Color stm = pos.side_to_move();
  Square to = move.to();

  if (move.is_drop()) {
    // 移動先に敵の利きがある and 移動先に味方の駒の利きがない
    return pos.num_controls(~stm, to) >= 1
        && pos.num_controls(stm, to) == 0;

  } else {
    // 移動先に敵の利きがある and 移動先に他の味方の駒の利きがない
    if (   pos.num_controls(~stm, to) >= 1
        && pos.num_controls(stm, to) == 1) {

      // 移動元に飛び駒の利きがない or 移動先方向に飛び駒の利きがない
      Square from = move.from();
      DirectionSet long_controls = pos.long_controls(stm, from);
      return long_controls.none()
          || !direction_bb(from, long_controls).test(to);
    }

    return false;
  }
}

bool IsDiscoveredCheck(Move move, const Position& pos) {
  Square opp_ksq = pos.king_square(~pos.side_to_move());
  return !move.is_drop()
      && pos.discovered_check_candidates().test(move.from())
      && !line_bb(move.from(), opp_ksq).test(move.to());
}

bool IsSendOffCheck(Move move, const Position& pos) {
  if (!move.is_drop()) {
    return false;
  }

  Color stm = pos.side_to_move();
  Square opp_ksq = pos.king_square(~stm);
  Bitboard opp_king_neighbors8 = neighborhood8_bb(opp_ksq);
  const ExtendedBoard& eb = pos.extended_board();

  if (   opp_king_neighbors8.test(move.to())
      && pos.num_controls(stm, move.to()) == 0
      && opp_king_neighbors8.test(pos.pieces(~stm))) {

    // 玉しか利きがついていない駒を目標に定める
    EightNeighborhoods own_controls = eb.GetEightNeighborhoodControls(stm, opp_ksq);
    EightNeighborhoods opp_controls = eb.GetEightNeighborhoodControls(~stm, opp_ksq);
    Bitboard own_attacks = direction_bb(opp_ksq, own_controls.more_than(0));
    Bitboard opp_defenses = direction_bb(opp_ksq, opp_controls.more_than(1));
    Bitboard target = (pos.pieces(~stm) & own_attacks).andnot(opp_defenses);

    // 玉しか利きがない駒があれば、玉の利きが外れるか否かを調べる
    if (target.any()) {
      Bitboard old_king_attacks = opp_king_neighbors8;
      Bitboard new_king_attacks = neighborhood8_bb(move.to());
      Bitboard no_king_attack = old_king_attacks.andnot(new_king_attacks);
      return (target & no_king_attack).any();
    }
  }

  return false;
}

bool BlocksLongAttacksToOwnCastle(Move move, const Position& pos) {
  assert(move.is_drop());

  Color stm = pos.side_to_move();
  const ExtendedBoard& old_eb = pos.extended_board();
  ExtendedBoard new_eb = pos.extended_board();
  Bitboard own_king_neighbors = neighborhood8_bb(pos.king_square(stm));

    new_eb.MakeDropMove(move);

  if (old_eb.long_controls(~stm, move.to()).any()) {
    bool more_opp_attack = false;
    bool less_opp_attack = false;
    own_king_neighbors.ForEach([&](Square s) {
      if (new_eb.num_controls(~stm, s) > old_eb.num_controls(~stm, s)) {
        more_opp_attack = true;
      } else if (new_eb.num_controls(~stm, s) < old_eb.num_controls(~stm, s)) {
        less_opp_attack = true;
      }
    });
    return !more_opp_attack && less_opp_attack;
  }

  return false;
}

inline Score EvaluateThreat(Move move, const Position& pos) {
  if (move.is_drop()) {
    return kScoreZero;
  } else {
    // 今駒を打ったかのようにして、仮想手を作る
    //（現実に存在しない手なので、通常のコンストラクタではなくファクトリ関数を使い、assertチェックの対象外にする。）
    Move pseudo_move = Move::Create(move.piece(), move.from());

    // 仮想手のSEE値を求めることで、今その駒が駒取りの脅威を受けているかが分かる
    return -Swap::Evaluate(pseudo_move, pos);
  }
}

bool IsInterceptionDefense(Move move, Square victim_sq, const Position& pos) {
  Color stm = pos.side_to_move();
  DirectionSet long_attacks = pos.extended_board().long_controls(~stm, victim_sq);
  if (long_attacks.any()) {
    Bitboard attackers = pos.SlidersAttackingTo(victim_sq, pos.pieces(), ~stm);
    while (attackers.any()) {
      Square attacker_sq = attackers.pop_first_one();
      if (between_bb(attacker_sq, victim_sq).test(move.to())) {
        return true;
      }
    }
  }
  return false;
}

bool IsPawnPushToOppRook(Move move, const Position& pos) {
  if (move.piece_type() == kPawn || move.is_drop()) {
    return false;
  }

  Color stm = pos.side_to_move();
  Bitboard opp_rooks_on_same_file = file_bb(move.from().file())
                                  & pos.pieces(~stm, kRook);
  if (opp_rooks_on_same_file.any()) {
    Square rook_sq = opp_rooks_on_same_file.first_one();
    Bitboard front = max_attacks_bb(Piece(~stm, kLance), rook_sq);
    Bitboard between = between_bb(move.from(), rook_sq);
    return (pos.pieces(~stm, kPawn) & front & between).any();
  }

  return false;
}

bool IsPawnDropInterruptingOwnRook(Move move, const Position& pos) {
  if (move.piece_type() != kPawn || !move.is_drop()) {
    return false;
  }

  Color stm = pos.side_to_move();
  Bitboard own_rooks = pos.pieces(stm, kRook);
  if (own_rooks.any()) {
    Square rook_sq = own_rooks.first_one();
    Bitboard front = lance_attacks_bb(rook_sq, pos.pieces(), stm);
    return front.test(move.to())
        && pos.num_controls(~stm, move.to()) == 0;
  }

  return false;
}

} // namespace

const int kNumBinaryMoveFeatures = key_chain.total_size_of_keys();
const int kNumMoveFeatures = kNumBinaryMoveFeatures + kNumContinuousMoveFeatures;

template<Color kColor>
MoveFeatureList ExtractMoveFeatures(const Move move, const Position& pos,
                                    const PositionInfo& pos_info) {
  assert(pos.MoveIsPseudoLegal(move));

  MoveFeatureList feature_list;

  const ExtendedBoard& old_eb = pos.extended_board();
  const ExtendedBoard new_eb = GetNewExtendedBoard(old_eb, move);

  const Square dst = move.to().relative_square(kColor);
  const Square src = move.is_drop() ? kSquareNone : move.from().relative_square(kColor);

  // 移動先・移動元のビットボード
  const Bitboard to_bb = square_bb(move.to());
  const Bitboard from_bb = move.is_drop() ? Bitboard() : square_bb(move.from());

  // 駒のあるマスのビットボード（指す前、指した後）
  const Bitboard old_occ = pos.pieces();
  const Bitboard new_occ = (old_occ | to_bb).andnot(from_bb);
  const Bitboard old_own_pieces = pos.pieces(kColor);
  const Bitboard old_opp_pieces = pos.pieces(~kColor);
  const Bitboard new_own_pieces = (old_own_pieces | to_bb).andnot(from_bb);
  const Bitboard new_opp_pieces = old_opp_pieces.andnot(to_bb);

  // 利きのあるマスのビットボード（指す前、指した後）
  const Bitboard old_own_controls = old_eb.GetControlledSquares(kColor);
  const Bitboard old_opp_controls = old_eb.GetControlledSquares(~kColor);
  const Bitboard new_own_controls = new_eb.GetControlledSquares(kColor);
  const Bitboard new_opp_controls = new_eb.GetControlledSquares(~kColor);

  // 自玉・敵玉の位置
  const Square own_ksq = pos.king_square(kColor);
  const Square opp_ksq = pos.king_square(~kColor);

  const Score see_score = Swap::Evaluate(move, pos);
  const int see_sign = math::sign(int(see_score));

  // 直近4手
  const Move previous_move1 = pos.last_move();
  const Move previous_move2 = pos.move_before_n_ply(2);
  const Move previous_move3 = pos.move_before_n_ply(3);
  const Move previous_move4 = pos.move_before_n_ply(4);

  // 動かした駒の利き
  const PieceType pt = move.piece_type();
  const Bitboard old_attacks = move.is_drop() ? Bitboard() : AttacksFrom(move.piece(), move.from(), old_occ);
  const Bitboard new_attacks = AttacksFrom(move.piece_after_move(), move.to(), new_occ);

  // 王手か否か
  const bool move_gives_check = pos.MoveGivesCheck(move);

  auto add_feature = [&](size_t index) {
    feature_list.push_back(index);
  };

  //
  // 一般的な特徴や、連続値を取る特徴
  //
  {
    const float kMaterialScale = 1.0f / float(Material::exchange_value(kDragon));
    const float kHistoryScale = 1.0f / float(HistoryStats::kMax);

    // バイアス項
    add_feature(kBiasTerm());

    // SEE値（-1＜龍損＞から+1＜龍得＞までの連続値）
    float normalized_see_score = float(see_score) * kMaterialScale;
    float see_value = min_max(normalized_see_score, -1.0f, 1.0f);
    feature_list.continuous_values[kSeeValue] = see_value;

    // Global SEE値（-1＜龍損＞から+1＜龍得＞までの連続値）
    if (!pos.in_check() && move.is_capture_or_promotion()) {
      int global_see_score = Swap::EvaluateGlobalSwap(move, pos, 3);
      float normalized_global_see_score = float(global_see_score) * kMaterialScale;
      float global_see_value = min_max(normalized_global_see_score, -1.0f, 1.0f);
      feature_list.continuous_values[kGlobalSeeValue] = global_see_value;
    } else {
      // Global SEE値により一致率が上がるのは、王手がかかっていない局面の、取る手又は成る手のみ
      feature_list.continuous_values[kGlobalSeeValue] = 0.0f;
    }

    // 手を指した後、相手にタダ取りされる最高の駒
    Bitboard capture_threats = pos.pieces(kColor) & new_occ & new_opp_controls & ~new_own_controls;
    PieceType biggest_threat = GetMostValuablePieceType(capture_threats, pos);
    add_feature(kCaptureThreatAfterMove(biggest_threat));

    // 手を指した後、飛車にタダで成りこまれる
    for (Bitboard opp_rooks = pos.pieces(~kColor, kRook) & new_occ.andnot(promotion_zone_bb(~kColor));
         opp_rooks.any(); ) {
      Square rook_sq = opp_rooks.pop_first_one();
      Bitboard rook_attacks = rook_attacks_bb(rook_sq, new_occ);
      Bitboard rook_promotions = rook_attacks & promotion_zone_bb(~kColor);
      if (rook_promotions.andnot(new_own_controls).any()) {
        add_feature(kRookPromotionThreatAfterMove());
      }
    }

    // 手を指した後、角にタダで成りこまれる
    for (Bitboard opp_bishops = pos.pieces(~kColor, kBishop) & new_occ.andnot(promotion_zone_bb(~kColor));
         opp_bishops.any(); ) {
      Square bishop_sq = opp_bishops.pop_first_one();
      Bitboard bishop_attacks = bishop_attacks_bb(bishop_sq, new_occ);
      Bitboard bishop_promotions = bishop_attacks & promotion_zone_bb(~kColor);
      if (bishop_promotions.andnot(new_own_controls).any()) {
        add_feature(kBishopPromotionThreatAfterMove());
      }
    }

    // 手を指した後の、敵玉の自由度
    int degree_of_freedom = GetDegreeOfFreedom(opp_ksq, new_own_controls);
    add_feature(kOppKingFreedomAfterMove(min_max(degree_of_freedom, 1, 6)));

    // 静かな手の特徴
    if (move.is_quiet()) {
      // 静かな手
      add_feature(kBiasTermOfQuietMove());

      // history value（-1から+1までの連続値）
      float history = pos_info.history[move];
      feature_list.continuous_values[kHistoryValue] = history * kHistoryScale;

      // counter move history value（-1から+1までの連続値）
      if (pos_info.countermoves_history != nullptr) {
        float cmh = (*pos_info.countermoves_history)[move];
        feature_list.continuous_values[kCounterMoveHistoryValue] = cmh * kHistoryScale;
      } else {
        feature_list.continuous_values[kCounterMoveHistoryValue] = 0.0f;
      }

      // follow-up move history value（-1から+1までの連続値）
      if (pos_info.followupmoves_history != nullptr) {
        float fmh = (*pos_info.followupmoves_history)[move];
        feature_list.continuous_values[kFollowupMoveHistoryValue] = fmh * kHistoryScale;
      } else {
        feature_list.continuous_values[kFollowupMoveHistoryValue] = 0.0f;
      }

      // evaluation gain（-1＜龍損＞から+1＜龍得＞までの連続値）
      float gain = pos_info.gains[move];
      feature_list.continuous_values[kEvaluationGain] = gain * kMaterialScale;
    } else {
      feature_list.continuous_values[kHistoryValue] = 0.0f;
      feature_list.continuous_values[kCounterMoveHistoryValue] = 0.0f;
      feature_list.continuous_values[kFollowupMoveHistoryValue] = 0.0f;
      feature_list.continuous_values[kEvaluationGain] = 0.0f;
    }
  }


  //
  // 王手回避手
  //
  if (pos.in_check()) {
    if (move.is_capture()) {
      // 取る手の場合に、動かす駒
      add_feature(kIsCaptureOfChecker(pt));

    } else if (move.piece_type() == kKing) {
      // 玉が逃げる手の場合に、移動後の玉の自由度（1から6まで）
      int degree_of_freedom = GetDegreeOfFreedom(move.to(), new_opp_controls);
      add_feature(kKingFreedomAfterEvasion(min_max(degree_of_freedom, 1, 6)));

    } else if (!IsSacrificeMove(move, pos)) {
      // 合駒の場合に、合駒する駒の種類
      add_feature(kIsInterception(pt));

      // 合駒の場合に、合駒と玉との距離（1から3まで）
      int distance = Square::distance(move.to(), own_ksq);
      add_feature(kDistanceBetweenKingAndInterceptor(min_max(distance, 1, 3)));

    } else {
      // 中合いの場合に、合駒する駒の種類
      add_feature(kIsChuai(pt));

      // 中合いの場合に、合駒と玉との距離（2から4まで）
      int distance = Square::distance(move.to(), own_ksq);
      add_feature(kDistanceBetweenKingAndChuaiPiece(min_max(distance, 2, 4)));
    }

    return feature_list;
  }


  //
  // 取る手の特徴
  //
  if (move.is_capture()) {
    // 取れる最高の駒を取る手（SEE>=0）
    if (   pos_info.most_valuable_victim.test(to_bb)
        && see_sign >= 0) {
      add_feature(kIsCaptureOfMostValuablePiece());
    }

    // 直前に動いた駒の取り返し（SEE>=0）
    if (   previous_move1.is_real_move()
        && move.to() == previous_move1.to()
        && see_sign >= 0) {
      add_feature(kIsRecapture());
    }

    // 2手前に動いた駒で駒を取る手（SEE>=0）
    if (   previous_move2.is_real_move()
        && move.from() == previous_move2.to()
        && see_sign >= 0) {
      add_feature(kIsCaptureWithLastMovedPiece());
    }

    if (see_sign > 0) {
      // 駒得の取る手 [歩何枚分の得か]
      int gain = int(see_score) / Material::exchange_value(kPawn);
      add_feature(kGoodCapture(min_max(gain, 0, 7)));
    } else if (see_sign == 0) {
      // 駒交換 [交換する駒]
      add_feature(kEqualCapture(pt));
    } else {
      // 駒損の取る手 [歩何枚分の損か]
      int loss = int(-see_score) / Material::exchange_value(kPawn);
      add_feature(kBadCapture(min_max(loss, 0, 7)));
    }

    // [取った駒]
    PieceType captured = move.captured_piece_type();
    add_feature(kCapturedPiece(captured));

    // [動かした駒]
    add_feature(kMovedPieceToCapture(pt));

    // [動かした駒][取った駒]
    add_feature(kMovedPieceAndCapturedPiece(pt, captured));

    // [取った駒][段]
    add_feature(kCapturedPieceAndRank(captured, dst.rank()));

    // [取った駒][自玉との距離]
    int distance_to_own_king = Square::distance(move.to(), own_ksq);
    add_feature(kCapturedPieceAndDistanceToOwnKing(captured, distance_to_own_king));

    // [取った駒][敵玉との距離]（歩・香・桂を取る手について）
    int distance_to_opp_king = Square::distance(move.to(), opp_ksq);
    add_feature(kCapturedPieceAndDistanceToOppKing(captured, distance_to_opp_king));

    // [取った駒][取った駒を持ち駒に持っているか]
    PieceType hand_type = GetOriginalType(captured);
    int num_hands = pos.hand(kColor).has(hand_type);
    add_feature(kNumHandPiecesBeforeCapture(hand_type, num_hands));
  }

  //
  // 成る手
  //
  if (move.is_promotion()) {
    // 成る手 [動かした駒]
    add_feature(kIsPromotion(pt));

    // [動かした駒][敵玉との距離]
    int distance_to_opp_king = Square::distance(move.to(), opp_ksq);
    add_feature(kDistanceToOppKingAfterPromotion(pt, distance_to_opp_king));

    // [動かした駒][移動元の段]
    if (see_sign >= 0) {
      Rank r = relative_rank(kColor, move.from().rank());
      add_feature(kRankBeforePromotion(pt, r));
    }

    // [動かした駒][移動先の段]
    if (see_sign >= 0) {
      Rank r = relative_rank(kColor, move.to().rank());
      add_feature(kRankAfterPromotion(pt, r));
    }

    // 駒損の成る手 [歩何枚分の損か]
    if (see_sign < 0) {
      int loss = int(-see_score) / Material::exchange_value(kPawn);
      add_feature(kMaterialLossAfterPromotion(std::min(loss, 7)));
    }
  }

  //
  // 王手
  //
  if (move_gives_check) {
    // 王手後の、敵玉の自由度
    int degree_of_freedom = GetDegreeOfFreedom(opp_ksq, new_own_controls);

    // a. 開き王手（SEE値で場合分けしない）
    if (IsDiscoveredCheck(move, pos)) {
      add_feature(kIsDiscoveredCheck(pt));

    // b. SEE>=0の王手
    } else if (see_sign >= 0) {
      add_feature(kIsGoodCheck(pt));
      add_feature(kOppKingFreedomAfterGoodCheck(min_max(degree_of_freedom, 1, 6)));

    // c. SEE<0の王手 [王手した駒]
    } else if (!IsSacrificeMove(move, pos)) {
      add_feature(kIsBadCheck(pt));
      add_feature(kOppKingFreedomAfterBadCheck(min_max(degree_of_freedom, 1, 6)));

      // d. タダ取りされる王手 [王手した駒]
    } else {
      add_feature(kIsSacrificeCheck(pt));
      add_feature(kOppKingFreedomAfterSacrificeCheck(min_max(degree_of_freedom, 1, 6)));
    }

    // 駒損の王手 [歩何枚分の損か]
    if (see_sign < 0) {
      int loss = int(-see_score) / Material::exchange_value(kPawn);
      add_feature(kMaterialLossAfterCheck(std::min(loss, 7)));
    }
  }

  //
  // 攻める手
  //
  {
    // 長い利きを付けた駒の種類 [動かした駒][利きを付けた駒][味方の利きの有無][敵の利きの有無]
    if (move.piece_after_move().is_slider()) {
      Bitboard long_attacks = (new_attacks & new_occ).andnot(neighborhood8_bb(move.to()));
      long_attacks.ForEach([&](Square sq) -> void {
        Piece piece = new_eb.piece_on(sq);
        if (kColor == kWhite) piece = piece.opponent_piece(); // 先手視点にする
        int own_control = std::min(new_eb.num_controls(kColor, sq) - 1, 1); // 自分自身の利きを除く
        int opp_control = std::min(new_eb.num_controls(~kColor, sq), 1);
        add_feature(kDstLongAttack(pt, piece, own_control, opp_control));
      });
    }

    // 長い利きを付けた駒の種類 [動かした駒][利きを付けた駒][味方の利きの有無][敵の利きの有無]
    if (   !move.is_drop()
        && move.piece().is_slider()) {
      Bitboard long_attacks = (old_attacks & old_occ).andnot(neighborhood8_bb(move.from()));
      long_attacks.ForEach([&](Square sq) -> void {
        Piece piece = old_eb.piece_on(sq);
        if (kColor == kWhite) piece = piece.opponent_piece(); // 先手視点にする
        int own_control = std::min(old_eb.num_controls(kColor, sq) - 1, 1); // 自分自身の利きを除く
        int opp_control = std::min(old_eb.num_controls(~kColor, sq), 1);
        add_feature(kSrcLongAttack(pt, piece, own_control, opp_control));
      });
    }

    // 空き当たり [利きを付けた駒][味方の利きの有無][敵の利きの有無]
    if (!move.is_drop()) {
      DirectionSet long_controls = old_eb.long_controls(kColor, move.from());
      if (long_controls.any()) {
        Bitboard discovered_attacks = queen_attacks_bb(move.from(), new_occ)
                                    & direction_bb(move.from(), long_controls)
                                    & ~line_bb(move.from(), move.to());
        Bitboard attacked_pieces = new_occ & discovered_attacks;
        attacked_pieces.ForEach([&](Square sq) -> void {
          Piece piece = new_eb.piece_on(sq);
          if (kColor == kWhite) piece = piece.opponent_piece(); // 先手視点にする
          int own_control = std::min(new_eb.num_controls(kColor, sq) - 1, 1); // 自分自身の利きを除く
          int opp_control = std::min(new_eb.num_controls(~kColor, sq), 1);
          add_feature(kDiscoveredAttack(pt, piece, own_control, opp_control));
        });
      }
    }

    // ピンしている駒に対する当たり [動かした駒][利きを付けた駒]
    if (see_sign < 0) {
      for (Bitboard targets = new_attacks & pos_info.opponent_pinned_pieces;
           targets.any(); ) {
        Square target_sq = targets.pop_first_one();
        if (!line_bb(target_sq, opp_ksq).test(move.to())) {
          PieceType target_pt = pos.piece_on(target_sq).type();
          add_feature(kAttackToPinnedPieces(pt, target_pt));
        }
      }
    }

    // 両取りの手 [指した駒][当たりを付けた駒のうち、２番目に価値が高い相手の駒]
    if (see_sign >= 0) {
      PieceType best_piece_type = kNoPieceType, second_piece_type = kNoPieceType;
      Score best_piece_value = kScoreZero, second_piece_value = kScoreZero;
      Bitboard targets = new_attacks.andnot(old_attacks) & pos.pieces(~kColor);
      targets.ForEach([&](Square sq) {
        PieceType piece_type = pos.piece_on(sq).type();
        Score piece_value = Material::exchange_value(piece_type);
        if (piece_value > best_piece_value) {
          // 最も価値の高い駒/２番めに価値の高い駒を更新する
          second_piece_type   = best_piece_type;
          second_piece_value  = best_piece_value;
          best_piece_type   = piece_type;
          best_piece_value  = piece_value;
        } else if (piece_value > second_piece_value) {
          // ２番目に価値の高い駒を更新
          second_piece_type   = piece_type;
          second_piece_value  = piece_value;
        }
      });
      add_feature(kDoubleAttack(pt, second_piece_type));
    }

    // 同〜と取り返してきたら取れる最高の駒 [犠牲にする駒][取れる最高の相手駒]
    if (IsSacrificeMove(move, pos)) {
      Bitboard old_victims = old_opp_pieces & old_own_controls.andnot(old_opp_controls);

      // 相手の取り返しの手を求める
      Bitboard opp_attackers = pos.AttackersTo<~kColor>(move.to(), new_occ);
      PieceType opp_attacker = GetLeastValuablePieceType(opp_attackers, new_eb);
      Square attacker_sq = (pos.pieces(~kColor, opp_attacker) & new_occ).first_one();
      Move recapture(Piece(~kColor, opp_attacker),
                     attacker_sq, move.to(), false, move.piece_after_move());

      // 取り返した後の駒の配置及び利きを求める
      ExtendedBoard next_eb = new_eb;
      next_eb.MakeCaptureMove(recapture);
      Bitboard next_opp_pieces = new_opp_pieces.andnot(square_bb(attacker_sq));
      Bitboard next_own_controls = next_eb.GetControlledSquares(kColor);
      Bitboard next_opp_controls = next_eb.GetControlledSquares(~kColor);

      // 次の局面でタダ取りできる相手の駒を求める
      Bitboard next_victims = next_opp_pieces & next_own_controls.andnot(next_opp_controls);

      // 新たにタダ取りできるようになった相手の駒のうち、最高の駒を求める
      PieceType victim_pt = GetMostValuablePieceType(next_victims.andnot(old_victims), next_eb);

      add_feature(kCaptureAfterSacrifice(pt, victim_pt));
    }
  }

  //
  // 受ける手
  //
  {
    Score threat_score = EvaluateThreat(move, pos);

    // 取られそうな最高の駒を逃げる手（SSE>=0）
    if (   pos_info.most_valuable_threatened_piece.test(from_bb)
        && see_sign >= 0
        && (pt != kPawn && pt != kLance)) {
      add_feature(kEscapeMoveOfMostValuablePiece());
    }

    // 取られそうな駒を逃げる手（SEE>=0）[動かした駒]
    if (   threat_score > 0
        && see_sign >= 0) {
      add_feature(kEscapeMove(pt));
    }

    // 駒が取られそうな場合に、合駒をして守る手 [動かした駒][守った駒]
    if (   see_sign == 0
        && move.is_drop()
        && pos_info.most_valuable_threatened_piece.any()) {
      Square victim_sq = pos_info.most_valuable_threatened_piece.first_one();
      if (IsInterceptionDefense(move, victim_sq, pos)) {
        PieceType victim_pt = pos.piece_on(victim_sq).type();
        add_feature(kInterceptionDefense(pt, victim_pt));
      }
    }
  }

  //
  // 自玉周り
  //
  if (move.is_drop()) {
    // 自玉の８近傍への飛び利きを遮る手（打つ手のみ） [駒の種類]
    if (   BlocksLongAttacksToOwnCastle(move, pos)
        && see_sign >= 0) {
      add_feature(kBlockOfLongAttacksToOwnCastle(pt));
    }

    // 自玉の８近傍に利きを足す手（打つ手のみ）（味方の利きより相手の利きが多いマスに利きを足す）[駒の種類]
    if (   new_attacks.test(pos_info.dangerous_king_neighborhood_squares)
        && see_sign >= 0) {
      add_feature(kReinforcementOfOurCastle(pt));
    }
  }

  //
  // 敵玉周り
  //
  {
    // 敵玉８近傍に利きを足す手（飛び駒のみ）
    if (   move.piece().is_slider()
        && new_attacks.test(pos_info.opponent_king_neighborhoods8)
        && see_sign >= 0) {
      add_feature(kAttackToOppKingNeighbor8());
    }
  }

  //
  // 利き関係（ExtendedBoardを使った盤全体の評価）
  //
  if (see_sign == 0) {
    // 新たに利きを付けた相手の駒
    Bitboard added_controls = new_own_controls.andnot(old_own_controls);
    Bitboard added_attacks = pos.pieces(~kColor).andnot(to_bb) & added_controls;
    add_feature(kNewAttackToOppPiece(GetMostValuablePieceType(added_attacks, new_eb)));

    // 手を指した後、相手がパスした場合、タダ取りできる最高の駒
    Bitboard expected_good_capture = pos.pieces(~kColor).andnot(to_bb)
                                   & new_own_controls.andnot(new_opp_controls);
    add_feature(kNextGoodCapture(GetMostValuablePieceType(expected_good_capture, new_eb)));
  }

  //
  // 移動手の特徴
  //
  if (   !move.is_drop()
      && see_sign == 0) {
    // 移動方向（前か、横か、後ろか） [指した駒][移動元の段][Y座標の増減]
    int delta_y = math::sign(int(dst.rank() - src.rank()));
    add_feature(kDeltaCoordinateY(pt, src.rank(), delta_y));

    // 5筋とのX距離の増減
    {
      int distance_to_center1 = std::abs(int(src.file() - kFile5));
      int distance_to_center2 = std::abs(int(dst.file() - kFile5));
      int delta = math::sign(distance_to_center2 - distance_to_center1);
      add_feature(kDeltaDistanceFromCenter(pt, delta));
    }

    // そっぽ手判定（自陣の駒） [指した駒][移動元と自玉とのX距離][自玉とのX距離の増減]
    if (move.from().is_promotion_zone_of(~kColor)) {
      int x_distance_to_own_king1 = std::abs(int(move.from().file() - own_ksq.file()));
      int x_distance_to_own_king2 = std::abs(int(move.to().file() - own_ksq.file()));
      int delta = math::sign(int(x_distance_to_own_king2 - x_distance_to_own_king1));
      add_feature(kIsGoingAwayFromOwnKing(pt, std::min(x_distance_to_own_king1, 7), delta));

    // そっぽ手判定（敵陣の駒） [指した駒][移動元と敵玉とのX距離][敵玉とのX距離の増減]
    } else if (move.from().is_promotion_zone_of(kColor)) {
      int x_distance_to_opp_king1 = std::abs(int(move.from().file() - opp_ksq.file()));
      int x_distance_to_opp_king2 = std::abs(int(move.to().file() - opp_ksq.file()));
      int delta = math::sign(int(x_distance_to_opp_king2 - x_distance_to_opp_king1));
      add_feature(kIsGoingAwayFromOwnKing(pt, std::min(x_distance_to_opp_king1, 7), delta));
    }

    // 動くと取られてしまう最高の駒（原因：ピン）
    DirectionSet opp_long_controls = old_eb.long_controls(~kColor, move.from());
    if (opp_long_controls.any()) {
      Bitboard opp_xray_attacks = queen_attacks_bb(move.from(), new_occ)
                                & direction_bb(move.from(), opp_long_controls)
                                & ~line_bb(move.from(), move.to());
      Bitboard xray_targets = pos.pieces(kColor) & new_occ & opp_xray_attacks;
      Bitboard victims = (xray_targets & new_opp_controls).andnot(new_own_controls);
      PieceType most_valuable_victim = GetMostValuablePieceType(victims, new_eb);
      add_feature(kOpponentXrayThreat(pt, most_valuable_victim));
    }

    // 駒の自由度 [指した駒][相手の利きがなく、移動可能なマス]
    if (see_sign == 0) {
      Bitboard safe_moves = new_attacks.andnot(new_own_pieces | new_opp_controls);
      add_feature(kDegreeOfLibertyAfterMove(pt, std::min(safe_moves.count(), 12)));
    }

    // 元いた場所に戻る手 [指した駒]
    if (   previous_move2.is_real_move()
        && move.from() == previous_move2.to()
        && !previous_move2.is_drop()
        && move.to() == previous_move2.from()
        && !move.is_capture_or_promotion()
        && !previous_move2.is_capture_or_promotion()) {
      add_feature(kSuccessiveMoveOfSamePiece(pt));
    }

    // 自陣の駒打ちのスキを増やしてしまう手 [指す駒][相手の持ち駒][スキが増えるか否か]
    if (   !move.is_capture_or_promotion()
        && !move_gives_check) {
      auto tests_move_makes_holes_in_our_area = [&](PieceType opp_drop_pt) {
        if (pos.hand(~kColor).has(opp_drop_pt)) {
          // 自陣の中で、相手が駒打を打てる場所を求める
          Bitboard drop_target = promotion_zone_bb(~kColor);
          if (opp_drop_pt == kPawn || opp_drop_pt == kLance) {
            drop_target &= rank_bb<kColor, 7, 8>();
          } else if (opp_drop_pt == kKnight) {
            drop_target &= rank_bb<kColor, 7, 7>();
          }

          // 駒を打たれても取り返せない場所を求める
          Bitboard old_drop_threat = drop_target.andnot(old_occ | old_own_controls);
          Bitboard new_drop_threat = drop_target.andnot(new_occ | new_own_controls);

          bool increases_holes = new_drop_threat.count() > old_drop_threat.count();
          add_feature(kMakesHolesInOurArea(pt, opp_drop_pt, increases_holes));
        }
      };
      tests_move_makes_holes_in_our_area(kPawn  );
      tests_move_makes_holes_in_our_area(kLance );
      tests_move_makes_holes_in_our_area(kKnight);
      tests_move_makes_holes_in_our_area(kSilver);
      tests_move_makes_holes_in_our_area(kGold  );
      tests_move_makes_holes_in_our_area(kBishop);
      tests_move_makes_holes_in_our_area(kRook  );
    }
  }

  //
  // 打つ手の特徴
  //
  if (move.is_drop()) {
    // 持ち駒の枚数 [打つ駒][持ち駒の枚数]
    add_feature(kNumHandPieces(pt, std::min(pos.hand(kColor).count(pt), 3)));

    // 安全に成れるマスの数 [打つ駒][安全に成れるマスの数]
    if (   promotion_zone_bb(kColor).test(to_bb)
        && see_sign == 0) {
      Bitboard safe_promotions = new_attacks.andnot(new_own_pieces | new_opp_controls);
      add_feature(kNumSafePromotions(pt, std::min(safe_promotions.count(), 4)));
    }

    // 駒の自由度 [指した駒][相手の利きがなく、移動可能なマス]
    if (see_sign == 0) {
      Bitboard safe_moves = new_attacks.andnot(new_own_pieces | new_opp_controls);
      add_feature(kDegreeOfLibertyAfterDrop(pt, std::min(safe_moves.count(), 12)));
    }
  }

  //
  // パターン
  //
  // 駒が安全に移動できるマスのパターン [指した駒][段][安全に移動できるマスのパターン]
  if (   !move.is_capture_or_promotion()
      && !move_gives_check
      && see_sign == 0) {
    auto flip = [](DirectionSet ds) {
      DirectionSet flipped;
      flipped.set(kDirNE, ds.test(kDirSW));
      flipped.set(kDirE , ds.test(kDirW ));
      flipped.set(kDirSE, ds.test(kDirNW));
      flipped.set(kDirN , ds.test(kDirS ));
      flipped.set(kDirS , ds.test(kDirN ));
      flipped.set(kDirNW, ds.test(kDirSE));
      flipped.set(kDirW , ds.test(kDirE ));
      flipped.set(kDirSW, ds.test(kDirNE));
      return flipped;
    };
    Bitboard safe_moves = new_attacks.andnot(new_own_pieces | new_opp_controls);
    DirectionSet pattern = safe_moves.neighborhood8(move.to());
    if (kColor == kWhite) {
      pattern = flip(pattern);
    }
    add_feature(kPatternOfSafeMoves(pt, dst.rank(), pattern));
  }

  //
  // 悪形手
  //
  {
    // 敵飛先の敵歩前に歩を突く
    if (IsPawnPushToOppRook(move, pos)) {
      add_feature(kPawnPushToOppRook());
    }
  }

  //
  // 移動元・移動先に関する特徴
  //
  if (move.is_drop()) {
    Square to = move.to();

    // 段 [指した駒][段]
    if (see_sign >= 0) {
      add_feature(kDropRank(pt, relative_rank(kColor, to.rank())));
    }

    // 周囲15マス（縦5マスx横3マス）の駒の配置 [指した駒][方向（左右対称）][近傍の駒][味方利きの有無][相手利きの有無]
    FifteenNeighborhoods pieces = new_eb.GetFifteenNeighborhoodPieces(to);
    FifteenNeighborhoods own_controls = new_eb.GetFifteenNeighborhoodControls(kColor, to).LimitTo(1);
    FifteenNeighborhoods opp_controls = new_eb.GetFifteenNeighborhoodControls(~kColor, to).LimitTo(1);
    if (kColor == kWhite) {
      pieces = pieces.Rotate180().FlipPieceColors();
      own_controls = own_controls.Rotate180();
      opp_controls = opp_controls.Rotate180();
    }
    add_feature(kDropNeighbors(pt,  0, Piece(pieces.at( 0)), own_controls.at( 0), opp_controls.at( 0)));;
    add_feature(kDropNeighbors(pt,  1, Piece(pieces.at( 1)), own_controls.at( 1), opp_controls.at( 1)));;
    add_feature(kDropNeighbors(pt,  2, Piece(pieces.at( 2)), own_controls.at( 2), opp_controls.at( 2)));;
    add_feature(kDropNeighbors(pt,  3, Piece(pieces.at( 3)), own_controls.at( 3), opp_controls.at( 3)));;
    add_feature(kDropNeighbors(pt,  4, Piece(pieces.at( 4)), own_controls.at( 4), opp_controls.at( 4)));;
    add_feature(kDropNeighbors(pt,  5, Piece(pieces.at( 5)), own_controls.at( 5), opp_controls.at( 5)));;
    add_feature(kDropNeighbors(pt,  6, Piece(pieces.at( 6)), own_controls.at( 6), opp_controls.at( 6)));;
    add_feature(kDropNeighbors(pt,  7, Piece(pieces.at( 7)), own_controls.at( 7), opp_controls.at( 7)));;
    add_feature(kDropNeighbors(pt,  8, Piece(pieces.at( 8)), own_controls.at( 8), opp_controls.at( 8)));;
    add_feature(kDropNeighbors(pt,  9, Piece(pieces.at( 9)), own_controls.at( 9), opp_controls.at( 9)));;
    add_feature(kDropNeighbors(pt,  0, Piece(pieces.at(10)), own_controls.at(10), opp_controls.at(10)));;
    add_feature(kDropNeighbors(pt,  1, Piece(pieces.at(11)), own_controls.at(11), opp_controls.at(11)));;
    add_feature(kDropNeighbors(pt,  2, Piece(pieces.at(12)), own_controls.at(12), opp_controls.at(12)));;
    add_feature(kDropNeighbors(pt,  3, Piece(pieces.at(13)), own_controls.at(13), opp_controls.at(13)));;
    add_feature(kDropNeighbors(pt,  4, Piece(pieces.at(14)), own_controls.at(14), opp_controls.at(14)));;

    // 周囲8マス（縦3マスx横3マス）の駒の配置（段ごとに場合分け） [指した駒][段][方向（左右対称）][近傍の駒]
    {
      Rank rank = dst.rank();
      // 10   5   0
      // 11   6   1     0  3  0
      // 12   7   2  => 1     1
      // 13   8   3     2  4  2
      // 14   9   4
      add_feature(kDropNeighborsAndRank(pt, rank, 0, Piece(pieces.at( 1))));
      add_feature(kDropNeighborsAndRank(pt, rank, 1, Piece(pieces.at( 2))));
      add_feature(kDropNeighborsAndRank(pt, rank, 2, Piece(pieces.at( 3))));
      add_feature(kDropNeighborsAndRank(pt, rank, 3, Piece(pieces.at( 6))));
      add_feature(kDropNeighborsAndRank(pt, rank, 4, Piece(pieces.at( 8))));
      add_feature(kDropNeighborsAndRank(pt, rank, 0, Piece(pieces.at(11))));
      add_feature(kDropNeighborsAndRank(pt, rank, 1, Piece(pieces.at(12))));
      add_feature(kDropNeighborsAndRank(pt, rank, 2, Piece(pieces.at(13))));
    }

    // 敵・味方の利き数の組み合わせ [指した駒][味方の利き数（最大2）][敵の利き数（最大2）]
    {
      int dst_own_controls = std::min(old_eb.num_controls(kColor, to), 2);
      int dst_opp_controls = std::min(old_eb.num_controls(~kColor, to), 2);
      add_feature(kDropControls(pt, dst_own_controls, dst_opp_controls));
    }

    // 敵・味方の長い利きの数（0から2まで） [指した駒][利き数（最大2）][どちらの利きか]
    {
      int dst_own_controls = std::min(old_eb.long_controls(kColor, to).count(), 2);
      int dst_opp_controls = std::min(old_eb.long_controls(~kColor, to).count(), 2);
      add_feature(kDropOwnLongControls(pt, dst_own_controls));
      add_feature(kDropOppLongControls(pt, dst_opp_controls));
    }

    // 1手前との関係 [今回指した駒][1手前に指した駒][1手前の移動先との位置関係]
    if (previous_move1.is_real_move()) {
      PieceType previous_pt = previous_move1.piece_type();
      Square previous_dst = previous_move1.to().relative_square(kColor);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kDropRelationToPreviousMove1(pt, previous_pt, dst_relation));
    }
    // 2手前との関係 [今回指した駒][2手前に指した駒][2手前の移動先との位置関係]
    if (previous_move2.is_real_move()) {
      PieceType previous_pt = previous_move2.piece_type();
      Square previous_dst = previous_move2.to().relative_square(kColor);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kDropRelationToPreviousMove2(pt, previous_pt, dst_relation));
    }
    // 3手前との関係 [今回指した駒][3手前に指した駒][3手前の移動先との位置関係]
    if (previous_move3.is_real_move()) {
      PieceType previous_pt = previous_move3.piece_type();
      Square previous_dst = previous_move3.to().relative_square(kColor);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kDropRelationToPreviousMove3(pt, previous_pt, dst_relation));
    }
    // 4手前との関係 [今回指した駒][4手前に指した駒][4手前の移動先との位置関係]
    if (previous_move4.is_real_move()) {
      PieceType previous_pt = previous_move4.piece_type();
      Square previous_dst = previous_move4.to().relative_square(kColor);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kDropRelationToPreviousMove4(pt, previous_pt, dst_relation));
    }

    if (   !move_gives_check
        && see_sign == 0) {
      // 味方の飛車・龍との相対位置
      for (Bitboard own_rooks = pos.pieces(kColor, kRook, kDragon); own_rooks.any(); ) {
        Square rook_sq = own_rooks.pop_first_one().relative_square(kColor);
        add_feature(kDropRelationToOwnRook(pt, Square::relation(dst, rook_sq)));
      }

      // 相手の飛車・龍との相対位置
      for (Bitboard opp_rooks = pos.pieces(~kColor, kRook, kDragon); opp_rooks.any(); ) {
        Square rook_sq = opp_rooks.pop_first_one().relative_square(kColor);
        add_feature(kDropRelationToOppRook(pt, Square::relation(dst, rook_sq)));
      }
    }

    // 自玉との相対位置
    int dst_relation_to_own_king = Square::relation(dst, own_ksq.relative_square(kColor));
    add_feature(kDropRelationToOwnKing(pt, dst_relation_to_own_king));

    // 敵玉との相対位置
    int dst_relation_to_opp_king = Square::relation(dst, opp_ksq.relative_square(kColor));
    add_feature(kDropRelationToOppKing(pt, dst_relation_to_opp_king));

    // 自玉との絶対位置 [自玉の位置][指した駒][マス]
    {
      Square own_ksq_m = own_ksq.relative_square(kColor);
      Square dst_m = dst;
      if (own_ksq_m.file() > kFile5) {
        own_ksq_m = Square::mirror_horizontal(own_ksq_m);
        dst_m = Square::mirror_horizontal(dst_m);
      }
      add_feature(kDropAbsRelationToOwnKing(own_ksq_m, pt, dst_m));
    }

    // 敵玉との絶対位置 [敵玉の位置][指した駒][マス]
    {
      Square opp_ksq_m = opp_ksq.relative_square(kColor);
      Square dst_m = dst;
      if (opp_ksq_m.file() > kFile5) {
        opp_ksq_m = Square::mirror_horizontal(opp_ksq_m);
        dst_m = Square::mirror_horizontal(dst_m);
      }
      add_feature(kDropAbsRelationToOppKing(opp_ksq_m, pt, dst_m));
    }

  } else {
    //
    // 移動手の特徴
    //
    // 移動元・移動先に関する特徴（後述）
    // 移動方向
    //   自玉との距離の増減 [移動元と自玉との距離][自玉との距離の増減]
    //   敵玉との距離の増減 [移動元と敵玉との距離][敵玉との距離の増減]
    //   移動方向（前か、横か、後ろか） [移動元の段][Y座標の増減]
    //   5筋とのX距離の増減
    //   そっぽ手判定（自陣の駒） [移動元と自玉とのX距離][自玉とのX距離の増減]
    //   そっぽ手判定（敵陣の駒） [移動元と敵玉とのX距離][敵玉とのX距離の増減]
    // 駒損の脅威の度合い
    // 動くと取られてしまう最高の駒（原因：ピン）
    // 動くと取られてしまう最高の駒（原因：ひもが外れる）

    Square to = move.to();
    Square from = move.from();

    {
      // 移動先周囲15マス（縦5マスx横3マス）の駒の配置 [指した駒][方向（左右対称）][近傍の駒][味方利きの有無][相手利きの有無]
      FifteenNeighborhoods pieces = new_eb.GetFifteenNeighborhoodPieces(to);
      FifteenNeighborhoods own_controls = new_eb.GetFifteenNeighborhoodControls(kColor, to).LimitTo(1);
      FifteenNeighborhoods opp_controls = new_eb.GetFifteenNeighborhoodControls(~kColor, to).LimitTo(1);
      if (kColor == kWhite) {
        pieces = pieces.Rotate180().FlipPieceColors();
        own_controls = own_controls.Rotate180();
        opp_controls = opp_controls.Rotate180();
      }
      add_feature(kDstNeighbors(pt,  0, Piece(pieces.at( 0)), own_controls.at( 0), opp_controls.at( 0)));
      add_feature(kDstNeighbors(pt,  1, Piece(pieces.at( 1)), own_controls.at( 1), opp_controls.at( 1)));
      add_feature(kDstNeighbors(pt,  2, Piece(pieces.at( 2)), own_controls.at( 2), opp_controls.at( 2)));
      add_feature(kDstNeighbors(pt,  3, Piece(pieces.at( 3)), own_controls.at( 3), opp_controls.at( 3)));
      add_feature(kDstNeighbors(pt,  4, Piece(pieces.at( 4)), own_controls.at( 4), opp_controls.at( 4)));
      add_feature(kDstNeighbors(pt,  5, Piece(pieces.at( 5)), own_controls.at( 5), opp_controls.at( 5)));
      add_feature(kDstNeighbors(pt,  6, Piece(pieces.at( 6)), own_controls.at( 6), opp_controls.at( 6)));
      add_feature(kDstNeighbors(pt,  7, Piece(pieces.at( 7)), own_controls.at( 7), opp_controls.at( 7)));
      add_feature(kDstNeighbors(pt,  8, Piece(pieces.at( 8)), own_controls.at( 8), opp_controls.at( 8)));
      add_feature(kDstNeighbors(pt,  9, Piece(pieces.at( 9)), own_controls.at( 9), opp_controls.at( 9)));
      add_feature(kDstNeighbors(pt,  0, Piece(pieces.at(10)), own_controls.at(10), opp_controls.at(10)));
      add_feature(kDstNeighbors(pt,  1, Piece(pieces.at(11)), own_controls.at(11), opp_controls.at(11)));
      add_feature(kDstNeighbors(pt,  2, Piece(pieces.at(12)), own_controls.at(12), opp_controls.at(12)));
      add_feature(kDstNeighbors(pt,  3, Piece(pieces.at(13)), own_controls.at(13), opp_controls.at(13)));
      add_feature(kDstNeighbors(pt,  4, Piece(pieces.at(14)), own_controls.at(14), opp_controls.at(14)));

      // 周囲8マス（縦3マスx横3マス）の駒の配置（段ごとに場合分け） [指した駒][段][方向（左右対称）][近傍の駒]
      Rank rank = dst.rank();
      // 10   5   0
      // 11   6   1     0  3  0
      // 12   7   2  => 1     1
      // 13   8   3     2  4  2
      // 14   9   4
      add_feature(kDstNeighborsAndRank(pt, rank, 0, Piece(pieces.at( 1))));
      add_feature(kDstNeighborsAndRank(pt, rank, 1, Piece(pieces.at( 2))));
      add_feature(kDstNeighborsAndRank(pt, rank, 2, Piece(pieces.at( 3))));
      add_feature(kDstNeighborsAndRank(pt, rank, 3, Piece(pieces.at( 6))));
      add_feature(kDstNeighborsAndRank(pt, rank, 4, Piece(pieces.at( 8))));
      add_feature(kDstNeighborsAndRank(pt, rank, 0, Piece(pieces.at(11))));
      add_feature(kDstNeighborsAndRank(pt, rank, 1, Piece(pieces.at(12))));
      add_feature(kDstNeighborsAndRank(pt, rank, 2, Piece(pieces.at(13))));
    }

    {
      // 移動元周囲15マス（縦5マスx横3マス）の駒の配置 [指した駒][方向（左右対称）][近傍の駒][味方利きの有無][相手利きの有無]
      FifteenNeighborhoods pieces = old_eb.GetFifteenNeighborhoodPieces(from);
      FifteenNeighborhoods own_controls = old_eb.GetFifteenNeighborhoodControls(kColor, to).LimitTo(1);
      FifteenNeighborhoods opp_controls = old_eb.GetFifteenNeighborhoodControls(~kColor, to).LimitTo(1);
      if (kColor == kWhite) {
        pieces = pieces.Rotate180().FlipPieceColors();
        own_controls = own_controls.Rotate180();
        opp_controls = opp_controls.Rotate180();
      }
      add_feature(kSrcNeighbors(pt,  0, Piece(pieces.at( 0)), own_controls.at( 0), opp_controls.at( 0)));
      add_feature(kSrcNeighbors(pt,  1, Piece(pieces.at( 1)), own_controls.at( 1), opp_controls.at( 1)));
      add_feature(kSrcNeighbors(pt,  2, Piece(pieces.at( 2)), own_controls.at( 2), opp_controls.at( 2)));
      add_feature(kSrcNeighbors(pt,  3, Piece(pieces.at( 3)), own_controls.at( 3), opp_controls.at( 3)));
      add_feature(kSrcNeighbors(pt,  4, Piece(pieces.at( 4)), own_controls.at( 4), opp_controls.at( 4)));
      add_feature(kSrcNeighbors(pt,  5, Piece(pieces.at( 5)), own_controls.at( 5), opp_controls.at( 5)));
      add_feature(kSrcNeighbors(pt,  6, Piece(pieces.at( 6)), own_controls.at( 6), opp_controls.at( 6)));
      add_feature(kSrcNeighbors(pt,  7, Piece(pieces.at( 7)), own_controls.at( 7), opp_controls.at( 7)));
      add_feature(kSrcNeighbors(pt,  8, Piece(pieces.at( 8)), own_controls.at( 8), opp_controls.at( 8)));
      add_feature(kSrcNeighbors(pt,  9, Piece(pieces.at( 9)), own_controls.at( 9), opp_controls.at( 9)));
      add_feature(kSrcNeighbors(pt,  0, Piece(pieces.at(10)), own_controls.at(10), opp_controls.at(10)));
      add_feature(kSrcNeighbors(pt,  1, Piece(pieces.at(11)), own_controls.at(11), opp_controls.at(11)));
      add_feature(kSrcNeighbors(pt,  2, Piece(pieces.at(12)), own_controls.at(12), opp_controls.at(12)));
      add_feature(kSrcNeighbors(pt,  3, Piece(pieces.at(13)), own_controls.at(13), opp_controls.at(13)));
      add_feature(kSrcNeighbors(pt,  4, Piece(pieces.at(14)), own_controls.at(14), opp_controls.at(14)));

      // 周囲8マス（縦3マスx横3マス）の駒の配置（段ごとに場合分け） [指した駒][段][方向（左右対称）][近傍の駒]
      Rank rank = dst.rank();
      // 10   5   0
      // 11   6   1     0  3  0
      // 12   7   2  => 1     1
      // 13   8   3     2  4  2
      // 14   9   4
      add_feature(kSrcNeighborsAndRank(pt, rank, 0, Piece(pieces.at( 1))));
      add_feature(kSrcNeighborsAndRank(pt, rank, 1, Piece(pieces.at( 2))));
      add_feature(kSrcNeighborsAndRank(pt, rank, 2, Piece(pieces.at( 3))));
      add_feature(kSrcNeighborsAndRank(pt, rank, 3, Piece(pieces.at( 6))));
      add_feature(kSrcNeighborsAndRank(pt, rank, 4, Piece(pieces.at( 8))));
      add_feature(kSrcNeighborsAndRank(pt, rank, 0, Piece(pieces.at(11))));
      add_feature(kSrcNeighborsAndRank(pt, rank, 1, Piece(pieces.at(12))));
      add_feature(kSrcNeighborsAndRank(pt, rank, 2, Piece(pieces.at(13))));
    }

    // 敵・味方の利き数の組み合わせ [指した駒][味方の利き数（最大2）][敵の利き数（最大2）]
    {
      int src_own_controls = std::min(old_eb.num_controls(kColor, from), 2);
      int src_opp_controls = std::min(old_eb.num_controls(~kColor, from), 2);
      int dst_own_controls = std::min(old_eb.num_controls(kColor, to), 2);
      int dst_opp_controls = std::min(old_eb.num_controls(~kColor, to), 2);
      add_feature(kSrcControls(pt, src_own_controls, src_opp_controls));
      add_feature(kDstControls(pt, dst_own_controls, dst_opp_controls));
    }

    // 1手前との関係 [今回指した駒][1手前に指した駒][1手前の移動先との位置関係]
    if (previous_move1.is_real_move()) {
      PieceType previous_pt = previous_move1.piece_type();
      Square previous_dst = previous_move1.to().relative_square(kColor);
      SquareRelation src_relation = Square::relation(src, previous_dst);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kSrcRelationToPreviousMove1(pt, previous_pt, src_relation));
      add_feature(kDstRelationToPreviousMove1(pt, previous_pt, dst_relation));
    }
    // 2手前との関係 [今回指した駒][2手前に指した駒][2手前の移動先との位置関係]
    if (previous_move2.is_real_move()) {
      PieceType previous_pt = previous_move2.piece_type();
      Square previous_dst = previous_move2.to().relative_square(kColor);
      SquareRelation src_relation = Square::relation(src, previous_dst);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kSrcRelationToPreviousMove2(pt, previous_pt, src_relation));
      add_feature(kDstRelationToPreviousMove2(pt, previous_pt, dst_relation));
    }
    // 3手前との関係 [今回指した駒][3手前に指した駒][3手前の移動先との位置関係]
    if (previous_move3.is_real_move()) {
      PieceType previous_pt = previous_move3.piece_type();
      Square previous_dst = previous_move3.to().relative_square(kColor);
      SquareRelation src_relation = Square::relation(src, previous_dst);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kSrcRelationToPreviousMove3(pt, previous_pt, src_relation));
      add_feature(kDstRelationToPreviousMove3(pt, previous_pt, dst_relation));
    }
    // 4手前との関係 [今回指した駒][4手前に指した駒][4手前の移動先との位置関係]
    if (previous_move4.is_real_move()) {
      PieceType previous_pt = previous_move4.piece_type();
      Square previous_dst = previous_move4.to().relative_square(kColor);
      SquareRelation src_relation = Square::relation(src, previous_dst);
      SquareRelation dst_relation = Square::relation(dst, previous_dst);
      add_feature(kSrcRelationToPreviousMove4(pt, previous_pt, src_relation));
      add_feature(kDstRelationToPreviousMove4(pt, previous_pt, dst_relation));
    }

    if (   !move.is_capture_or_promotion()
        && !move_gives_check
        && see_sign == 0) {
      // 味方の飛車・龍との相対位置
      for (Bitboard own_rooks = pos.pieces(kColor, kRook, kDragon); own_rooks.any(); ) {
        Square rook_sq = own_rooks.pop_first_one().relative_square(kColor);
        add_feature(kSrcRelationToOwnRook(pt, Square::relation(src, rook_sq)));
        add_feature(kDstRelationToOwnRook(pt, Square::relation(dst, rook_sq)));
      }

      // 相手の飛車・龍との相対位置
      for (Bitboard opp_rooks = pos.pieces(~kColor, kRook, kDragon); opp_rooks.any(); ) {
        Square rook_sq = opp_rooks.pop_first_one().relative_square(kColor);
        add_feature(kSrcRelationToOppRook(pt, Square::relation(src, rook_sq)));
        add_feature(kDstRelationToOppRook(pt, Square::relation(dst, rook_sq)));
      }
    }

    // 自玉との相対位置
    int src_relation_to_own_king = Square::relation(src, own_ksq.relative_square(kColor));
    int dst_relation_to_own_king = Square::relation(dst, own_ksq.relative_square(kColor));
    add_feature(kSrcRelationToOwnKing(pt, src_relation_to_own_king));
    add_feature(kDstRelationToOwnKing(pt, dst_relation_to_own_king));

    // 敵玉との相対位置
    int src_relation_to_opp_king = Square::relation(src, opp_ksq.relative_square(kColor));
    int dst_relation_to_opp_king = Square::relation(dst, opp_ksq.relative_square(kColor));
    add_feature(kSrcRelationToOppKing(pt, src_relation_to_opp_king));
    add_feature(kDstRelationToOppKing(pt, dst_relation_to_opp_king));

    // 自玉との絶対位置 [自玉の位置][指した駒][マス]
    {
      Square own_ksq_m = own_ksq.relative_square(kColor);
      Square src_m = src;
      Square dst_m = dst;
      if (own_ksq_m.file() > kFile5) {
        own_ksq_m = Square::mirror_horizontal(own_ksq_m);
        src_m = Square::mirror_horizontal(src_m);
        dst_m = Square::mirror_horizontal(dst_m);
      }
      add_feature(kSrcAbsRelationToOwnKing(own_ksq_m, pt, src_m));
      add_feature(kDstAbsRelationToOwnKing(own_ksq_m, pt, dst_m));
    }

    // 敵玉との絶対位置 [敵玉の位置][指した駒][マス]
    {
      Square opp_ksq_m = opp_ksq.relative_square(kColor);
      Square src_m = src;
      Square dst_m = dst;
      if (opp_ksq_m.file() > kFile5) {
        opp_ksq_m = Square::mirror_horizontal(opp_ksq_m);
        src_m = Square::mirror_horizontal(src_m);
        dst_m = Square::mirror_horizontal(dst_m);
      }
      add_feature(kSrcAbsRelationToOppKing(opp_ksq_m, pt, src_m));
      add_feature(kDstAbsRelationToOppKing(opp_ksq_m, pt, dst_m));
    }
  }

  //
  // 駒ごとの指し手の特徴
  //
  switch (pt) {
    case kPawn : {
      const auto relative_dir = [](Direction d) -> Direction {
        return kColor == kBlack ? d : inverse_direction(d);
      };
      const DirectionSet opp_long_controls = old_eb.long_controls(~kColor, move.to());
      const Square delta_n = Square::direction_to_delta(relative_dir(kDirN));
      const Square delta_s = Square::direction_to_delta(relative_dir(kDirS));
      const Square north_sq = move.to() + delta_n;
      const Square south_sq = move.to() + delta_s;

      if (move.is_drop()) {
        Bitboard own_area_or_own_pieces = old_own_pieces | promotion_zone_bb(~kColor);

        if (   opp_long_controls.test(relative_dir(kDirS))
            && rank_bb<kColor, 2, 7>().test(move.to())) {
          Bitboard opp_lance_attacks = lance_attacks_bb(move.to(), old_occ, ~kColor);

          if (opp_lance_attacks.test(own_area_or_own_pieces)) {
            // 直射止めの歩（相手の飛・香の、自陣または味方の駒への直射を止める歩を打つ手）
            if (pos.num_controls(kColor, move.to()) >= 1) {
              add_feature(kPawnDropInterception());

            // 歩を打って、飛香の利きを止める手 [何枚歩を打てば利きが止まるか] （連打の歩を意識）
            } else if (pos.hand(kColor).count(kPawn) >= 2) {
              Bitboard block_point = old_own_controls.andnot(pos.pieces());
              if (opp_lance_attacks.test(block_point)) {
                int min_distance = 8;
                (opp_lance_attacks & old_own_controls).ForEach([&](Square sq) {
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
          }
        }

        // 底歩を打って、飛車の横利きを止める手 [上にある駒]
        if (   move.to().rank() == relative_rank(kColor, kRank9)
            && pos.piece_on(north_sq) != kNoPiece
            && pos.piece_on(north_sq).is(kColor)
            && pos.num_controls(kColor, move.to()) >= 1
            && (opp_long_controls.test(kDirE) || opp_long_controls.test(kDirW))) {
          add_feature(kAnchorByPawn(pos.piece_on(north_sq).type()));
        }

        // 蓋歩（敵の飛車の退路を断つ歩）
        if (   rank_bb<kColor, 5, 8>().test(move.to())
            && pos.piece_on(south_sq) == Piece(~kColor, kRook)
            && pos.num_controls(kColor, move.to()) >= 1) {
          Bitboard rook_moves = rook_attacks_bb(south_sq, new_occ);
          Bitboard blocked = new_opp_pieces | new_own_controls;
          Bitboard safe_rook_evasions = rook_moves.andnot(blocked);
          if (safe_rook_evasions.none()) {
            add_feature(kRookRetreatCutOffByPawn());
          }
        }

      } else if (!move.is_capture_or_promotion()) {
        // 端歩を突く手 [玉とのX距離]
        if (move.to().file() == kFile1 || move.to().file() == kFile9) {
          int x_distance = std::abs(move.to().file() - opp_ksq.file());
          add_feature(kEdgePawnPush(x_distance));
        }
      }

      // 銀バサミの歩
      Bitboard side_bb = rank_bb(move.to().rank()) & neighborhood8_bb(move.to());
      if (   rank_bb<4, 6>().test(move.to())
          && side_bb.test(pos.pieces(~kColor, kSilver))
          && pos.num_controls(~kColor, move.to()) == 0) {
        (pos.pieces(~kColor, kSilver) & side_bb).ForEach([&](Square sq) {
          Bitboard silver_moves = step_attacks_bb(Piece(~kColor, kSilver), sq);
          Bitboard blocked = pos.pieces(~kColor) | new_own_controls;
          Bitboard safe_silver_moves = silver_moves.andnot(blocked);
          if (safe_silver_moves.none()) { // 相手の銀が逃げるマスがない場合
            add_feature(kSilverPincerPawn());
          }
        });
      }
      break;
    }


    //
    // 11. 香の手筋
    //
    case kLance: {
      const Bitboard opp_non_pawn_pieces = pos.pieces(~kColor).andnot(pos.pieces(kPawn));

      // 香車の利きの先にある駒を調べる
      if (   move.is_drop()
          && see_sign == 0
          && new_attacks.test(opp_non_pawn_pieces)) {
        Square target_sq1 = (new_attacks & opp_non_pawn_pieces).first_one();
        PieceType target_pt1 = pos.piece_on(target_sq1).type();

        // 敵の駒に利きを付ける手 [駒の種類][相手が歩で受けられるか否か]（底歩に対する香打ち等を意識）
        Bitboard opp_pawns = pos.pieces(~kColor, kPawn);
        Bitboard same_file = file_bb(move.to().file());
        Bitboard opp_block_candidates = between_bb(move.to(), target_sq1)
                                      & new_opp_controls;
        bool pawn_block_possible =   pos.hand(~kColor).has(kPawn)
                                  && !opp_pawns.test(same_file)
                                  && opp_block_candidates.any();
        add_feature(kAttackByLance(target_pt1, pawn_block_possible));

        // 田楽の香（歩の受けが利かない手に限る）
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

      break;
    }

    //
    // 12. 桂の手筋
    //
    case kKnight: {
      // 控えの桂（駒損しない、打つ手のみ）[桂が跳ねれば当たりになる相手の駒]
      if (move.is_drop() && see_sign == 0) {
        Bitboard non_sacrifice = old_own_controls | ~old_opp_controls;
        Bitboard opp_pawns = pos.pieces(~kColor, kPawn);
        (new_attacks & non_sacrifice & opp_pawns).ForEach([&](Square sq) {
          Bitboard next_attacks = step_attacks_bb(Piece(kColor, kKnight), sq);
          (next_attacks & pos.pieces(~kColor)).ForEach([&](Square attack_sq) {
            PieceType pt_to_be_attacked = pos.piece_on(attack_sq).type();
            add_feature(kKnightTowardsTheRear(pt_to_be_attacked));
          });
        });
      }
      break;
    }

    default:
      break;
  }

  return feature_list;
}

Array<float, kNumContinuousMoveFeatures> ExtractDynamicMoveFeatures(
    const Move move, const HistoryStats& history, const GainsStats& gains,
    const HistoryStats* countermoves_history,
    const HistoryStats* followupmoves_history) {

  const float kMaterialScale = 1.0f / float(Material::exchange_value(kDragon));
  const float kHistoryScale = 1.0f / float(HistoryStats::kMax);

  Array<float, kNumContinuousMoveFeatures> continuous_features;

  // 静かな手の特徴
  if (move.is_quiet()) {
    // history value（-1から+1までの連続値）
    float h = history[move];
    continuous_features[kHistoryValue] = h * kHistoryScale;

    // counter move history value（-1から+1までの連続値）
    if (countermoves_history != nullptr) {
      float cmh = (*countermoves_history)[move];
      continuous_features[kCounterMoveHistoryValue] = cmh * kHistoryScale;
    } else {
      continuous_features[kCounterMoveHistoryValue] = 0.0f;
    }

    // follow-up move history value（-1から+1までの連続値）
    if (followupmoves_history != nullptr) {
      float fmh = (*followupmoves_history)[move];
      continuous_features[kFollowupMoveHistoryValue] = fmh * kHistoryScale;
    } else {
      continuous_features[kFollowupMoveHistoryValue] = 0.0f;
    }

    // evaluation gain（-1＜龍損＞から+1＜龍得＞までの連続値）
    float gain = gains[move];
    continuous_features[kEvaluationGain] = gain * kMaterialScale;
  } else {
    continuous_features[kHistoryValue] = 0.0f;
    continuous_features[kCounterMoveHistoryValue] = 0.0f;
    continuous_features[kFollowupMoveHistoryValue] = 0.0f;
    continuous_features[kEvaluationGain] = 0.0f;
  }

  return continuous_features;
}

MoveFeatureList ExtractMoveFeatures(Move move, const Position& pos,
                                    const PositionInfo& pos_info) {
  return pos.side_to_move() == kBlack
       ? ExtractMoveFeatures<kBlack>(move, pos, pos_info)
       : ExtractMoveFeatures<kWhite>(move, pos, pos_info);
}

PositionInfo::PositionInfo(const Position& pos,
                           const HistoryStats& history_stats,
                           const GainsStats& gains_stats,
                           const HistoryStats* cmh, const HistoryStats* fmh)
    : history(history_stats),
      gains(gains_stats),
      countermoves_history(cmh),
      followupmoves_history(fmh) {
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
