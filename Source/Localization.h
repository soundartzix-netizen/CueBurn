#pragma once

#include "UiKit.h"

// UI文言 (英語 / 日本語)。文字列はすべてUTF-8。

inline const LocEntry kLoc[] = {
    { "rate", "RATE", "速さ",
      "How fast the record moves: the length of one full back-and-forth stroke, in note values. Synced to the host tempo.",
      "レコードを動かす速さ。1往復(押し出して戻す)の長さを、音符で指定します。ホストのテンポに合わせます。" },
    { "depth", "DEPTH", "振り幅",
      "How far the record is pushed forward in each stroke. Bigger is a longer, higher-pitched sweep. In FILL mode this is the base value: drag a block up/down in the timeline to scale its depth.",
      "1回の往復で、レコードをどこまで押し出すか。大きいほど、長く、高い音まで動きます。フィルでは基準の値で、タイムラインのブロックを上下にドラッグすると、ブロックごとに増減できます。" },
    { "human", "HUMAN", "人間味",
      "Adds small natural variations to fills: starts a little late, each stroke's depth and each block's volume drift slightly. 0% is exactly on the grid.",
      "フィルに、人間らしい小さな揺れを足します。少し遅れて入る、ストロークごとに振り幅が揺れる、ブロックごとに音量が揺れる。0%でグリッドどおりに正確になります。" },
    { "cut", "CUT", "カット",
      "Cuts out the slow parts of each stroke with the fader, like a DJ does: only the fast part is heard, short and sharp. Baby-style patterns (fader left open) get half the amount. 0% hears the whole stroke.",
      "DJ がやるように、レコードが遅い所をフェーダーで切ります。速い所だけが短く鋭く聞こえます。フェーダーを開けたまま動きで聴かせるベイビー系のパターンには、半分だけ効きます。0% で動き全体が聞こえます。" },
    { "feel", "FEEL", "重さ",
      "The weight of the record. Low is snappy and tight, high is heavy and smooth.",
      "レコードの重さ。小さいと、キビキビした動き。大きいと、重くてなめらかな動きになります。" },
    { "start", "START", "頭出し",
      "Sample only. Where on the sample the scratch starts (the cue point). You can also click the waveform.",
      "サンプル用。サンプルのどこからスクラッチを始めるか(頭出しの位置)。波形をクリックしても選べます。" },
    { "tone", "TONE", "トーン",
      "Low-pass filter to soften harsh highs. Fully open means off.",
      "耳に刺さる高音をやわらげるローパスフィルター。一番右で無効です。" },
    { "vinyl", "VINYL", "レコード感",
      "Adds the feel of a real record: needle scuff that follows the scratch speed, dust crackle while the record moves, softened highs and a touch of warmth. Silent when the record stops.",
      "本物のレコードらしさを足します。こする速さに合わせた針の擦れ、動いている間のパチパチ、高域のなまり、ほんの少しの歪み。レコードが止まっている間は鳴りません。" },
    { "level", "LEVEL", "音量",
      "Output volume. Note velocity also changes the volume.",
      "出力の音量。ノートの強さ(ベロシティ)でも、音量が変わります。" },
};

inline const char* const kPatternNames[18][2] = {
    { "BABY", "ベイビー" },
    { "FORWARD", "フォワード" },
    { "TEAR", "ティア" },
    { "CHIRP", "チャープ" },
    { "FLARE", "フレア" },
    { "SCRIBBLE", "スクリブル" },
    { "TRANSFORMER", "トランスフォーマー" },
    { "CRAB", "クラブ" },
    { "ORBIT", "オービット" },
    { "STAB", "スタブ" },
    { "DRAG", "ドラッグ" },
    { "HYDROPLANE", "ハイドロプレーン" },
    { "BRAKE", "ブレーキ" },
    { "ZAP", "ザップ" },
    { "USER 1", "ユーザー1" },
    { "USER 2", "ユーザー2" },
    { "USER 3", "ユーザー3" },
    { "USER 4", "ユーザー4" },
};

inline const char* const kPatternHelp[18][2] = {
    { "The classic scratch: push forward, pull back. Both strokes are heard.",
      "定番のスクラッチ。押し出して、引き戻す。行きも帰りも聞こえます。" },
    { "A quick push forward (heard), then a slow pull back (muted).",
      "素早く押し出して(聞こえる)、ゆっくり引き戻す(ミュート)。" },
    { "The pull-back is split into two jerky steps, for a ripping sound.",
      "引き戻しを2段階に分けた、引きちぎるようなスクラッチ。" },
    { "A short burst at the fastest part of the push, like a bird chirp.",
      "押し出す一番速い瞬間だけを短く鳴らす、鳥のさえずりのようなスクラッチ。" },
    { "The push is cut up by quick on/off clicks of the crossfader.",
      "押し出す途中を、クロスフェーダーの素早い開け閉めで刻みます。" },
    { "Very fast, tiny shakes of the record for a buzzing sound.",
      "レコードを小刻みに素早く揺らす、ビリビリした音。" },
    { "A baby scratch chopped into 8 quick pieces by the crossfader, like a robot.",
      "ベイビーの動きを、クロスフェーダーで8つに細かく刻む。ロボットのような音。" },
    { "4 rapid clicks during the push, like fingers tapping the fader.",
      "押し出す間に、4回の素早いクリック。指でフェーダーを弾くような音。" },
    { "One click going out and one coming back. Rolling and round.",
      "行きと帰りで、1回ずつクリック。転がるような丸いリズム。" },
    { "A sharp, very short stab, then a silent slow return.",
      "一瞬だけ鋭く突いて、ゆっくり(無音で)戻す。アクセント向き。" },
    { "A slow, shallow move for a low, growling sound.",
      "ゆっくり浅く動かす、低く唸るような音。" },
    { "A baby scratch with a fine buzzing wobble, like rubbing the record.",
      "ベイビーに細かい揺れを重ねた、レコードを擦ってビリつかせる音。" },
    { "A fast push that brakes to a crawl: the pitch only falls (no rise), then stays low. Like a zap that sinks.",
      "勢いよく押し出して、ブレーキをかけるように遅くする。音は高い所から下がるだけ(上がらない)で、低く残ります。" },
    { "A short version of BRAKE: just the falling part, then the fader closes. Good for quick cuts.",
      "ブレーキの短い版。下がる所だけ鳴らして、フェーダーを閉じます。細かく刻むのに向きます。" },
    { "Your own pattern. Draw it in the WAVE EDITOR.", "自分で描いたパターン。「波形エディタ」で描けます。" },
    { "Your own pattern. Draw it in the WAVE EDITOR.", "自分で描いたパターン。「波形エディタ」で描けます。" },
    { "Your own pattern. Draw it in the WAVE EDITOR.", "自分で描いたパターン。「波形エディタ」で描けます。" },
    { "Your own pattern. Draw it in the WAVE EDITOR.", "自分で描いたパターン。「波形エディタ」で描けます。" },
};

inline const char* const kSourceNames[5][2] = {
    { "SINE", "サイン" },
    { "SAW", "ノコギリ" },
    { "SQUARE", "矩形" },
    { "NOISE", "ノイズ" },
    { "SAMPLE", "サンプル" },
};

inline const char* const kSourceHelp[5][2] = {
    { "A pure sine tone. Smooth, whistling scratches.",
      "純粋なサイン波。なめらかで、口笛のようなスクラッチ。" },
    { "A bright sawtooth tone. Buzzy, synth-like scratches.",
      "明るいノコギリ波。ブザーのような、シンセらしいスクラッチ。" },
    { "A hollow square tone. Retro, game-like scratches.",
      "中が抜けた矩形波。レトロなゲーム機のようなスクラッチ。" },
    { "White noise. The classic noise scratch sound.",
      "ホワイトノイズ。定番のノイズ・スクラッチの音。" },
    { "Your own sound. Drop an audio file onto the window, or use LOAD.",
      "自分の音。オーディオファイルをウィンドウにドロップするか、「読み込む」から選びます。" },
};

inline const char* const kPanelTitles[3][2] = {
    { "MOTION", "動き" },
    { "SOUND", "音" },
    { "OUTPUT", "出力" },
};

inline const char* const kRecordTitle[2] = { "RECORD", "レコード" };
inline const char* const kSourceTitle[2] = { "SOURCE", "音源" };
inline const char* const kPatternTitle[2] = { "PATTERN", "パターン" };

inline const char* const kTemplateNames[37][2] = {
    { "Baby Basic", "ベイビー基本" },
    { "Chirp Groove", "チャープ・グルーヴ" },
    { "Forward Hits", "フォワード連打" },
    { "Flare Roll", "フレア・ロール" },
    { "Tear Break", "ティア・ブレイク" },
    { "Chirp Rise", "チャープ上昇" },
    { "Stutter Cuts", "スタッター" },
    { "Call & Response", "コール&レスポンス" },
    { "Double Time", "倍速" },
    { "Half-Time", "ハーフタイム" },
    { "Chirp + Flare", "チャープ+フレア" },
    { "Build Up", "ビルドアップ" },
    { "Drop Stop", "ドロップ・ストップ" },
    { "Syncopated", "シンコペーション" },
    { "Minimal", "ミニマル" },
    { "Crazy", "クレイジー" },
    { "Transformer Run", "トランスフォーマー" },
    { "Crab Combo", "クラブ・コンボ" },
    { "Orbit Flow", "オービット" },
    { "Slow Drag", "スロー・ドラッグ" },
    { "Boom Bap", "ブーンバップ" },
    { "Breakbeat", "ブレイクビーツ" },
    { "Drum & Bass", "ドラムンベース" },
    { "Jungle", "ジャングル" },
    { "Ragga (Dembow)", "ラガ (デンボウ)" },
    { "Trap", "トラップ" },
    { "House", "ハウス" },
    { "Dubstep", "ダブステップ" },
    { "Funk", "ファンク" },
    { "Trap (Triplets)", "トラップ (3連)" },
    { "Shuffle (Triplets)", "シャッフル (3連)" },
    { "Brake Groove", "ブレーキ・グルーヴ" },
    { "Zap Cuts", "ザップ・カット" },
    { "Vocal Chop", "ボーカル・チョップ" },
    { "Empty", "空 (何も置かない)" },
    { "Sound Artz Set1", "Sound Artz Set1" },
    { "Sound Artz Set2", "Sound Artz Set2" },
};

inline const char* const kPatternShort[18] = { "BABY", "FWD", "TEAR", "CHIRP", "FLARE", "SCRBL",
                                                "TRANS", "CRAB", "ORBIT", "STAB", "DRAG", "HYDRO", "BRAKE", "ZAP",
                                                "USR1", "USR2", "USR3", "USR4" };

inline const char* const kRateNames[9] = { "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T" };

inline const char* const kFillTitle[2] = { "FILL", "フィル" };
