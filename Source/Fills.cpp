// フィル(フレーズ): テンプレート・ランダム・バリエーションと、フレーズの管理
#include "PluginProcessor.h"
#include "FactoryPatterns.h"

namespace
{
using FE = ScratchProcessor::FillEvent;

// パターン: 0=Baby 1=Forward 2=Tear 3=Chirp 4=Flare 5=Scribble
//           6=Transformer 7=Crab 8=Orbit 9=Stab 10=Drag 11=Hydroplane 12=Brake 13=Zap
// 速さ:     0=1/2 1=1/4 2=1/8 3=1/16 4=1/32 / 3連符: 5=1/4T 6=1/8T 7=1/16T 8=1/32T
// { 開始, 長さ, パターン, 速さ, 振り幅, 高さ, ずらし(ms), 鳴らす長さ(0.1〜1) }  (1小節 = 16ステップ。3連符は小数で書く)
constexpr float T8 = 4.0f / 3.0f; // 3連8分 = 16分音符 1.333個ぶん
// 振り幅と鳴らす長さは、ブロックごとに強弱をつけてある (全部同じだと、機械的に聞こえるので)
struct Motif
{
    std::vector<std::array<float, 8>> ev;
};

const Motif& motif (int index)
{
    static const Motif m[ScratchProcessor::numTemplates] = {
        { { { 0, 8, 0, 2, 1.0f, 0, 0, 0.90f }, { 10, 6, 0, 3, 0.65f, 0, 0, 0.75f } } },                                    // Baby Basic
        { { { 0, 4, 3, 2, 1.25f, 0, 0, 0.85f }, { 6, 2, 3, 3, 0.7f, 0, 0, 0.6f }, { 8, 4, 3, 2, 1.0f, 0, 0, 0.85f },
            { 14, 2, 3, 3, 0.8f, 2, 0, 0.5f } } },                                                                           // Chirp Groove
        { { { 0, 2, 1, 3, 1.3f, 0, 0, 0.8f }, { 3, 2, 1, 3, 0.75f, 0, 0, 0.7f }, { 6, 2, 1, 3, 1.0f, 0, 0, 0.8f },
            { 8, 2, 1, 3, 1.35f, 0, 0, 0.8f }, { 11, 2, 1, 3, 0.65f, 0, 0, 0.6f }, { 14, 2, 1, 3, 1.0f, 2, 0, 0.7f } } },    // Forward Hits
        { { { 0, 8, 4, 1, 1.25f, 0, 0, 1.0f }, { 8, 8, 4, 2, 0.85f, 0, 0, 0.9f } } },                                      // Flare Roll
        { { { 0, 8, 2, 1, 1.15f, 0, 0, 1.0f }, { 8, 4, 0, 2, 0.75f, 0, 0, 0.8f }, { 12, 4, 2, 2, 1.0f, 0, 0, 0.9f } } },    // Tear Break
        { { { 0, 2, 3, 2, 0.75f, 0, 0, 1.0f }, { 3, 1, 3, 3, 0.6f, 0, 0, 1.0f }, { 4, 2, 3, 2, 0.85f, 2, 0, 1.0f },
            { 7, 1, 3, 3, 0.65f, 2, 0, 1.0f }, { 8, 2, 3, 2, 0.95f, 4, 0, 1.0f }, { 11, 1, 3, 3, 0.7f, 4, 0, 1.0f },
            { 12, 2, 3, 2, 1.1f, 7, 0, 1.0f }, { 14, 2, 1, 3, 1.2f, 7, 0, 1.0f } } },                                       // Chirp Rise
        { { { 0, 1, 3, 3, 0.75f, 0, 0, 1.0f }, { 2, 1, 3, 3, 0.9f, 0, 0, 1.0f }, { 4, 1, 3, 3, 0.7f, 0, 0, 1.0f },
            { 6, 1, 3, 3, 1.0f, 0, 0, 1.0f }, { 8, 4, 3, 2, 1.25f, 0, 0, 0.85f }, { 12, 4, 0, 3, 0.8f, 0, 0, 0.75f } } },    // Stutter Cuts
        { { { 0, 6, 3, 2, 1.3f, 0, 0, 0.9f }, { 8, 6, 3, 2, 0.8f, -2, 120, 0.8f } } },                                     // Call & Response
        { { { 0, 8, 0, 3, 0.55f, 0, 0, 0.95f }, { 8, 8, 0, 3, 0.9f, 0, 0, 0.95f } } },                                     // Double Time
        { { { 0, 8, 0, 1, 1.3f, 0, 0, 1.0f }, { 8, 8, 1, 2, 0.95f, 0, 0, 0.8f } } },                                       // Half-Time
        { { { 0, 4, 3, 2, 1.2f, 0, 0, 0.85f }, { 4, 4, 4, 2, 0.8f, 0, 0, 0.7f }, { 8, 4, 3, 2, 1.0f, 0, 0, 0.85f },
            { 12, 4, 4, 2, 1.35f, 0, 0, 0.75f } } },                                                                         // Chirp + Flare
        { { { 0, 4, 0, 1, 0.7f, 0, 0, 0.9f }, { 4, 4, 0, 2, 0.9f, 2, 0, 0.9f }, { 8, 4, 0, 3, 1.1f, 4, 0, 0.9f },
            { 12, 4, 3, 3, 1.3f, 7, 0, 0.9f } } },                                                                           // Build Up
        { { { 0, 12, 1, 2, 0.95f, 0, 0, 0.9f }, { 12, 4, 2, 0, 1.5f, -5, 0, 1.0f } } },                                    // Drop Stop
        { { { 0, 3, 0, 2, 1.1f, 0, 0, 0.8f }, { 3, 3, 0, 2, 0.7f, 0, 0, 0.7f }, { 6, 2, 3, 3, 1.3f, 0, 0, 0.6f },
            { 10, 3, 0, 2, 0.9f, 0, 0, 0.8f }, { 13, 3, 0, 2, 1.2f, 2, 0, 0.7f } } },                                        // Syncopated
        { { { 0, 2, 3, 2, 1.3f, 0, 0, 0.8f }, { 8, 2, 3, 2, 0.8f, 0, 0, 0.7f } } },                                        // Minimal
        { { { 0, 4, 1, 3, 0.8f, 0, 0, 0.9f }, { 4, 4, 2, 3, 1.3f, 0, 0, 0.6f }, { 8, 4, 4, 3, 1.0f, 2, 0, 0.8f },
            { 12, 4, 3, 3, 0.9f, 5, 0, 0.7f } } },                                                                           // Crazy
        { { { 0, 4, 6, 1, 1.2f, 0, 0, 1.0f }, { 4, 4, 6, 2, 0.8f, 0, 0, 0.9f }, { 8, 2, 9, 3, 1.3f, 0, 0, 0.7f },
            { 10, 2, 9, 3, 0.9f, 0, 0, 0.6f }, { 12, 4, 6, 2, 1.0f, 2, 0, 0.85f } } },                                      // Transformer Run
        { { { 0, 4, 7, 1, 1.25f, 0, 0, 0.9f }, { 4, 2, 3, 3, 0.8f, 0, 0, 0.6f }, { 6, 2, 3, 3, 0.7f, 0, 0, 0.6f },
            { 8, 4, 7, 1, 1.1f, 2, 0, 0.9f }, { 12, 4, 8, 2, 0.9f, 0, 0, 0.85f } } },                                       // Crab Combo
        { { { 0, 4, 8, 2, 1.2f, 0, 0, 1.0f }, { 4, 4, 8, 2, 0.75f, 0, 0, 0.9f }, { 8, 4, 8, 2, 1.0f, 0, 0, 0.9f },
            { 12, 2, 9, 3, 1.3f, 2, 0, 0.6f }, { 14, 2, 9, 3, 0.9f, 4, 0, 0.5f } } },                                       // Orbit Flow
        { { { 0, 8, 10, 0, 1.3f, -5, 0, 1.0f }, { 8, 4, 11, 2, 0.9f, 0, 0, 0.9f }, { 12, 4, 10, 1, 1.1f, -2, 0, 0.8f } } }, // Slow Drag

        // ---- ジャンル別: そのジャンルのキック・スネアの位置や、シンコペーションに合わせて置く ----
        // Boom Bap: キック 1拍目・3拍目、スネア 2拍目・4拍目に答える
        { { { 0, 3, 1, 2, 1.3f, 0, 0, 0.8f }, { 4, 2, 3, 2, 1.1f, 0, 0, 0.85f }, { 7, 1, 9, 3, 0.8f, 0, 0, 1.0f },
            { 8, 3, 0, 2, 1.0f, 0, 0, 0.75f }, { 12, 2, 3, 2, 1.25f, 0, 0, 0.85f }, { 14, 2, 7, 2, 0.8f, 2, 0, 0.9f } } },
        // Breakbeat: アーメン風。キック 1・1と・3と、スネア 2・4 と、間のゴーストノート
        { { { 0, 2, 9, 3, 1.35f, 0, 0, 1.0f }, { 2, 2, 0, 3, 0.8f, 0, 0, 0.9f }, { 4, 3, 1, 2, 1.2f, 0, 0, 0.85f },
            { 7, 1, 3, 3, 0.9f, 0, 0, 1.0f }, { 9, 1, 3, 3, 0.7f, 0, 0, 1.0f }, { 10, 2, 0, 3, 1.0f, 0, 0, 0.85f },
            { 12, 3, 4, 2, 1.3f, 0, 0, 0.9f }, { 15, 1, 9, 3, 0.8f, 2, 0, 1.0f } } },
        // Drum & Bass: 2ステップ (キック 1・3と、スネア 2・4)。速いテンポ向けに、細かく刻む
        { { { 0, 4, 6, 2, 1.2f, 0, 0, 1.0f }, { 4, 2, 9, 3, 1.35f, 0, 0, 1.0f }, { 6, 2, 3, 3, 0.7f, 0, 0, 1.0f },
            { 10, 2, 1, 3, 1.1f, 0, 0, 0.85f }, { 12, 2, 9, 3, 1.4f, 0, 0, 1.0f }, { 14, 2, 3, 3, 0.8f, 2, 0, 1.0f } } },
        // Jungle: ブレイクを細かく切り刻む。1/32 のロールを混ぜる
        { { { 0, 2, 9, 3, 1.4f, 0, 0, 1.0f }, { 2, 2, 6, 3, 0.8f, 0, 0, 1.0f }, { 4, 1, 9, 3, 1.2f, 0, 0, 1.0f },
            { 5, 3, 3, 3, 0.75f, 0, 0, 1.0f }, { 8, 2, 2, 2, 1.1f, 0, 0, 0.9f }, { 10, 2, 9, 3, 1.3f, -2, 0, 1.0f },
            { 12, 2, 6, 3, 1.0f, 0, 0, 1.0f }, { 14, 2, 3, 3, 0.9f, 3, 0, 1.0f } } },
        // Ragga: ダンスホールの 3+3+2 (デンボウ)。キック 1・3、スネア 1拍目の「あ」・2拍目の「と」…
        { { { 0, 2, 1, 2, 1.1f, 0, 0, 0.8f }, { 3, 2, 3, 3, 1.3f, 0, 0, 0.9f }, { 6, 2, 3, 3, 1.2f, 0, 0, 0.9f },
            { 8, 2, 1, 2, 0.85f, 0, 0, 0.8f }, { 11, 2, 3, 3, 1.3f, 2, 0, 0.9f }, { 14, 2, 7, 3, 1.1f, 2, 0, 0.9f } } },
        // Trap: ハーフタイム (スネア 3拍目)。低いドラッグと、ハイハットのような 1/32 ロール
        { { { 0, 4, 10, 1, 1.3f, -5, 0, 0.9f }, { 4, 2, 4, 3, 0.9f, 0, 0, 0.9f }, { 6, 2, 3, 3, 0.7f, 0, 0, 1.0f },
            { 8, 2, 9, 3, 1.4f, 0, 0, 1.0f }, { 10, 2, 11, 3, 0.9f, 0, 0, 1.0f }, { 12, 2, 3, 3, 0.6f, 0, 0, 1.0f },
            { 14, 2, 3, 3, 0.9f, 3, 0, 1.0f } } },
        // House: 4つ打ちの裏 (オフビート) に合わせる
        { { { 2, 2, 3, 2, 1.2f, 0, 0, 0.85f }, { 6, 2, 3, 2, 0.85f, 0, 0, 0.85f }, { 10, 2, 3, 2, 1.3f, 0, 0, 0.85f },
            { 13, 3, 8, 2, 1.0f, 2, 0, 0.9f } } },
        // Dubstep: ハーフタイム。低く唸るドラッグとハイドロ、スネア(3拍目)にスタブ
        { { { 0, 4, 10, 1, 1.4f, -7, 0, 1.0f }, { 4, 4, 11, 2, 1.0f, -5, 0, 0.9f }, { 8, 2, 9, 3, 1.4f, 0, 0, 1.0f },
            { 10, 2, 6, 4, 0.9f, 0, 0, 1.0f }, { 12, 4, 2, 1, 1.2f, -2, 0, 0.9f } } },
        // Funk: 16分のシンコペーションと、小さなゴーストノート
        { { { 0, 2, 1, 3, 1.3f, 0, 0, 0.9f }, { 3, 1, 3, 3, 0.7f, 0, 0, 1.0f }, { 4, 2, 0, 3, 1.1f, 0, 0, 0.85f },
            { 6, 1, 3, 3, 0.6f, 0, 0, 1.0f }, { 7, 2, 1, 3, 1.0f, 0, 0, 0.9f }, { 10, 2, 0, 3, 1.2f, 0, 0, 0.85f },
            { 12, 1, 3, 3, 0.7f, 0, 0, 1.0f }, { 13, 3, 7, 2, 1.1f, 2, 0, 0.9f } } },
        // Trap (3連): 3拍目のスネアに3連8分、2拍目は3連16分のロール、最後は3連32分で上がる
        { { { 0, 4, 10, 1, 1.3f, -5, 0, 0.9f }, { 4, 4, 0, 7, 0.7f, 0, 0, 1.0f },
            { 8, T8, 9, 6, 1.4f, 0, 0, 1.0f }, { 8 + T8, T8, 3, 6, 0.9f, 0, 0, 1.0f }, { 8 + 2 * T8, T8, 3, 6, 1.1f, 2, 0, 1.0f },
            { 12, 2, 3, 7, 0.6f, 0, 0, 1.0f }, { 14, 2, 3, 7, 0.9f, 3, 0, 1.0f } } },
        // Shuffle (3連): 3連8分の1つ目と3つ目 (はねるリズム)
        { { { 0, T8, 3, 6, 1.3f, 0, 0, 0.9f }, { 2 * T8, T8, 0, 6, 0.75f, 0, 0, 0.85f },
            { 4, T8, 1, 6, 1.1f, 0, 0, 0.9f }, { 4 + 2 * T8, T8, 3, 6, 0.7f, 0, 0, 0.85f },
            { 8, T8, 3, 6, 1.25f, 0, 0, 0.9f }, { 8 + 2 * T8, T8, 0, 6, 0.85f, 0, 0, 0.85f },
            { 12, T8, 8, 6, 1.0f, 2, 0, 0.9f }, { 12 + 2 * T8, T8, 7, 6, 1.15f, 2, 0, 0.9f } } },
        // Brake Groove: 高い所から下がる音を、8分音符で途切れずに並べる (振り幅で強弱)
        { { { 0, 2, 12, 2, 1.15f, 0, 0, 1.0f }, { 2, 2, 12, 2, 0.95f, 0, 0, 1.0f }, { 4, 2, 12, 2, 1.05f, 0, 0, 1.0f },
            { 6, 2, 12, 2, 0.9f, 0, 0, 1.0f }, { 8, 2, 12, 2, 1.15f, 0, 0, 1.0f }, { 10, 2, 12, 2, 0.95f, 0, 0, 1.0f },
            { 12, 2, 12, 2, 1.05f, 0, 0, 1.0f }, { 14, 2, 12, 2, 0.85f, 0, 0, 1.0f } } },
        // Zap Cuts: 下がる音を短く切って、16分で刻む (休みを入れて、はねる感じに)
        // (16分のザップは1往復が短く、そのままだと高すぎるので、振り幅を小さめに)
        { { { 2, 1, 13, 3, 0.8f, 0, 0, 1.0f }, { 4, 1, 13, 3, 0.62f, 0, 0, 1.0f }, { 6, 1, 13, 3, 0.75f, 0, 0, 1.0f },
            { 7, 1, 13, 3, 0.55f, 0, 0, 1.0f }, { 8, 1, 13, 3, 0.78f, 0, 0, 1.0f }, { 9, 1, 13, 3, 0.6f, 0, 0, 1.0f },
            { 11, 1, 13, 3, 0.7f, 0, 0, 1.0f }, { 12, 1, 13, 3, 0.62f, 0, 0, 1.0f }, { 14, 2, 12, 2, 1.1f, 0, 0, 1.0f } } },
        // Vocal Chop: 声のサンプルを、32分音符くらいの短さで、少し不揃いに刻む (実際の曲の刻みを測って並べたもの)
        // 1つの刻みの中は、レコードを押し出す途中 (ベイビー 1/16 の前半) だけ鳴らす
        { { { 0, 0.5f, 0, 3, 1.0f, 0, 0, 1.0f }, { 1, 0.5f, 0, 3, 1.1f, 0, 0, 1.0f }, { 2.5f, 0.5f, 0, 3, 0.85f, 0, 0, 1.0f },
            { 3, 0.5f, 0, 3, 0.95f, 0, 0, 1.0f }, { 4, 0.5f, 0, 3, 1.05f, 0, 0, 1.0f }, { 5.5f, 1, 0, 3, 1.0f, 0, 0, 0.9f },
            { 7, 0.5f, 0, 3, 0.8f, -2, 0, 1.0f }, { 8.5f, 1, 0, 3, 1.0f, 0, 0, 0.9f }, { 10, 0.5f, 0, 3, 1.1f, 0, 0, 1.0f },
            { 11, 0.5f, 0, 3, 0.95f, 0, 0, 1.0f }, { 13, 0.5f, 0, 3, 0.9f, 0, 0, 1.0f }, { 14.5f, 1, 0, 3, 1.05f, 0, 0, 0.9f } } },
    };
    return m[juce::jlimit (0, ScratchProcessor::numTemplates - 1, index)];
}

FE toEvent (const std::array<float, 8>& a, int offset)
{
    FE e;
    e.start = juce::roundToInt (a[0] * (float) ScratchProcessor::ticksPerStep) + offset;
    e.len = juce::roundToInt (a[1] * (float) ScratchProcessor::ticksPerStep);
    e.pattern = (int) a[2];
    e.rate = (int) a[3];
    e.depth = a[4];
    e.pitch = (int) a[5];
    e.cue = a[6] * 0.001f;
    e.gate = a[7];
    return e;
}

// 小節の最後の1拍を、締めのロール(高さが上がっていく細かいスクラッチ)に置き換える (位置はティック)
void addTurnaround (std::vector<FE>& events, int barStart)
{
    constexpr int st = ScratchProcessor::ticksPerStep;
    const int cut = barStart + 12 * st;
    std::vector<FE> kept;
    for (auto e : events)
    {
        if (e.start >= cut)
            continue;
        if (e.start + e.len > cut)
            e.len = cut - e.start;
        if (e.len > 0)
            kept.push_back (e);
    }
    events = kept;

    // チャープを 16分で、高さを上げながら (スクリブル・32分のベイビーは、耳で聴いて邪魔だったので使わない)
    FE a;
    a.start = cut;
    a.len = 2 * st;
    a.pattern = 3;
    a.rate = 3;
    a.depth = 0.85f;
    a.pitch = 2;
    events.push_back (a);

    FE b;
    b.start = cut + 2 * st;
    b.len = 2 * st;
    b.pattern = 3;
    b.rate = 3;
    b.depth = 1.1f;
    b.pitch = 5;
    events.push_back (b);
}

// ランダム・バリエーションでは、スクリブルは使わない (チャープに置き換える)
int notScribble (int pattern)
{
    return pattern == 5 ? 3 : pattern;
}

// ブロックの長さより後ろのミュートを消す
void clearMuteBeyond (FE& e)
{
    for (int i = juce::jmax (0, e.len); i < ScratchProcessor::maxEventTicks; ++i)
        e.mute.reset ((size_t) i);
}

// 鳴らない部分(ゴースト)が16分音符1つ以上あるブロックは、ブロック自体を縮める (16分音符単位)。鳴り方は変わらない
void trimGhosts (std::vector<FE>& ev)
{
    constexpr int st = ScratchProcessor::ticksPerStep;
    for (auto& e : ev)
    {
        const float audible = (float) e.len * e.gate;
        const int len = juce::jmax (1, (int) std::ceil (audible / (float) st - 1.0e-3f)) * st;
        if (len < e.len)
        {
            e.gate = juce::jlimit (0.1f, 1.0f, audible / (float) len);
            e.len = len;
            clearMuteBeyond (e);
        }
    }
}

void sortEvents (std::vector<FE>& ev)
{
    std::sort (ev.begin(), ev.end(), [] (const FE& a, const FE& b) { return a.start < b.start; });
}

int weighted (juce::Random& r, std::initializer_list<std::pair<int, int>> items)
{
    int total = 0;
    for (auto& it : items)
        total += it.second;
    int x = r.nextInt (juce::jmax (1, total));
    for (auto& it : items)
    {
        if (x < it.second)
            return it.first;
        x -= it.second;
    }
    return items.begin()->first;
}

// ランダムな振り幅: 小・中・大をばらつかせる。アクセント(小節の頭など)は、大きめ
float randomDepth (juce::Random& r, bool accent)
{
    if (accent)
        return 1.15f + 0.35f * r.nextFloat();
    const int k = r.nextInt (10);
    if (k < 3)
        return 0.45f + 0.2f * r.nextFloat();
    if (k < 7)
        return 0.75f + 0.25f * r.nextFloat();
    return 1.05f + 0.3f * r.nextFloat();
}

// ランダムな「鳴らす長さ」: 全部鳴らす/少し切る/短く切る
float randomGate (juce::Random& r)
{
    static const float g[] = { 1.0f, 0.85f, 0.65f, 0.45f };
    return g[weighted (r, { { 0, 4 }, { 1, 3 }, { 2, 2 }, { 3, 1 } })];
}
}

//==============================================================================
ScratchProcessor::Phrase ScratchProcessor::makeTemplate (int index, int bars)
{
    Phrase p;
    p.bars = juce::jlimit (1, 4, bars);
    p.templateIndex = juce::jlimit (0, emptyTemplate + numFactoryPatterns, index);
    if (p.templateIndex == emptyTemplate)
        return p;
    if (p.templateIndex > emptyTemplate)
    {
        // Sound Artz の標準パターン: 1小節を、小節数ぶんくり返す
        const auto xml = juce::parseXML (juce::String::fromUTF8 (kFactoryPatterns[p.templateIndex - emptyTemplate - 1]));
        jassert (xml != nullptr);
        if (xml == nullptr)
            return p;
        const auto bar = phraseFromTree (juce::ValueTree::fromXml (*xml).getChildWithName ("PHRASE"));
        for (int k = 0; k < p.bars; ++k)
            for (auto e : bar.events)
            {
                e.start += k * ticksPerBar;
                p.events.push_back (e);
            }
        return p;
    }

    for (int k = 0; k < p.bars; ++k)
        for (auto& a : motif (p.templateIndex).ev)
        {
            auto e = toEvent (a, k * ticksPerBar);
            if (p.bars == 4 && k == 2)
            {
                e.pitch -= 2; // 3小節目は、少し低くして変化をつける
                e.depth *= 0.85f;
            }
            p.events.push_back (e);
        }

    if (p.bars > 1)
        addTurnaround (p.events, (p.bars - 1) * ticksPerBar);

    sortEvents (p.events);
    trimGhosts (p.events);
    return p;
}

ScratchProcessor::Phrase ScratchProcessor::makeRandom (juce::uint32 seed, int bars)
{
    juce::Random r ((juce::int64) seed);
    Phrase p;
    p.bars = juce::jlimit (1, 4, bars);
    p.isRandom = true;
    p.seed = seed;

    // フレーズ全体の「好み」: よく使うパターン2〜3個と、基本の速さ
    std::vector<int> favorites;
    while (favorites.size() < 2)
    {
        const int pat = weighted (r, { { 0, 3 }, { 3, 3 }, { 1, 2 }, { 4, 2 }, { 2, 2 },
                                       { 6, 2 }, { 7, 2 }, { 8, 2 }, { 9, 2 }, { 10, 1 }, { 11, 1 }, { 12, 2 }, { 13, 2 } });
        if (std::find (favorites.begin(), favorites.end(), pat) == favorites.end())
            favorites.push_back (pat);
    }
    if (r.nextFloat() < 0.3f)
        favorites.push_back (notScribble (r.nextInt (numPresetPatterns)));
    const int baseRate = weighted (r, { { 2, 5 }, { 3, 3 }, { 1, 2 } });

    auto genBar = [&] (int barStart)
    {
        std::vector<FE> out;
        int s = 0;
        while (s < stepsPerBar)
        {
            if (s > 0 && r.nextFloat() < 0.18f) // 休み
            {
                s += 1 + r.nextInt (2);
                continue;
            }
            FE e;
            e.start = barStart + s;
            e.len = juce::jmin (stepsPerBar - s, weighted (r, { { 2, 4 }, { 3, 2 }, { 4, 4 }, { 6, 1 }, { 8, 1 } }));
            e.pattern = favorites[(size_t) r.nextInt ((int) favorites.size())];
            e.rate = baseRate;
            if (r.nextFloat() < 0.2f)
                e.rate = juce::jmin (3, e.rate + 1);
            else if (r.nextFloat() < 0.1f)
                e.rate = juce::jmax (1, e.rate - 1);
            e.depth = randomDepth (r, s == 0 || (s % 8 == 0 && r.nextFloat() < 0.5f));
            e.gate = randomGate (r);
            e.pitch = r.nextFloat() < 0.15f ? (r.nextBool() ? 2 : -2) : 0;
            e.cue = r.nextFloat() < 0.25f ? (r.nextBool() ? 0.08f : 0.16f) : 0.0f;
            out.push_back (e);
            s += e.len;
        }
        return out;
    };

    const auto first = genBar (0);
    p.events = first;
    for (int k = 1; k < p.bars; ++k)
    {
        std::vector<FE> bar;
        if (r.nextFloat() < 0.6f)
        {
            // 1小節目をくり返し、1〜2個だけ変える (振り幅・長さも変える)
            for (auto e : first)
            {
                e.start += k * stepsPerBar;
                bar.push_back (e);
            }
            const int changes = 1 + r.nextInt (2);
            for (int c = 0; c < changes && ! bar.empty(); ++c)
            {
                auto& e = bar[(size_t) r.nextInt ((int) bar.size())];
                e.pattern = favorites[(size_t) r.nextInt ((int) favorites.size())];
                e.pitch = r.nextBool() ? 2 : 0;
                e.depth = randomDepth (r, false);
                e.gate = randomGate (r);
            }
        }
        else
        {
            bar = genBar (k * stepsPerBar);
        }
        p.events.insert (p.events.end(), bar.begin(), bar.end());
    }

    // ここまでは16分音符単位で作ったので、ティックに直す
    for (auto& e : p.events)
    {
        e.start *= ticksPerStep;
        e.len *= ticksPerStep;
    }

    // 最後の小節は、締めのロールで盛り上げる (1小節のときは半々)
    if (p.bars > 1 || r.nextBool())
        addTurnaround (p.events, (p.bars - 1) * ticksPerBar);

    sortEvents (p.events);
    trimGhosts (p.events);
    return p;
}

ScratchProcessor::Phrase ScratchProcessor::makeVariation (const Phrase& source, juce::uint32 seed)
{
    juce::Random r ((juce::int64) seed);
    Phrase p = source;
    p.edited = true;
    bool changed = false;
    for (auto& e : p.events)
    {
        if (r.nextFloat() >= 0.4f)
            continue;
        changed = true;
        switch (r.nextInt (5))
        {
            case 0: e.pattern = notScribble ((e.pattern + 1 + r.nextInt (numPresetPatterns - 1)) % numPresetPatterns); break;
            case 1: // 速さ: 3連符は3連符の中で、ふつうの音符はふつうの音符の中で変える
                e.rate = e.rate >= 5 ? juce::jlimit (5, 8, e.rate + (r.nextBool() ? 1 : -1))
                                     : juce::jlimit (1, 3, e.rate + (r.nextBool() ? 1 : -1));
                break;
            case 2: e.pitch = juce::jlimit (-7, 7, e.pitch + (r.nextBool() ? 2 : -2)); break;
            case 3: e.depth = juce::jlimit (0.2f, 1.5f, e.depth * (0.6f + 0.8f * r.nextFloat())); break;
            default: e.gate = randomGate (r); break;
        }
    }
    if (! changed && ! p.events.empty()) // 必ず1つは変える
    {
        auto& e = p.events[(size_t) r.nextInt ((int) p.events.size())];
        e.pattern = notScribble ((e.pattern + 1 + r.nextInt (numPresetPatterns - 1)) % numPresetPatterns);
    }
    trimGhosts (p.events);
    return p;
}

//==============================================================================
void ScratchProcessor::publishPhrase()
{
    {
        // オーディオスレッドは、取り込めないときは前のまま鳴らすので、ここで直接書き換えてよい
        const juce::SpinLock::ScopedLockType lock (phraseLock);
        auto& rt = *pending;
        rt.bars = phrase.bars;
        rt.n = juce::jmin ((int) phrase.events.size(), maxEvents);
        for (int i = 0; i < rt.n; ++i)
        {
            rt.ev[(size_t) i] = phrase.events[(size_t) i];
            rt.natEnd[(size_t) i] = rt.ev[(size_t) i].tail < 1.0f ? naturalEnd (rt.ev[(size_t) i]) : 1.0f;
        }
        for (int pi = 0; pi < numPatterns && pi < (int) shapes.size(); ++pi)
        {
            rt.apex[(size_t) pi] = patternApex (pi);
            const bool use = isShapeEdited (pi);
            rt.useShape[(size_t) pi] = use;
            if (use)
            {
                rt.shapeTab[(size_t) pi] = shapes[(size_t) pi].table;
                rt.shapeGateTab[(size_t) pi] = shapes[(size_t) pi].gate;
                rt.shapeMaxSlope[(size_t) pi] = shapes[(size_t) pi].maxSlope;
            }
        }
    }
    phraseVersion.fetch_add (1);
    phraseChanged.fetch_add (1);
}

void ScratchProcessor::setPhrase (Phrase newPhrase, bool keepHistory)
{
    if (keepHistory)
    {
        history.push_back (phrase);
        if (history.size() > 30)
            history.erase (history.begin());
    }
    phrase = std::move (newPhrase);
    publishPhrase();
}

void ScratchProcessor::loadTemplate (int index)
{
    setPhrase (makeTemplate (index, phrase.bars));
}

void ScratchProcessor::randomize()
{
    randomizeWithSeed ((juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff));
}

void ScratchProcessor::randomizeWithSeed (juce::uint32 seed)
{
    auto p = makeRandom (seed, phrase.bars);
    p.templateIndex = phrase.templateIndex; // ◀▶ は、最後に選んだテンプレートから
    p.myName = phrase.myName;
    setPhrase (std::move (p));
}

void ScratchProcessor::stepTemplate (int delta)
{
    // 一覧と同じ並び: 空 → Sound Artz → テンプレート → マイパターン。読めないファイルは飛ばす
    const auto names = getMyPatternNames();
    const int firstTemplate = 1 + numFactoryPatterns;
    const int firstMine = firstTemplate + numTemplates;
    const int total = firstMine + names.size();
    const int mine = names.indexOf (phrase.myName);
    const int tmpl = phrase.templateIndex;
    int pos = mine >= 0 ? firstMine + mine : (tmpl >= emptyTemplate ? tmpl - emptyTemplate : firstTemplate + tmpl);
    for (int tries = 0; tries < total; ++tries)
    {
        pos = (pos + delta % total + total) % total;
        if (pos < firstMine)
        {
            loadTemplate (pos < firstTemplate ? emptyTemplate + pos : pos - firstTemplate);
            return;
        }
        if (loadMyPattern (names[pos - firstMine]))
            return;
    }
}

//==============================================================================
juce::String ScratchProcessor::cleanPatternName (const juce::String& name)
{
    return juce::File::createLegalFileName (name).substring (0, 60).trimCharactersAtStart (". ").trimCharactersAtEnd (". ");
}

juce::File ScratchProcessor::myPatternFile (const juce::String& name) const
{
    return myPatternDir.getChildFile (cleanPatternName (name) + ".cueburn");
}

juce::StringArray ScratchProcessor::getMyPatternNames() const
{
    juce::StringArray names;
    for (auto& f : myPatternDir.findChildFiles (juce::File::findFiles, false, "*.cueburn"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool ScratchProcessor::myPatternExists (const juce::String& name) const
{
    return cleanPatternName (name).isNotEmpty() && myPatternFile (name).existsAsFile();
}

bool ScratchProcessor::saveMyPattern (const juce::String& rawName)
{
    const auto name = cleanPatternName (rawName);
    if (name.isEmpty())
        return false;

    juce::ValueTree file ("SCRATCHPAT");
    file.setProperty ("ver", 1, nullptr);
    file.setProperty ("patver", 2, nullptr);
    file.appendChild (phraseToTree (phrase), nullptr);

    // ブロックが使っている波形を編集していたら、形も一緒に (別のプロジェクトでも同じ音になるように)
    std::array<bool, numPatterns> used {};
    for (auto& e : phrase.events)
        used[(size_t) juce::jlimit (0, numPatterns - 1, e.pattern)] = true;
    juce::ValueTree shs ("SHAPES");
    for (int pi = 0; pi < numPatterns; ++pi)
        if (used[(size_t) pi] && isShapeEdited (pi))
            shs.appendChild (shapeToTree (pi), nullptr);
    file.appendChild (shs, nullptr);

    const auto xml = file.createXml();
    if (xml == nullptr || ! myPatternDir.createDirectory().wasOk() || ! xml->writeTo (myPatternFile (name)))
        return false;

    phrase.myName = name;
    phrase.edited = false;
    phrase.isRandom = false;
    publishPhrase();
    return true;
}

bool ScratchProcessor::loadMyPattern (const juce::String& name)
{
    const auto xml = juce::parseXML (myPatternFile (name));
    if (xml == nullptr || ! xml->hasTagName ("SCRATCHPAT"))
        return false;
    const auto file = juce::ValueTree::fromXml (*xml);
    const auto ph = file.getChildWithName ("PHRASE");
    if (! ph.isValid())
        return false;

    auto p = phraseFromTree (ph);
    p.templateIndex = phrase.templateIndex;
    p.isRandom = false;
    p.edited = false;
    p.myName = cleanPatternName (name);

    const auto shs = file.getChildWithName ("SHAPES");
    if (shs.getNumChildren() > 0)
        beginUserEdit(); // 波形エディタの「戻る」で、呼び出す前の形に戻せる
    for (int i = 0; i < shs.getNumChildren(); ++i)
    {
        const auto sh = shs.getChild (i);
        const int pi = (int) sh.getProperty ("p", -1);
        if (validPattern (pi))
            readShape (sh, pi);
    }
    setPhrase (std::move (p));
    return true;
}

bool ScratchProcessor::deleteMyPattern (const juce::String& name)
{
    if (! myPatternExists (name) || ! myPatternFile (name).moveToTrash())
        return false;
    if (phrase.myName == cleanPatternName (name))
    {
        // 鳴っているブロックは、そのまま残す
        phrase.myName.clear();
        phrase.edited = true;
        publishPhrase();
    }
    return true;
}

void ScratchProcessor::vary()
{
    setPhrase (makeVariation (phrase, (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff)));
}

bool ScratchProcessor::back()
{
    if (history.empty())
        return false;
    phrase = history.back();
    history.pop_back();
    publishPhrase();
    return true;
}

void ScratchProcessor::setBars (int bars)
{
    bars = juce::jlimit (1, 4, bars);
    if (bars == phrase.bars)
        return;

    if (! phrase.edited && (phrase.isRandom || phrase.myName.isEmpty()))
    {
        // テンプレート・ランダムから作ったままなら、その長さで作り直す (締めのロールも付く)
        auto p = phrase.isRandom ? makeRandom (phrase.seed, bars) : makeTemplate (phrase.templateIndex, bars);
        p.templateIndex = phrase.templateIndex;
        p.myName = phrase.myName;
        setPhrase (std::move (p));
        return;
    }

    // 手を加えたフレーズは、くり返し/切り詰めで長さを変える
    Phrase p = phrase;
    p.edited = true;
    const int oldSteps = phrase.bars * ticksPerBar;
    const int newSteps = bars * ticksPerBar;
    p.bars = bars;
    p.events.clear();
    for (int offset = 0; offset < newSteps; offset += oldSteps)
        for (auto e : phrase.events)
        {
            e.start += offset;
            if (e.start >= newSteps)
                continue;
            e.len = juce::jmin (e.len, newSteps - e.start);
            clearMuteBeyond (e);
            p.events.push_back (e);
        }
    setPhrase (std::move (p));
}

void ScratchProcessor::cycleEventPattern (int eventIndex, int delta)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    auto& e = p.events[(size_t) eventIndex];
    e.pattern = ((e.pattern + delta) % numPatterns + numPatterns) % numPatterns;
    setPhrase (std::move (p));
}

// ブロックのドラッグ編集: 始めるときに1回だけ履歴に積み、ドラッグ中は、その場で書き換える
void ScratchProcessor::beginEdit()
{
    history.push_back (phrase);
    if (history.size() > 30)
        history.erase (history.begin());
}

void ScratchProcessor::setEventDepth (int eventIndex, float depth)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    phrase.events[(size_t) eventIndex].depth = juce::jlimit (0.2f, 1.5f, depth);
    phrase.edited = true;
    publishPhrase();
}

int ScratchProcessor::autoRate (int lenTicks)
{
    // 1往復が、ブロックに収まる速さ。ただし、ゆっくりすぎないように 1/8 まで
    // (1/8 = 12ティック、1/8T = 8、1/16 = 6、1/16T = 4、1/32 = 3、1/32T = 2)
    static const std::pair<int, int> table[] = { { 12, 2 }, { 8, 6 }, { 6, 3 }, { 4, 7 }, { 3, 4 }, { 2, 8 } };
    for (auto& t : table)
        if (lenTicks >= t.first)
            return t.second;
    return 8;
}

int ScratchProcessor::addEvent (int start, int len, int pattern, int rate)
{
    const int steps = phrase.bars * ticksPerBar;
    if (start < 0 || start >= steps)
        return -1;
    len = juce::jlimit (1, steps - start, len);
    const int end = start + len;

    Phrase p = phrase;
    p.edited = true;

    // 重なるブロックは、重なった部分を詰める (前にはみ出す部分だけ残す / 後ろにはみ出す部分だけ残す)
    std::vector<FillEvent> kept;
    for (auto e : p.events)
    {
        const int eEnd = e.start + e.len;
        if (eEnd <= start || e.start >= end)
            kept.push_back (e);
        else if (e.start < start)
        {
            e.len = start - e.start;
            clearMuteBeyond (e);
            kept.push_back (e);
        }
        else if (eEnd > end)
        {
            e.mute >>= (size_t) (end - e.start); // ミュートは、元の位置のまま
            e.len = eEnd - end;
            e.start = end;
            clearMuteBeyond (e);
            kept.push_back (e);
        }
    }
    if ((int) kept.size() >= maxEvents)
        return -1;

    FillEvent n;
    n.start = start;
    n.len = len;
    n.pattern = juce::jlimit (0, numPatterns - 1, pattern);
    n.rate = juce::jlimit (0, numRates - 1, rate);
    kept.push_back (n);
    std::sort (kept.begin(), kept.end(), [] (const FillEvent& a, const FillEvent& b) { return a.start < b.start; });
    p.events = std::move (kept);

    int index = -1;
    for (int i = 0; i < (int) p.events.size(); ++i)
        if (p.events[(size_t) i].start == start)
            index = i;
    setPhrase (std::move (p));
    return index;
}

void ScratchProcessor::removeEvent (int eventIndex)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    p.events.erase (p.events.begin() + eventIndex);
    setPhrase (std::move (p));
}

void ScratchProcessor::setEventPattern (int eventIndex, int pattern)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    p.events[(size_t) eventIndex].pattern = juce::jlimit (0, numPatterns - 1, pattern);
    setPhrase (std::move (p));
}

void ScratchProcessor::setEventRate (int eventIndex, int rate)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    p.events[(size_t) eventIndex].rate = juce::jlimit (0, numRates - 1, rate);
    setPhrase (std::move (p));
}

void ScratchProcessor::setEventPitch (int eventIndex, int pitch)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    p.events[(size_t) eventIndex].pitch = juce::jlimit (-12, 12, pitch);
    setPhrase (std::move (p));
}

void ScratchProcessor::resetEventShape (int eventIndex)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    p.events[(size_t) eventIndex].depth = 1.0f;
    p.events[(size_t) eventIndex].gate = 1.0f;
    setPhrase (std::move (p));
}

void ScratchProcessor::setEventEnd (int eventIndex, float audibleTicks)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    auto& e = phrase.events[(size_t) eventIndex];
    // 伸ばせるのは、次のブロック(またはフレーズの終わり)まで。一番短くて 1.5ティック (32分音符の半分)
    const int limit = (eventIndex + 1 < (int) phrase.events.size() ? phrase.events[(size_t) eventIndex + 1].start
                                                                   : phrase.bars * ticksPerBar) - e.start;
    const float ticks = juce::jlimit (1.5f, (float) juce::jmax (2, limit), audibleTicks);
    e.len = juce::jlimit (1, juce::jmax (1, limit), (int) std::ceil (ticks - 1.0e-3f));
    e.gate = juce::jlimit (0.1f, 1.0f, ticks / (float) e.len);
    clearMuteBeyond (e);
    phrase.edited = true;
    publishPhrase();
}

void ScratchProcessor::setEventStart (int eventIndex, int newStart)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    auto& e = phrase.events[(size_t) eventIndex];
    const float audEnd = (float) e.start + (float) e.len * e.gate; // 鳴り終わりの位置 (動かさない)
    const int prevEnd = eventIndex > 0 ? phrase.events[(size_t) eventIndex - 1].start + phrase.events[(size_t) eventIndex - 1].len : 0;
    const int maxStart = (int) std::floor (audEnd - 1.5f);
    const int s = juce::jlimit (prevEnd, juce::jmax (prevEnd, maxStart), newStart);
    if (s == e.start)
        return;
    const float audible = audEnd - (float) s;
    // ミュートは、タイムライン上の元の位置のまま
    if (s < e.start)
        e.mute <<= (size_t) (e.start - s);
    else
        e.mute >>= (size_t) (s - e.start);
    e.start = s;
    e.len = juce::jmax (1, (int) std::ceil (audible - 1.0e-3f));
    e.gate = juce::jlimit (0.1f, 1.0f, audible / (float) e.len);
    clearMuteBeyond (e);
    phrase.edited = true;
    publishPhrase();
}

bool ScratchProcessor::canPlace (int eventIndex, int start) const
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return false;
    const int len = phrase.events[(size_t) eventIndex].len;
    if (start < 0 || start + len > phrase.bars * ticksPerBar)
        return false;
    for (int i = 0; i < (int) phrase.events.size(); ++i)
    {
        const auto& o = phrase.events[(size_t) i];
        if (i != eventIndex && start < o.start + o.len && o.start < start + len)
            return false;
    }
    return true;
}

int ScratchProcessor::moveEvent (int eventIndex, int start)
{
    if (! canPlace (eventIndex, start))
        return eventIndex;
    if (phrase.events[(size_t) eventIndex].start == start)
        return eventIndex;
    phrase.events[(size_t) eventIndex].start = start;
    std::sort (phrase.events.begin(), phrase.events.end(), [] (const FillEvent& a, const FillEvent& b) { return a.start < b.start; });
    int index = eventIndex;
    for (int i = 0; i < (int) phrase.events.size(); ++i)
        if (phrase.events[(size_t) i].start == start)
            index = i;
    phrase.edited = true;
    publishPhrase();
    return index;
}

void ScratchProcessor::setEventMuteMask (int eventIndex, const std::bitset<maxEventTicks>& mask)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    auto& e = phrase.events[(size_t) eventIndex];
    e.mute = mask;
    clearMuteBeyond (e);
    phrase.edited = true;
    publishPhrase();
}

void ScratchProcessor::clearEventMutes (int eventIndex)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    p.events[(size_t) eventIndex].mute.reset();
    setPhrase (std::move (p));
}

void ScratchProcessor::setEventBend (int eventIndex, float rise, float fall)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    auto& e = phrase.events[(size_t) eventIndex];
    e.bendRise = juce::jlimit (-1.0f, 1.0f, rise);
    e.bendFall = juce::jlimit (-1.0f, 1.0f, fall);
    phrase.edited = true;
    publishPhrase();
}

void ScratchProcessor::setEventTail (int eventIndex, float tail)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    phrase.events[(size_t) eventIndex].tail = juce::jlimit (0.03f, 1.0f, tail);
    phrase.edited = true;
    publishPhrase();
}

void ScratchProcessor::setEventApex (int eventIndex, float at)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    phrase.events[(size_t) eventIndex].apexAt = at < 0.0f ? -1.0f : juce::jlimit (0.05f, 0.95f, at);
    phrase.edited = true;
    publishPhrase();
}

void ScratchProcessor::resetEventCurve (int eventIndex)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    Phrase p = phrase;
    p.edited = true;
    auto& e = p.events[(size_t) eventIndex];
    e.bendRise = e.bendFall = 0.0f;
    e.apexAt = -1.0f;
    e.tail = 1.0f;
    setPhrase (std::move (p));
}

void ScratchProcessor::setEventGate (int eventIndex, float gate)
{
    if (eventIndex < 0 || eventIndex >= (int) phrase.events.size())
        return;
    phrase.events[(size_t) eventIndex].gate = juce::jlimit (0.1f, 1.0f, gate);
    phrase.edited = true;
    publishPhrase();
}
