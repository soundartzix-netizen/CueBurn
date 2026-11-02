#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetSamples.h"
#include "BinaryData.h"

namespace
{
constexpr double kTwoPi = 6.283185307179586;

// 1往復の長さ(拍)。後ろの4つは3連符
constexpr double kRateBeats[] = { 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };

// ミュート(ティックの集まり)を、保存用の文字列 "開始-終わり,…" にする
juce::String muteToString (const std::bitset<ScratchProcessor::maxEventTicks>& m, int len)
{
    juce::StringArray parts;
    int i = 0;
    while (i < len)
    {
        if (! m[(size_t) i])
        {
            ++i;
            continue;
        }
        int j = i;
        while (j < len && m[(size_t) j])
            ++j;
        parts.add (juce::String (i) + "-" + juce::String (j));
        i = j;
    }
    return parts.joinIntoString (",");
}

void muteFromString (const juce::String& s, std::bitset<ScratchProcessor::maxEventTicks>& m, int len, int scale)
{
    for (auto& part : juce::StringArray::fromTokens (s, ",", ""))
    {
        const int a = juce::jmax (0, part.upToFirstOccurrenceOf ("-", false, false).getIntValue() * scale);
        const int b = juce::jmin (len, part.fromFirstOccurrenceOf ("-", false, false).getIntValue() * scale);
        for (int i = a; i < b; ++i)
            m.set ((size_t) i);
    }
}

juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String dbText (float v, int) { return juce::String (v, 1) + " dB"; }
juce::String msText (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
juce::String hzText (float v, int)
{
    if (v >= 19900.0f)
        return "Off";
    return v < 1000.0f ? juce::String (v, 0) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz";
}

inline float smooth01 (float x) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    return x * x * (3.0f - 2.0f * x);
}

// 人の手の自然な動き (最小躍度): 動き出しと止まりがなめらかで、途中は速い
inline float minJerk (float s) noexcept
{
    s = juce::jlimit (0.0f, 1.0f, s);
    return s * s * s * (10.0f + s * (-15.0f + 6.0f * s));
}

// ベイビー系: 押す・引くを、別々の1回の動きにする (合間はレコードが止まる = 音が途切れる)。
// 耳での聴き比べで、往復をくり返すより「キレとグルーヴが出る」と判断された形 (2026-10)
constexpr float kPushEnd = 0.225f;   // 押し: 前半の 45% で速く
constexpr float kPullStart = 0.5f;   // 引き: 後半の頭から
constexpr float kPullEnd = 0.9f;     //       後半の 80% かけて、ゆっくり
inline float babyStroke (float phi) noexcept
{
    if (phi < kPushEnd)
        return minJerk (phi / kPushEnd);
    if (phi < kPullStart)
        return 1.0f;
    if (phi < kPullEnd)
        return 1.0f - minJerk ((phi - kPullStart) / (kPullEnd - kPullStart));
    return 0.0f;
}

// フォワード系: 速く押して (聞こえる)、一瞬止まって、ゆっくり戻す (フェーダーは閉じる)
constexpr float kFwdPushEnd = 0.42f, kFwdReturn = 0.43f;
inline float forwardStroke (float phi) noexcept
{
    if (phi < kFwdPushEnd)
        return minJerk (phi / kFwdPushEnd);
    if (phi < kFwdReturn)
        return 1.0f;
    return 1.0f - minJerk ((phi - kFwdReturn) / (1.0f - kFwdReturn));
}

// 4点のエルミート補間
inline float hermite (float ym1, float y0, float y1, float y2, float t) noexcept
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

// 決まった乱数 (0〜1)。同じ入力なら、いつも同じ値
inline float hash01 (unsigned a, unsigned b) noexcept
{
    unsigned x = a * 374761393u + b * 668265263u + 0x9e3779b9u;
    x = (x ^ (x >> 13)) * 1274126177u;
    x ^= x >> 16;
    return (float) (x & 0xffffff) / 16777216.0f;
}

// 不連続点のギザギザ(折り返しノイズ)を減らす補正。t は周期内の位置、dt は1サンプルで進む周期
inline double polyBlep (double t, double dt) noexcept
{
    if (dt <= 0.0)
        return 0.0;
    if (t < dt)
    {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}
}

//==============================================================================
double ScratchProcessor::rateBeats (int rate) noexcept
{
    return kRateBeats[juce::jlimit (0, numRates - 1, rate)];
}

float ScratchProcessor::patternD (int pattern, float phi) noexcept
{
    if (pattern < 0 || pattern >= numPresetPatterns)
        pattern = 0; // ユーザー波形は shapeD で
    phi -= std::floor (phi);
    const float twoPi = (float) kTwoPi;
    switch (pattern)
    {
        case 0: // Baby: 押して、止まって、引く (どちらも聞こえる)
        case 4: // Flare: 押す途中を、開け閉めで刻む
        case 8: // Orbit: 押しと引きで、それぞれ1回ずつクリック
            return babyStroke (phi);
        case 1: // Forward: 速く押して(聞こえる)、ゆっくり戻す(ミュート)
        case 3: // Chirp: 押す途中の速いところだけ
        case 7: // Crab: 押しながら、4連打で刻む
            return forwardStroke (phi);
        case 2: // Tear: 押し出して、引き戻しを2段階に分ける
            if (phi < 0.40f)
                return smooth01 (phi / 0.40f);
            if (phi < 0.56f)
                return 1.0f - 0.55f * smooth01 ((phi - 0.40f) / 0.16f);
            if (phi < 0.66f)
                return 0.45f - 0.02f * (phi - 0.56f) / 0.10f;
            if (phi < 0.84f)
                return 0.43f * (1.0f - smooth01 ((phi - 0.66f) / 0.18f));
            return 0.0f;
        case 6: // Transformer: なめらかな往復を、開け閉めで細かく刻む (刻みが主役なので、動きは止めない)
            return 0.5f - 0.5f * std::cos (twoPi * phi);
        case 9: // Stab: 一瞬だけ鋭く突いて、ゆっくり戻す
            return phi < 0.15f ? smooth01 (phi / 0.15f) : 1.0f - smooth01 ((phi - 0.15f) / 0.85f);
        case 10: // Drag: ゆっくり浅く (低く唸る)
            return 0.6f * (0.5f - 0.5f * std::cos (twoPi * phi));
        case 12: // Brake: 勢いよく押し出して、ブレーキをかけるように遅くなる (音は高い所から下がるだけ。低く残る)
        case 13: // Zap: 同じく下がるだけの、短いもの
        {
            // 速さ v = a + (1-a)·exp(-φ/τ) を積分した位置。鳴らす区間の終わり(end)で 1 になるように割る
            // (曲の分析から: 音の高さは、ほぼ一定の割合で下がり続ける = 速さが指数的に落ちる。1往復で約2.3オクターブ)
            const bool brake = pattern == 12;
            const float a = 0.0f, tau = brake ? 0.55f : 0.6f, end = brake ? 0.8f : 0.48f;
            auto integral = [a, tau] (float x) { return a * x + (1.0f - a) * tau * (1.0f - std::exp (-x / tau)); };
            if (phi < end)
                return integral (phi) / integral (end);
            // 戻り (フェーダーは閉じている)。次の押し出しの前に戻り終わるように、少し早めに頭出し位置へ
            const float retEnd = brake ? 0.95f : 0.97f;
            return phi >= retEnd ? 0.0f : 1.0f - smooth01 ((phi - end) / (retEnd - end));
        }
        case 11: // Hydroplane: 指で擦ってビリつかせる (細かい揺れを重ねる)
            return juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * std::cos (twoPi * phi)
                                                 + 0.035f * std::sin (twoPi * 24.0f * phi) * std::sin (0.5f * twoPi * phi));
        default: // Scribble: 小刻みに素早く揺らす
            return 0.3f * (0.5f - 0.5f * std::cos (twoPi * 6.0f * phi));
    }
}

bool ScratchProcessor::patternGate (int pattern, float phi) noexcept
{
    if (pattern < 0 || pattern >= numPresetPatterns)
        pattern = 0;
    phi -= std::floor (phi);
    switch (pattern)
    {
        case 1: // Forward: 押すときだけ聞こえる
            return phi < kFwdReturn;
        case 3: // Chirp: 押す途中の、速いところだけ (押しの 25%〜85%)
            return phi >= 0.25f * kFwdPushEnd && phi < 0.85f * kFwdPushEnd;
        case 4: // Flare: 押す途中を、開け閉めで刻む
        {
            if (phi >= kPushEnd)
                return false;
            const float f = phi / kPushEnd;
            return f < 0.2f || (f >= 0.36f && f < 0.6f) || f >= 0.76f;
        }
        case 6: // Transformer: 1往復を8つに刻む
        {
            const float f = phi * 8.0f;
            return f - std::floor (f) < 0.5f;
        }
        case 7: // Crab: 押す間に4回クリック
        {
            if (phi >= kFwdPushEnd)
                return false;
            const float f = phi / kFwdPushEnd * 4.0f;
            return f - std::floor (f) < 0.55f;
        }
        case 8: // Orbit: 押しと引きの、それぞれ真ん中だけ開ける
        {
            const float pullLen = kPullEnd - kPullStart;
            return (phi >= 0.16f * kPushEnd && phi < 0.84f * kPushEnd)
                   || (phi >= kPullStart + 0.16f * pullLen && phi < kPullStart + 0.84f * pullLen);
        }
        case 9: // Stab: 突いた瞬間だけ
            return phi < 0.14f;
        // ブレーキ・ザップは、レコードが速さに乗ってから開ける (出だしで音が下から駆け上がらないように)
        case 12: // Brake: 押し出す間ずっと (素早い戻りは閉じる)
            return phi >= 0.03f && phi < 0.77f;
        case 13: // Zap: 下がる所だけ
            return phi >= 0.06f && phi < 0.46f;
        default:
            return true;
    }
}

float ScratchProcessor::shapeValue (int source, double cycles) noexcept
{
    const double t = cycles - std::floor (cycles);
    switch (source)
    {
        case srcSine: return (float) std::sin (kTwoPi * t);
        case srcSaw: return (float) (2.0 * t - 1.0);
        case srcSquare: return t < 0.5 ? 1.0f : -1.0f;
        default: return 0.0f;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ScratchProcessor::createLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;

    auto skewed = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "src", 1 }, "Source",
                                                   StringArray { "Sine", "Saw", "Square", "Noise", "Sample" }, 4)); // 初めはサンプル (プリセットの声)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Fill", "Single" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pattern", 1 }, "Pattern",
                                                   StringArray { "Baby", "Forward", "Tear", "Chirp", "Flare", "Scribble",
                                                                 "Transformer", "Crab", "Orbit", "Stab", "Drag", "Hydroplane", "Brake", "Zap",
                                                                 "User 1", "User 2", "User 3", "User 4" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Rate",
                                                   StringArray { "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T" }, 2));
    l.add (std::make_unique<P> (ParameterID { "depth", 1 }, "Depth", skewed (20.0f, 1000.0f, 200.0f), 250.0f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (msText)));
    l.add (std::make_unique<P> (ParameterID { "feel", 1 }, "Feel", skewed (1.0f, 40.0f, 10.0f), 3.0f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (msText)));
    l.add (std::make_unique<P> (ParameterID { "cut", 1 }, "Cut", NormalisableRange<float> (0.0f, 0.9f), 0.5f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    l.add (std::make_unique<P> (ParameterID { "human", 1 }, "Human", NormalisableRange<float> (0.0f, 1.0f), 0.3f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    l.add (std::make_unique<P> (ParameterID { "start", 1 }, "Start", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    l.add (std::make_unique<P> (ParameterID { "tone", 1 }, "Tone", skewed (500.0f, 20000.0f, 5000.0f), 20000.0f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    l.add (std::make_unique<P> (ParameterID { "vinyl", 1 }, "Vinyl", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    l.add (std::make_unique<P> (ParameterID { "level", 1 }, "Level", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), -6.0f,
                                AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    return l;
}

ScratchProcessor::ScratchProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      pending (std::make_unique<PhraseRT>()), live (std::make_unique<PhraseRT>()),
      myPatternDir (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Sound Artz/Cue Burn/MyPatterns"))
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pSrc = get ("src");
    pMode = get ("mode");
    pPattern = get ("pattern");
    pRate = get ("rate");
    pDepth = get ("depth");
    pFeel = get ("feel");
    pHuman = get ("human");
    pCut = get ("cut");
    pStart = get ("start");
    pTone = get ("tone");
    pLevel = get ("level");
    pVinyl = get ("vinyl");

    formats.registerBasicFormats();
    for (auto& w : uiWave)
        w.store (0.0f);
    heldStack.reserve (128);

    // 波形: 決まった形のパターンは数式のまま。ユーザー波形の初期値は ベイビー / フォワード / チャープ / ティア を写したもの
    shapes.resize ((size_t) numPatterns);
    static const int defaults[numUserPatterns] = { 0, 1, 3, 2 };
    for (int s = 0; s < numUserPatterns; ++s)
        copyPresetToUser (s, defaults[s]);
    shapeHistory.clear();

    phrase = makeTemplate (emptyTemplate, 1); // 起動時は、何も置いていない状態から
    publishPhrase();
    loadPresetSample (0); // 初めの音源: プリセットの声 "Ha"
}

bool ScratchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet().isDisabled();
}

void ScratchProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;

    // ノイズの音源: 2秒ぶんの固定のノイズ (同じ位置は、いつも同じ音。後ろに動かすと、逆再生になる)
    // ホワイトノイズのままだと、速く回しても音が変わらない (もともと全部の高さが入っている) ので、
    // 1kHz あたりに中心のある「色の付いたノイズ」にする。回す速さで中心が上下して、こすった感じが出る
    {
        const size_t n = (size_t) (2.0 * sampleRate);
        noiseTable.assign (n, 0.0f);
        std::vector<float> white (n);
        juce::Random rng (12345);
        for (auto& v : white)
            v = rng.nextFloat() * 2.0f - 1.0f;

        // バンドパス (RBJ、中心 1kHz、Q 1.1) + 少しの高域 (1次のローパス 3.5kHz)
        const double w0 = kTwoPi * 1000.0 / sampleRate, alpha = std::sin (w0) / (2.0 * 1.1);
        const double a0 = 1.0 + alpha;
        const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos (w0) / a0, a2 = (1.0 - alpha) / a0;
        const double lpC = 1.0 - std::exp (-kTwoPi * 3500.0 / sampleRate);
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, lp = 0;
        for (int pass = 0; pass < 2; ++pass) // 2周目の値を使う (つなぎ目でも、フィルターが落ち着いている)
            for (size_t i = 0; i < n; ++i)
            {
                const double x = white[i];
                const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                lp += lpC * (x - lp);
                noiseTable[i] = (float) (y + 0.3 * lp);
            }
        double sum = 0.0;
        for (auto v : noiseTable)
            sum += (double) v * v;
        const float gain = (float) (0.58 / std::sqrt (sum / (double) n + 1.0e-12)); // 前のホワイトノイズと同じくらいの大きさ
        for (auto& v : noiseTable)
            v = std::tanh (v * gain); // 山を少しだけ丸めて、±1 に収める
    }

    pos = pos1 = target = 0.0;
    phase = 0.0;
    gateGain = gateTarget = 1.0f;
    speedEnv = 0.0f;
    env = 0.0f;
    std::memset (toneState, 0, sizeof (toneState));
    usingMouse = false;
    angle = 0.0;
    angleSm = platterAngle.load();
    heldStack.clear();
    noteActive = false;
    pendingTrigger = false;
    previewWasOn = false;
    evGainSm = 1.0f;
    vinylRng.setSeed (4242); // 決まった乱数 (書き出しのたびに同じ音)
    hissHp = hissLp = crackleHp = crackleX1 = 0.0f;
    for (auto& s : vinylLp)
        s[0] = s[1] = 0.0f;
    beatsSinceNote = 0.0;
    lastEvent = -1;
    lastCue = 0.0f;
    liveVersion = -1;
    peakOut = 0.0f;
}

//==============================================================================
bool ScratchProcessor::canLoad (const juce::String& fileName) const
{
    for (auto* f : formats)
        for (auto& ext : f->getFileExtensions())
            if (fileName.endsWithIgnoreCase (ext))
                return true;
    return false;
}

bool ScratchProcessor::loadSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSampleSeconds * reader->sampleRate));
    juce::AudioBuffer<float> data (2, len);
    data.clear();
    if (reader->numChannels >= 2)
        reader->read (&data, 0, len, 0, true, true);
    else
    {
        reader->read (&data, 0, len, 0, true, false);
        data.copyFrom (1, 0, data, 0, 0, len); // モノラルは左右に同じ音を
    }
    setSample (std::move (data), reader->sampleRate, file.getFileName(), file.getFullPathName());
    return true;
}

int ScratchProcessor::getNumPresetSamples() noexcept
{
    return kNumPresetSamples;
}

bool ScratchProcessor::loadPresetSample (int index)
{
    if (index < 0 || index >= kNumPresetSamples)
        return false;
    const auto& info = kPresetSamples[index];
    int size = 0;
    const char* bytes = BinaryData::getNamedResource (info.resource, size);
    if (bytes == nullptr || size <= 0)
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, (size_t) size, false)));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;
    const int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> data (2, len);
    data.clear();
    reader->read (&data, 0, len, 0, true, false);
    data.copyFrom (1, 0, data, 0, 0, len); // モノラルを左右に
    setSample (std::move (data), reader->sampleRate, info.en, juce::String ("preset:") + info.resource);
    return true;
}

int ScratchProcessor::getPresetIndex() const
{
    if (! samplePath.startsWith ("preset:"))
        return -1;
    const auto res = samplePath.fromFirstOccurrenceOf ("preset:", false, false);
    for (int i = 0; i < kNumPresetSamples; ++i)
        if (res == kPresetSamples[i].resource)
            return i;
    return -1;
}

void ScratchProcessor::setSample (juce::AudioBuffer<float>&& data, double rate, const juce::String& name, const juce::String& path)
{
    auto fresh = std::make_unique<SampleData>();
    fresh->data = std::move (data);
    fresh->rate = rate;

    // 波形の表示用に、ピークをまとめておく
    const int n = fresh->data.getNumSamples();
    float overall = 1.0e-6f;
    std::array<float, numWaveBins> bins {};
    for (int b = 0; b < numWaveBins; ++b)
    {
        const int s = (int) ((long long) b * n / numWaveBins);
        const int e = juce::jmax (s + 1, (int) ((long long) (b + 1) * n / numWaveBins));
        float pk = 0.0f;
        for (int ch = 0; ch < fresh->data.getNumChannels(); ++ch)
            pk = juce::jmax (pk, fresh->data.getMagnitude (ch, s, juce::jmin (e, n) - s));
        bins[(size_t) b] = pk;
        overall = juce::jmax (overall, pk);
    }
    for (int b = 0; b < numWaveBins; ++b)
        uiWave[(size_t) b].store (bins[(size_t) b] / overall);

    // 拡大しても形が読めるように、細かい塊でも持っておく
    auto overview = std::make_shared<WaveOverview>();
    overview->numSamples = n;
    overview->blockSize = juce::jmax (4, (n + maxOverviewBlocks - 1) / maxOverviewBlocks);
    overview->peak = overall;
    const int numBlocks = (n + overview->blockSize - 1) / overview->blockSize;
    overview->lo.assign ((size_t) numBlocks, 0.0f);
    overview->hi.assign ((size_t) numBlocks, 0.0f);
    overview->power.assign ((size_t) numBlocks, 0.0f);
    const int numCh = fresh->data.getNumChannels();
    for (int b = 0; b < numBlocks; ++b)
    {
        const int s = b * overview->blockSize;
        const int e = juce::jmin (n, s + overview->blockSize);
        float lo = 0.0f, hi = 0.0f, sum = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* src = fresh->data.getReadPointer (ch);
            for (int i = s; i < e; ++i)
            {
                lo = juce::jmin (lo, src[i]);
                hi = juce::jmax (hi, src[i]);
                sum += src[i] * src[i];
            }
        }
        overview->lo[(size_t) b] = lo;
        overview->hi[(size_t) b] = hi;
        overview->power[(size_t) b] = sum / (float) juce::jmax (1, (e - s) * numCh);
    }

    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        std::swap (sample, fresh); // 古いサンプルは、ロックの外で解放する
    }
    {
        const juce::SpinLock::ScopedLockType lock (waveLock);
        waveOverview = std::move (overview);
    }
    sampleName = name;
    samplePath = path;
    uiSampleSeconds.store (n > 0 ? (double) n / rate : 0.0);
}

std::shared_ptr<const ScratchProcessor::WaveOverview> ScratchProcessor::getWaveOverview() const
{
    const juce::SpinLock::ScopedLockType lock (waveLock);
    return waveOverview;
}

ScratchProcessor::WaveOverview::Column ScratchProcessor::WaveOverview::column (double from, double to) const noexcept
{
    Column c;
    const int numBlocks = (int) lo.size();
    if (numBlocks == 0 || peak <= 0.0f)
        return c;

    const int first = juce::jlimit (0, numBlocks - 1, (int) (from * numSamples / blockSize));
    const int last = juce::jlimit (first, numBlocks - 1, (int) std::ceil (to * numSamples / blockSize) - 1);
    float sum = 0.0f;
    for (int b = first; b <= last; ++b)
    {
        c.lo = juce::jmin (c.lo, lo[(size_t) b]);
        c.hi = juce::jmax (c.hi, hi[(size_t) b]);
        sum += power[(size_t) b];
    }
    c.lo /= peak;
    c.hi /= peak;
    c.rms = std::sqrt (sum / (float) (last - first + 1)) / peak;
    return c;
}

//==============================================================================
void ScratchProcessor::noteOn (int note, float velocity)
{
    heldStack.erase (std::remove (heldStack.begin(), heldStack.end(), note), heldStack.end());
    heldStack.push_back (note);
    heldVel[(size_t) note] = velocity;
    currentNote = note;
    velocityAmp = velocity;
    noteActive = true;
    pendingTrigger = true; // 頭出し位置への移動は、音源の長さがわかる処理の中で行う
}

void ScratchProcessor::noteOff (int note)
{
    heldStack.erase (std::remove (heldStack.begin(), heldStack.end(), note), heldStack.end());
    if (note != currentNote || ! noteActive)
        return;
    if (! heldStack.empty())
    {
        // まだ押しているノートに戻る (動きは続けたまま、音程だけ変わる)
        currentNote = heldStack.back();
        velocityAmp = heldVel[(size_t) currentNote];
    }
    else
    {
        noteActive = false;
    }
}

void ScratchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    if (numCh == 0 || noiseTable.empty())
        return;

    // ホストのテンポ (パターンの速さに使う)
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto p = ph->getPosition())
            if (auto b = p->getBpm())
                bpm = *b;
    if (bpm < 1.0)
        bpm = 120.0;

    // フレーズ: 変わっていたら、オーディオスレッド用のコピーを取り込む (取り込めないブロックは前のまま)
    if (phraseVersion.load() != liveVersion)
    {
        const juce::SpinLock::ScopedTryLockType pl (phraseLock);
        if (pl.isLocked())
        {
            *live = *pending;
            liveVersion = phraseVersion.load();
            lastEvent = -1;
        }
    }

    // 試聴: ノートを置かなくても、C5 を押しているのと同じように鳴らす
    {
        const bool prev = previewOn.load();
        if (prev && ! previewWasOn)
            noteOn (rootNote, 0.85f);
        else if (! prev && previewWasOn)
            noteOff (rootNote);
        previewWasOn = prev;
    }

    // パラメーター
    const int src = juce::jlimit (0, numSources - 1, (int) pSrc->load());
    const double beatInc = bpm / 60.0 / fs;
    const double phraseBeats = (double) live->bars * 4.0;
    // 波形エディタの試聴: 選んだパターンを、単発で鳴らす
    const int auditionPattern = previewOn.load() ? previewPattern.load() : -1;
    const bool fillMode = (int) pMode->load() == 0 && auditionPattern < 0;
    const int pattern = juce::jlimit (0, numPatterns - 1, auditionPattern >= 0 ? auditionPattern : (int) pPattern->load());
    const double rb1 = rateBeats ((int) pRate->load());
    const double phaseInc = bpm / 60.0 / fs / rb1;
    const double depthSec = 0.001 * (double) pDepth->load();
    const float human = pHuman->load();
    const float cut = pCut->load(); // 遅い所をフェーダーで切る量
    const float gateRelCoef = (float) (1.0 - std::exp (-1.0 / (0.006 * fs))); // ブロックの終わりで切るときの、リリース: 6ms
    const float evGainCoef = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));
    const float posCoef = (float) (1.0 - std::exp (-1.0 / (0.001 * (double) pFeel->load() * fs)));
    const float gateCoef = (float) (1.0 - std::exp (-1.0 / (0.0015 * fs)));
    const float envCoefSpeed = (float) (1.0 - std::exp (-1.0 / (0.010 * fs)));
    const float atkCoef = (float) (1.0 - std::exp (-1.0 / (0.002 * fs)));
    const float relCoef = (float) (1.0 - std::exp (-1.0 / (0.012 * fs)));
    const float tone = pTone->load();
    const bool useTone = tone < 19900.0f;
    const float toneCoef = useTone ? (float) (1.0 - std::exp (-kTwoPi * (double) tone / fs)) : 1.0f;
    const float levelGain = juce::Decibels::decibelsToGain (pLevel->load());
    // レコード感: 針の擦れ(こする速さに比例)、ほこりのパチパチ(動いている時だけ)、高域のなまり、ほんの少しの歪み
    const float vinyl = pVinyl->load();
    const bool useVinyl = vinyl > 0.001f;
    const float vinylLpC = (float) (1.0 - std::exp (-kTwoPi * (20000.0 - 18000.0 * (double) vinyl) / fs));
    const float hissHpC = (float) (1.0 - std::exp (-kTwoPi * 1500.0 / fs));
    const float hissLpC = (float) (1.0 - std::exp (-kTwoPi * 7000.0 / fs));
    const float crackleHpC = (float) std::exp (-kTwoPi * 3000.0 / fs);
    const float drive = 1.0f + 0.8f * vinyl;
    const double rev = revSeconds; // 1周 = 1.8秒ぶん

    // サンプル (読み込み中で使えないブロックは、無音にする)
    const juce::SpinLock::ScopedTryLockType lock (sampleLock);
    const SampleData* smp = (lock.isLocked() && sample != nullptr && sample->data.getNumSamples() > 4) ? sample.get() : nullptr;
    const bool sampleMode = src == srcSample;
    const double lenSec = smp != nullptr ? (double) smp->data.getNumSamples() / smp->rate : 0.0;
    const double cue = sampleMode ? (double) pStart->load() * lenSec : 0.0;

    // マウス: 掴んでいない間は、角度に追従。掴んでいる間は、前のブロックの終わりから今の値へ、1ブロックかけてつなぐ
    const bool touching = platterTouched.load();
    const double angleNow = platterAngle.load();
    if (! touching)
        angleSm = angleNow;
    const double angStart = angleSm;

    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    auto midiIt = midi.begin();

    for (int n = 0; n < numSamples; ++n)
    {
        // --- MIDI (サンプル精度) ---
        while (midiIt != midi.end() && (*midiIt).samplePosition <= n)
        {
            const auto m = (*midiIt).getMessage();
            if (m.isNoteOn())
                noteOn (m.getNoteNumber() & 127, m.getFloatVelocity());
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber() & 127);
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                heldStack.clear();
                noteActive = false;
            }
            ++midiIt;
        }

        if (pendingTrigger)
        {
            pendingTrigger = false;
            cueNow = cue;
            phase = 0.0;
            beatsSinceNote = 0.0;
            lastEvent = -1;
            lastCue = 0.0f;
            gateGain = gateTarget = 1.0f;
            if (! touching)
                pos = pos1 = target = cue; // 頭出し位置から始める
        }

        const double ratio = std::exp2 ((double) (currentNote - rootNote) / 12.0);
        const double angleCur = angStart + (angleNow - angStart) * (double) (n + 1) / (double) numSamples;

        // マウスで掴んだ/離した瞬間の切り替え
        if (touching != usingMouse)
        {
            usingMouse = touching;
            if (touching)
            {
                mouseBaseAngle = angleCur;
                mouseBasePos = pos;
            }
            else if (noteActive)
            {
                cueNow = pos; // 離した場所から、パターンを続ける
                phase = 0.0;
            }
        }

        // --- レコードの動き ---
        float gateCoefNow = gateCoef;
        float evGainTarget = 1.0f;
        if (usingMouse)
        {
            target = mouseBasePos + (angleCur - mouseBaseAngle) / kTwoPi * rev;
            gateTarget = 1.0f;
        }
        else if (noteActive && fillMode)
        {
            // フィル: フレーズの中の位置から、いま鳴らすイベントを探す (ノートが長ければループ)
            const double b = std::fmod (beatsSinceNote, phraseBeats);
            const double step = b * (double) ticksPerBeat;
            int found = -1;
            if (lastEvent >= 0 && lastEvent < live->n)
            {
                const auto& e = live->ev[(size_t) lastEvent];
                if (step >= e.start && step < e.start + e.len)
                    found = lastEvent;
            }
            if (found < 0)
                for (int i = 0; i < live->n; ++i)
                {
                    const auto& e = live->ev[(size_t) i];
                    if (step >= e.start && step < e.start + e.len)
                    {
                        found = i;
                        break;
                    }
                }
            lastEvent = found;

            if (found >= 0)
            {
                const auto& e = live->ev[(size_t) found];
                const double slotBeat = b - (double) e.start / (double) ticksPerBeat; // ブロックの頭からの経過 (拍)
                const double slotBeats = (double) e.len / (double) ticksPerBeat;
                const unsigned loopIdx = (unsigned) (long long) std::floor (beatsSinceNote / phraseBeats);

                // 人間味: 少し遅れて入る (最大12ms)。ループのたびに揺れ方が変わる(決まった乱数)
                const unsigned h = (unsigned) found * 7919u + loopIdx * 104729u;
                const double lateBeats = (double) human * 0.012 * (bpm / 60.0) * (double) hash01 (h, 1u);
                const double evBeat = slotBeat - lateBeats;
                const bool cutByGate = slotBeat >= slotBeats * (double) e.gate; // 「鳴らす長さ」を過ぎた

                if (evBeat < 0.0 || cutByGate)
                {
                    // 遅れて入る前、または、ブロックの終わりで切った後: レコードを頭に戻して、ミュート
                    target = cueNow + (double) e.cue;
                    gateTarget = 0.0f;
                    if (cutByGate)
                        gateCoefNow = gateRelCoef;
                    lastCue = e.cue;
                }
                else
                {
                    const double rb = rateBeats (e.rate);
                    const double cyc = evBeat / rb;
                    const float phiF = (float) (cyc - std::floor (cyc));
                    const unsigned cycleIdx = (unsigned) (long long) std::floor (cyc);
                    // 人間味: ストロークごとに、振り幅が少し揺れる / ブロックごとに、音量が少し揺れる
                    const float depthJit = 1.0f + human * 0.3f * (hash01 (h, cycleIdx + 11u) * 2.0f - 1.0f);
                    evGainTarget = 1.0f - human * 0.25f * hash01 (h, 3u);
                    const double r2 = ratio * std::exp2 ((double) e.pitch / 12.0);
                    lastCue = e.cue;
                    // このブロックだけのカーブ (曲げ) と、1往復の中で鳴らす終わり
                    const bool warped = e.bendRise != 0.0f || e.bendFall != 0.0f;
                    const float ap = live->apex[(size_t) juce::jlimit (0, numPatterns - 1, e.pattern)];
                    const float phiW = warped ? warpPhase (phiF, ap, e.bendRise, e.bendFall) : phiF;
                    target = cueNow + (double) e.cue + (double) rtD (e.pattern, phiW) * depthSec * (double) e.depth * (double) depthJit * r2;
                    // ミュートしたところは、レコードは動いたまま、フェーダーだけ閉じる (人間味の遅れにも合わせる)
                    const int relTick = (int) std::floor (evBeat * (double) ticksPerBeat);
                    const bool muted = relTick >= 0 && relTick < e.len && relTick < maxEventTicks && e.mute[(size_t) relTick];
                    const bool fast = warped ? rtFastWarped (e.pattern, phiF, cut, ap, e.bendRise, e.bendFall) : rtFast (e.pattern, phiF, cut);
                    // 鳴らす終わりを元の終わりより後ろにした時は、元の終わりからそこまで、フェーダーを開けたまま
                    const float nat = live->natEnd[(size_t) found];
                    const bool held = e.tail < 1.0f && e.tail > nat && phiF >= nat;
                    gateTarget = (! muted && phiF < e.tail && (held || (rtGate (e.pattern, phiW) && fast))) ? 1.0f : 0.0f;
                }
            }
            else
            {
                target = cueNow + (double) lastCue; // 休み: レコードを止めて、ミュート
                gateTarget = 0.0f;
            }
            beatsSinceNote += beatInc;
        }
        else if (noteActive)
        {
            const float phiF = (float) (phase - std::floor (phase));
            target = cueNow + (double) rtD (pattern, phiF) * depthSec * ratio;
            gateTarget = (rtGate (pattern, phiF) && rtFast (pattern, phiF, cut)) ? 1.0f : 0.0f;
            phase += phaseInc;
        }
        // どちらでもないときは、その場で止まる

        if (sampleMode)
            target = juce::jlimit (0.0, juce::jmax (0.0, lenSec), target);

        const double prev = pos;
        if (usingMouse)
        {
            // マウスの更新は、ブロックより疎なことがある。2段でなめらかにして、速度(=ピッチ)が階段状にならないようにする
            pos1 += (target - pos1) * (double) posCoef;
            pos += (pos1 - pos) * (double) posCoef;
        }
        else
        {
            pos += (target - pos) * (double) posCoef;
            pos1 = pos;
        }
        const float speed = (float) ((pos - prev) * fs); // 1 = 元の速さ

        gateGain += gateCoefNow * (gateTarget - gateGain);
        evGainSm += evGainCoef * (evGainTarget - evGainSm);
        speedEnv += envCoefSpeed * (std::abs (speed) - speedEnv);

        // 音量: ノートの強さ(マウスだけのときは一定)。止まっている間は無音 (実際のレコードと同じ)
        const bool sounding = noteActive || usingMouse;
        const float envTarget = sounding ? (noteActive ? velocityAmp : 0.8f) : 0.0f;
        env += (envTarget > env ? atkCoef : relCoef) * (envTarget - env);
        angle += (double) speed / fs * kTwoPi / rev;

        if (env < 1.0e-5f && ! sounding)
            continue;

        const float absSpeed = usingMouse ? speedEnv : std::abs (speed);
        const float gSpeed = smooth01 (absSpeed / 0.02f);
        const float gOut = gateGain * env * levelGain * evGainSm;
        const float g = gSpeed * gOut;

        // レコード感の、こすれる音 (左右共通)
        float scuff = 0.0f;
        if (useVinyl)
        {
            const float sp = juce::jmin (absSpeed, 3.0f);
            const float w = vinylRng.nextFloat() * 2.0f - 1.0f;
            hissLp += hissLpC * (w - hissLp);
            hissHp += hissHpC * (hissLp - hissHp);
            scuff = (hissLp - hissHp) * 0.03f * vinyl * sp;
            float imp = 0.0f;
            if (vinylRng.nextFloat() < 4.0f * vinyl * sp / (float) fs)
                imp = (vinylRng.nextFloat() * 2.0f - 1.0f) * 0.35f;
            crackleHp = crackleHpC * (crackleHp + imp - crackleX1); // 1次のハイパス (3kHz)
            crackleX1 = imp;
            scuff += crackleHp;
        }

        // --- 音源を、レコードの位置で読む ---
        float v[2] = { 0.0f, 0.0f };
        switch (src)
        {
            case srcSine:
                v[0] = (float) std::sin (kTwoPi * baseFreq * pos) * 0.9f;
                break;
            case srcSaw:
            {
                const double c = baseFreq * pos;
                const double t = c - std::floor (c);
                const double dt = juce::jmin (0.5, std::abs (baseFreq * (pos - prev)));
                v[0] = (float) ((2.0 * t - 1.0) - polyBlep (t, dt)) * 0.6f;
                break;
            }
            case srcSquare:
            {
                const double c = baseFreq * pos;
                const double t = c - std::floor (c);
                const double t2 = t + 0.5 - std::floor (t + 0.5);
                const double dt = juce::jmin (0.5, std::abs (baseFreq * (pos - prev)));
                v[0] = (float) ((t < 0.5 ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (t2, dt)) * 0.45f;
                break;
            }
            case srcNoise:
            {
                const double idx = pos * fs;
                const long long i0 = (long long) std::floor (idx);
                const float t = (float) (idx - (double) i0);
                const long long N = (long long) noiseTable.size();
                auto at = [&] (long long i) { return noiseTable[(size_t) (((i % N) + N) % N)]; };
                v[0] = hermite (at (i0 - 1), at (i0), at (i0 + 1), at (i0 + 2), t) * 0.55f;
                break;
            }
            default:
                if (smp != nullptr)
                {
                    const double idx = pos * smp->rate;
                    const long long i0 = (long long) std::floor (idx);
                    const float t = (float) (idx - (double) i0);
                    const long long N = smp->data.getNumSamples();
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float* d = smp->data.getReadPointer (ch);
                        auto at = [&] (long long i) { return (i < 0 || i >= N) ? 0.0f : d[i]; };
                        v[ch] = hermite (at (i0 - 1), at (i0), at (i0 + 1), at (i0 + 2), t);
                    }
                }
                break;
        }
        if (src != srcSample)
            v[1] = v[0];

        for (int ch = 0; ch < numCh; ++ch)
        {
            float y;
            if (useVinyl)
            {
                float x = v[ch] * gSpeed + scuff;
                vinylLp[ch][0] += vinylLpC * (x - vinylLp[ch][0]);
                vinylLp[ch][1] += vinylLpC * (vinylLp[ch][0] - vinylLp[ch][1]);
                x = std::tanh (vinylLp[ch][1] * drive) / drive;
                y = x * gOut;
            }
            else
            {
                y = v[ch] * g;
            }
            if (useTone)
            {
                toneState[ch][0] += toneCoef * (y - toneState[ch][0]);
                toneState[ch][1] += toneCoef * (toneState[ch][0] - toneState[ch][1]);
                y = toneState[ch][1];
            }
            data[ch][n] = y;
            peakOut = juce::jmax (peakOut, std::abs (y));
        }
    }

    angleSm = angleNow;

    // 表示用の情報
    const float decay = std::exp (-(float) numSamples / (0.25f * (float) fs));
    uiOutDb.store (juce::Decibels::gainToDecibels (peakOut, -100.0f));
    peakOut *= decay;
    uiPos.store (pos);
    uiAngle.store ((float) std::fmod (angle, kTwoPi));
    uiNote.store (noteActive ? currentNote : -1);
    uiFillBeat.store (noteActive && fillMode && ! usingMouse ? (float) std::fmod (beatsSinceNote, phraseBeats) : -1.0f);
    uiActive.store (env > 0.001f);
}

juce::AudioProcessorEditor* ScratchProcessor::createEditor()
{
    return new ScratchEditor (*this);
}

juce::ValueTree ScratchProcessor::phraseToTree (const Phrase& source) const
{
    juce::ValueTree ph ("PHRASE");
    ph.setProperty ("bars", source.bars, nullptr);
    ph.setProperty ("tmpl", source.templateIndex, nullptr);
    ph.setProperty ("random", source.isRandom, nullptr);
    ph.setProperty ("seed", (juce::int64) source.seed, nullptr);
    ph.setProperty ("edited", source.edited, nullptr);
    if (source.myName.isNotEmpty())
        ph.setProperty ("my", source.myName, nullptr);
    ph.setProperty ("tpb", ticksPerBar, nullptr); // 位置の単位 (1小節のティック数)
    for (auto& e : source.events)
    {
        juce::ValueTree ev ("EV");
        ev.setProperty ("s", e.start, nullptr);
        ev.setProperty ("l", e.len, nullptr);
        ev.setProperty ("p", e.pattern, nullptr);
        ev.setProperty ("r", e.rate, nullptr);
        ev.setProperty ("d", e.depth, nullptr);
        ev.setProperty ("t", e.pitch, nullptr);
        ev.setProperty ("c", e.cue, nullptr);
        ev.setProperty ("g", e.gate, nullptr);
        if (e.bendRise != 0.0f || e.bendFall != 0.0f || e.tail < 1.0f)
        {
            ev.setProperty ("br", e.bendRise, nullptr);
            ev.setProperty ("bf", e.bendFall, nullptr);
            ev.setProperty ("tl", e.tail, nullptr);
        }
        if (e.mute.any())
            ev.setProperty ("m", muteToString (e.mute, e.len), nullptr);
        ph.appendChild (ev, nullptr);
    }
    return ph;
}

ScratchProcessor::Phrase ScratchProcessor::phraseFromTree (const juce::ValueTree& ph)
{
    Phrase p;
    p.bars = juce::jlimit (1, 4, (int) ph.getProperty ("bars", 1));
    p.templateIndex = (int) ph.getProperty ("tmpl", 0);
    p.isRandom = (bool) ph.getProperty ("random", false);
    p.seed = (juce::uint32) (juce::int64) ph.getProperty ("seed", 0);
    p.edited = (bool) ph.getProperty ("edited", false);
    p.myName = ph.getProperty ("my").toString();
    // 古い保存データは16分音符単位 (1小節 = 16)
    const int scale = juce::jmax (1, ticksPerBar / juce::jmax (1, (int) ph.getProperty ("tpb", stepsPerBar)));
    for (int i = 0; i < ph.getNumChildren(); ++i)
    {
        const auto ev = ph.getChild (i);
        FillEvent e;
        e.start = (int) ev.getProperty ("s", 0) * scale;
        e.len = juce::jlimit (1, maxEventTicks, (int) ev.getProperty ("l", 1) * scale);
        e.pattern = juce::jlimit (0, numPatterns - 1, (int) ev.getProperty ("p", 0));
        e.rate = juce::jlimit (0, numRates - 1, (int) ev.getProperty ("r", 2));
        muteFromString (ev.getProperty ("m").toString(), e.mute, e.len, scale);
        e.depth = (float) ev.getProperty ("d", 1.0f);
        e.pitch = (int) ev.getProperty ("t", 0);
        e.cue = (float) ev.getProperty ("c", 0.0f);
        e.gate = juce::jlimit (0.1f, 1.0f, (float) ev.getProperty ("g", 1.0f));
        e.bendRise = juce::jlimit (-1.0f, 1.0f, (float) ev.getProperty ("br", 0.0f));
        e.bendFall = juce::jlimit (-1.0f, 1.0f, (float) ev.getProperty ("bf", 0.0f));
        e.tail = juce::jlimit (0.03f, 1.0f, (float) ev.getProperty ("tl", 1.0f));
        p.events.push_back (e);
    }
    return p;
}

juce::ValueTree ScratchProcessor::shapeToTree (int pi) const
{
    const auto& u = shapes[(size_t) pi];
    juce::ValueTree sh ("SH");
    juce::StringArray pts, ten;
    juce::String cur, gate;
    for (auto& p : u.points)
        pts.add (juce::String (p.x, 6) + "," + juce::String (p.y, 6));
    for (size_t k = 0; k + 1 < u.points.size(); ++k)
    {
        ten.add (juce::String (k < u.tension.size() ? u.tension[k] : 0.0f, 3));
        cur << (int) (k < u.curve.size() ? u.curve[k] : 0);
    }
    for (bool g : u.gate)
        gate << (g ? "1" : "0");
    sh.setProperty ("p", pi, nullptr);
    sh.setProperty ("pts", pts.joinIntoString (";"), nullptr);
    sh.setProperty ("ten", ten.joinIntoString (","), nullptr);
    sh.setProperty ("cur", cur, nullptr);
    sh.setProperty ("gate", gate, nullptr);
    return sh;
}

void ScratchProcessor::readShape (const juce::ValueTree& t, int pi)
{
    Shape u;
    for (auto& tok : juce::StringArray::fromTokens (t.getProperty ("pts").toString(), ";", ""))
        u.points.push_back ({ juce::jlimit (0.0f, 1.0f, tok.upToFirstOccurrenceOf (",", false, false).getFloatValue()),
                             juce::jlimit (0.0f, 1.0f, tok.fromFirstOccurrenceOf (",", false, false).getFloatValue()) });
    const auto ten = juce::StringArray::fromTokens (t.getProperty ("ten").toString(), ",", "");
    const auto cur = t.getProperty ("cur").toString();
    for (size_t k = 0; k + 1 < u.points.size(); ++k)
    {
        u.tension.push_back (juce::jlimit (-1.0f, 1.0f, (int) k < ten.size() ? ten[(int) k].getFloatValue() : 0.0f));
        u.curve.push_back ((unsigned char) juce::jlimit (0, 2, (int) k < cur.length() ? (int) (cur[(int) k] - '0') : 0));
    }
    const auto gate = t.getProperty ("gate").toString();
    for (int k = 0; k < userGateSegments; ++k)
        u.gate[(size_t) k] = k >= gate.length() || gate[k] == '1';
    // 壊れたデータは使わない (両端が 0 と 1 で、昇順)
    bool ok = u.points.size() >= 2 && (int) u.points.size() <= maxUserPoints
              && u.points.front().x == 0.0f && u.points.back().x == 1.0f;
    for (size_t i = 1; ok && i < u.points.size(); ++i)
        ok = u.points[i].x > u.points[i - 1].x;
    if (! ok)
        return;
    u.points.front().y = u.points.back().y = 0.0f;
    shapes[(size_t) pi] = std::move (u);
    if (pi < numPresetPatterns)
        presetEdited[(size_t) pi] = true;
    rebuildShapeTable (pi);
}

void ScratchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", samplePath, nullptr);
    state.setProperty ("patver", 2, nullptr); // パターン番号の並び (2 = ブレーキ・ザップ追加後。ユーザー波形は 14〜)

    if (auto old = state.getChildWithName ("PHRASE"); old.isValid())
        state.removeChild (old, nullptr);
    state.appendChild (phraseToTree (phrase), nullptr);

    for (auto* name : { "USERPAT", "SHAPES" })
        if (auto old = state.getChildWithName (name); old.isValid())
            state.removeChild (old, nullptr);
    juce::ValueTree shs ("SHAPES");
    for (int pi = 0; pi < numPatterns; ++pi)
        if (isShapeEdited (pi))
            shs.appendChild (shapeToTree (pi), nullptr);
    state.appendChild (shs, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ScratchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);

            // 古い保存データ: ユーザー波形の番号が 12〜15 だった (ブレーキ・ザップを足して 14〜17 になった)
            if (! tree.hasProperty ("patver"))
            {
                constexpr int oldUser = 12, shift = 2;
                for (int i = 0; i < tree.getNumChildren(); ++i)
                {
                    auto c = tree.getChild (i);
                    if (c.hasType ("PARAM") && c.getProperty ("id").toString() == "pattern" && (float) c.getProperty ("value") >= (float) oldUser - 0.5f)
                        c.setProperty ("value", (float) c.getProperty ("value") + (float) shift, nullptr);
                }
                auto oldPh = tree.getChildWithName ("PHRASE");
                for (int i = 0; i < oldPh.getNumChildren(); ++i)
                {
                    auto ev = oldPh.getChild (i);
                    if ((int) ev.getProperty ("p", 0) >= oldUser)
                        ev.setProperty ("p", (int) ev.getProperty ("p") + shift, nullptr);
                }
            }
            apvts.replaceState (tree);

            const auto ph = apvts.state.getChildWithName ("PHRASE");
            if (ph.isValid())
            {
                history.clear();
                phrase = phraseFromTree (ph);
            }

            // 波形: 新しい形式 (SHAPES) か、古い形式 (USERPAT: ユーザー波形だけ、なめらか(自動)のカーブ)
            presetEdited.fill (false);
            if (const auto shs = apvts.state.getChildWithName ("SHAPES"); shs.isValid())
            {
                for (int i = 0; i < shs.getNumChildren(); ++i)
                {
                    const auto sh = shs.getChild (i);
                    const int pi = (int) sh.getProperty ("p", -1);
                    if (validPattern (pi))
                        readShape (sh, pi);
                }
            }
            else
            {
                const auto ups = apvts.state.getChildWithName ("USERPAT");
                for (int s = 0; s < juce::jmin (numUserPatterns, ups.getNumChildren()); ++s)
                    readShape (ups.getChild (s), numPresetPatterns + s);
            }
            shapeHistory.clear();
            publishPhrase();

            const auto path = apvts.state.getProperty ("samplePath").toString();
            if (path.startsWith ("preset:"))
            {
                const auto res = path.fromFirstOccurrenceOf ("preset:", false, false);
                for (int i = 0; i < kNumPresetSamples; ++i)
                    if (res == kPresetSamples[i].resource && path != samplePath)
                        loadPresetSample (i);
            }
            else if (path.isNotEmpty() && path != samplePath)
            {
                const juce::File f (path);
                if (f.existsAsFile())
                    loadSample (f);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScratchProcessor();
}
