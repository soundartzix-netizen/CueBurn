// ホスト無しで Scratch(楽器) にノートを送り、出力を数値で確認するテスト
#include "PluginProcessor.h"
#include <cstdio>

namespace
{
constexpr double fs = 44100.0;
constexpr int block = 256;
int failures = 0;
constexpr int T = ScratchProcessor::ticksPerStep; // 16分音符 = 6ティック

void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

void setParam (ScratchProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

void prepare (ScratchProcessor& p)
{
    p.setPlayConfigDetails (0, 2, fs, block);
    p.prepareToPlay (fs, block);
    setParam (p, "vinyl", 0.0f); // 既存のテストは、レコード感なしで正確に比べる (レコード感は V1 で確かめる)
    setParam (p, "cut", 0.0f);   // カットも無し (X1 で確かめる)
    setParam (p, "src", 0.0f);   // 既存のテストは、サイン波が前提 (初期値はプリセットの声になった)
    p.loadTemplate (0);          // 既存のテストは、ベイビー基本が前提 (初期値は「空」になった)
}

// ブロック数ぶん処理して、出力(左)を返す。events[i] は、i番目のブロックの頭で送るMIDI
std::vector<float> run (ScratchProcessor& p, int blocks, std::map<int, juce::MidiMessage> events = {},
                        std::function<void (int)> beforeBlock = nullptr)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, block);
    for (int b = 0; b < blocks; ++b)
    {
        if (beforeBlock)
            beforeBlock (b);
        juce::MidiBuffer m;
        auto it = events.find (b);
        if (it != events.end())
            m.addEvent (it->second, 0);
        p.processBlock (buf, m);
        for (int i = 0; i < block; ++i)
            out.push_back (buf.getSample (0, i));
    }
    return out;
}

int blocksFor (double seconds) { return (int) std::ceil (seconds * fs / block); }
juce::MidiMessage on (int note, int vel = 127) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) vel); }
juce::MidiMessage off (int note) { return juce::MidiMessage::noteOff (1, note); }

double rms (const std::vector<float>& v, size_t from, size_t to)
{
    to = std::min (to, v.size());
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += (double) v[i] * v[i];
    return std::sqrt (s / (double) std::max<size_t> (1, to - from));
}

double peak (const std::vector<float>& v, size_t from, size_t to)
{
    to = std::min (to, v.size());
    double m = 0.0;
    for (size_t i = from; i < to; ++i)
        m = std::max (m, (double) std::abs (v[i]));
    return m;
}

int zeroCrossings (const std::vector<float>& v, size_t from, size_t to)
{
    to = std::min (to, v.size());
    int c = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((v[i - 1] < 0.0f) != (v[i] < 0.0f))
            ++c;
    return c;
}

// 値が「位置(秒)÷4」になるサンプル (4秒)。読み出し位置を、出力の値から直接確かめられる
void loadRampSample (ScratchProcessor& p)
{
    const int n = (int) (4.0 * fs);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) ((double) i / fs / 4.0);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    p.setSample (std::move (b), fs, "ramp.wav", "");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // ------------------------------------------------------------------
    std::printf ("1) ノートが無いときは、完全に無音\n");
    {
        ScratchProcessor p;
        prepare (p);
        auto out = run (p, blocksFor (0.3));
        check (peak (out, 0, out.size()) == 0.0, "出力が 0");
    }

    // ------------------------------------------------------------------
    std::printf ("2) ノートで鳴り、離すと止まる (サイン)\n");
    {
        ScratchProcessor p;
        prepare (p);
        auto out = run (p, blocksFor (0.6), { { 0, on (60) }, { blocksFor (0.4), off (60) } });
        check (rms (out, (size_t) (0.02 * fs), (size_t) (0.38 * fs)) > 0.05, "ノートの間は、鳴る (rms " + juce::String (rms (out, (size_t) (0.02 * fs), (size_t) (0.38 * fs)), 3) + ")");
        check (peak (out, (size_t) (0.5 * fs), out.size()) < 1.0e-3, "離して100ms後には、無音 (peak " + juce::String (peak (out, (size_t) (0.5 * fs), out.size()), 6) + ")");
        check (p.getCurrentNote() == -1, "ノートは無し");
    }

    // ------------------------------------------------------------------
    std::printf ("3) ベイビー: 頭出し位置から前へ押し出して、戻る (サンプル, 1/8, 振り幅250ms, 頭出し25%%)\n");
    {
        ScratchProcessor p;
        prepare (p);
        loadRampSample (p);
        setParam (p, "src", (float) ScratchProcessor::srcSample);
        setParam (p, "mode", 1.0f);     // 単発
        setParam (p, "pattern", 0.0f);
        setParam (p, "rate", 2.0f);     // 1/8 = 0.5拍 = 0.25秒
        setParam (p, "depth", 250.0f);
        setParam (p, "feel", 1.0f);
        setParam (p, "start", 0.25f);   // 頭出し = 1.0秒
        setParam (p, "level", 0.0f);
        auto out = run (p, blocksFor (0.3), { { 0, on (60) } });

        const int cycle = (int) (0.25 * fs);
        // 予想はパターンの形そのもの。レコードが動いている所で測る (止まっている所は無音で、位置が読めない)
        auto expectAt = [] (double phi) { return (1.0 + (double) ScratchProcessor::patternD (0, (float) phi) * 0.25) / 4.0; };
        for (double phi : { 0.05, 0.6, 0.85 })
        {
            const size_t k = (size_t) (phi * cycle);
            // 重さ(1ms)のなまりで、速いほど少し遅れる。4ms + 速さ×1.2ms まで許す
            const double sp = std::abs ((double) ScratchProcessor::patternD (0, (float) phi + 0.001f) - (double) ScratchProcessor::patternD (0, (float) phi)) / 0.001 * 0.25 / 0.25;
            const double tol = (0.004 + 0.0012 * sp) / 4.0;
            check (std::abs (out[k] - expectAt (phi)) < tol,
                   "位相 " + juce::String (phi, 2) + " の位置: " + juce::String (out[k] * 4.0, 4) + " 秒 (予想 " + juce::String (expectAt (phi) * 4.0, 4) + " 秒)");
        }
        check (out[(size_t) (0.05 * cycle)] * 4.0 > 1.0, "動き出しは、頭出し位置より前(未来)へ");
    }

    // ------------------------------------------------------------------
    std::printf ("4) 音程: 1オクターブ上のノートは、2倍の高さ (サイン)\n");
    {
        auto crossings = [] (int note)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "mode", 1.0f);
            setParam (p, "feel", 1.0f);
            auto out = run (p, blocksFor (0.3), { { 0, on (note) } });
            return zeroCrossings (out, 0, (size_t) (0.25 * fs)); // ちょうど1往復
        };
        const int c60 = crossings (60), c72 = crossings (72);
        const double ratio = (double) c72 / (double) juce::jmax (1, c60);
        check (std::abs (ratio - 2.0) < 0.1, "ゼロ交差の数の比 " + juce::String (ratio, 2) + " (C5: " + juce::String (c60) + ", C6: " + juce::String (c72) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("5) フォワード: 押し出すときだけ聞こえる\n");
    {
        ScratchProcessor p;
        prepare (p);
        setParam (p, "mode", 1.0f);
        setParam (p, "pattern", 1.0f);
        setParam (p, "rate", 2.0f);
        setParam (p, "feel", 1.0f);
        auto out = run (p, blocksFor (0.3), { { 0, on (60) } });
        const int cycle = (int) (0.25 * fs);
        const double push = rms (out, (size_t) (0.05 * cycle), (size_t) (0.30 * cycle));
        const double back = rms (out, (size_t) (0.50 * cycle), (size_t) (0.95 * cycle));
        check (push > 0.1, "押し出すときは、聞こえる (rms " + juce::String (push, 3) + ")");
        check (back < 0.01, "引き戻す間は、ミュート (rms " + juce::String (back, 4) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("6) 単音: 後から押したノートが優先、離すと前のノートに戻る\n");
    {
        ScratchProcessor p;
        prepare (p);
        run (p, 4, { { 0, on (60) }, { 2, on (64) } });
        check (p.getCurrentNote() == 64, "2つ目のノートが鳴る");
        run (p, 2, { { 0, off (64) } });
        check (p.getCurrentNote() == 60, "2つ目を離すと、1つ目に戻る");
        run (p, 2, { { 0, off (60) } });
        check (p.getCurrentNote() == -1, "両方離すと、止まる");
    }

    // ------------------------------------------------------------------
    std::printf ("7) ベロシティで音量が変わる\n");
    {
        auto level = [] (int vel)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "mode", 1.0f);
            auto out = run (p, blocksFor (0.3), { { 0, on (60, vel) } });
            return rms (out, (size_t) (0.02 * fs), out.size());
        };
        const double ratio = level (127) / level (32);
        check (std::abs (ratio - 127.0 / 32.0) < 0.3, "強さ127と32の音量比 " + juce::String (ratio, 2) + " (予想 3.97)");
    }

    // ------------------------------------------------------------------
    std::printf ("8) ノートが無くても、レコードを回せば鳴る。止めると無音、離すと無音\n");
    {
        ScratchProcessor p;
        prepare (p);
        setParam (p, "src", (float) ScratchProcessor::srcNoise);
        p.platterTouched.store (true);
        auto moving = run (p, blocksFor (0.3), {}, [&] (int) { p.platterAngle.store (p.platterAngle.load() + 0.02); });
        check (rms (moving, (size_t) (0.1 * fs), moving.size()) > 0.05, "回している間は、鳴る (rms " + juce::String (rms (moving, (size_t) (0.1 * fs), moving.size()), 3) + ")");
        auto still = run (p, blocksFor (0.2));
        check (rms (still, still.size() / 2, still.size()) < 1.0e-3, "止めると、無音 (rms " + juce::String (rms (still, still.size() / 2, still.size()), 6) + ")");
        p.platterTouched.store (false);
        auto released = run (p, blocksFor (0.2), {}, [&] (int) { p.platterAngle.store (p.platterAngle.load() + 0.02); });
        check (peak (released, released.size() / 2, released.size()) < 1.0e-4, "離すと、無音");
    }

    // ------------------------------------------------------------------
    std::printf ("9) 読み込めないファイルを指定しても、前のサンプル (初めはプリセットの声) のまま鳴る (落ちない)\n");
    {
        ScratchProcessor p;
        prepare (p);
        setParam (p, "src", (float) ScratchProcessor::srcSample);
        const bool loaded = p.loadSample (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no_such_scratch_file.wav"));
        auto out = run (p, blocksFor (0.3), { { 0, on (60) } });
        check (! loaded && p.getPresetIndex() == 0, "読み込みは失敗し、プリセットのまま");
        check (peak (out, 0, out.size()) > 0.05, "プリセットの声で鳴る (peak " + juce::String (peak (out, 0, out.size()), 3) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("10) 全音源・全パターン・極端な設定でも、破綻しない\n");
    {
        bool finite = true;
        double pk = 0.0;
        for (int src = 0; src < ScratchProcessor::numSources; ++src)
            for (int pat = 0; pat < ScratchProcessor::numPatterns; ++pat)
                for (int rate : { 0, 4 })
                {
                    ScratchProcessor p;
                    prepare (p);
                    if (src == ScratchProcessor::srcSample)
                        loadRampSample (p);
                    setParam (p, "src", (float) src);
                    setParam (p, "mode", 1.0f);
                    setParam (p, "pattern", (float) pat);
                    setParam (p, "rate", (float) rate);
                    setParam (p, "depth", 1000.0f);
                    setParam (p, "feel", 1.0f);
                    setParam (p, "start", 0.95f);
                    setParam (p, "level", 0.0f);
                    for (float v : run (p, blocksFor (0.5), { { 0, on (96) } }))
                    {
                        if (! std::isfinite (v))
                            finite = false;
                        pk = std::max (pk, (double) std::abs (v));
                    }
                }
        // 0dB でフルスケールの音源を鳴らすので、補間のわずかな行き過ぎを含めて 1.2 以内
        check (finite && pk < 1.2, "有限値で、振幅も範囲内 (peak " + juce::String (pk, 3) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("11) パターンの形: 頭出し位置から始まり、1周で戻る。途中に飛びが無い\n");
    {
        bool ok = true;
        for (int pat = 0; pat < ScratchProcessor::numPatterns; ++pat)
        {
            if (std::abs (ScratchProcessor::patternD (pat, 0.0f)) > 1.0e-4f || std::abs (ScratchProcessor::patternD (pat, 0.99999f)) > 1.0e-3f)
                ok = false;
            float prev = ScratchProcessor::patternD (pat, 0.0f);
            for (int i = 1; i <= 1000; ++i)
            {
                const float d = ScratchProcessor::patternD (pat, (float) i / 1000.0f);
                if (std::abs (d - prev) > 0.08f || d < -1.0e-4f || d > 1.0001f)
                    ok = false;
                prev = d;
            }
        }
        check (ok, "全パターンで OK");
    }

    // ------------------------------------------------------------------
    std::printf ("12) WAVファイルの読み込み\n");
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("scratch_test.wav");
        file.deleteFile();
        {
            juce::AudioBuffer<float> b (1, (int) (0.5 * fs));
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.5f * (float) std::sin (6.283185307 * 440.0 * i / fs));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), fs, 1, 16, {}, 0));
            if (w != nullptr)
            {
                os.release();
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        }
        ScratchProcessor p;
        prepare (p);
        const bool ok = p.loadSample (file);
        check (ok && std::abs (p.getSampleSeconds() - 0.5) < 0.001 && p.getSampleName() == "scratch_test.wav",
               "読み込めて、長さ " + juce::String (p.getSampleSeconds(), 3) + " 秒");
        setParam (p, "src", (float) ScratchProcessor::srcSample);
        auto out = run (p, blocksFor (0.3), { { 0, on (60) } });
        check (rms (out, (size_t) (0.02 * fs), out.size()) > 0.02, "読み込んだ音で、鳴る");
        check (! p.loadSample (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no_such_file.wav")), "存在しないファイルは、失敗を返す");
        file.deleteFile();
    }

    // ------------------------------------------------------------------
    std::printf ("13) 状態の保存と復元\n");
    {
        ScratchProcessor p;
        prepare (p);
        setParam (p, "pattern", 4.0f);
        setParam (p, "depth", 333.0f);
        setParam (p, "src", 3.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);

        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check ((int) q.apvts.getRawParameterValue ("pattern")->load() == 4 && std::abs (q.apvts.getRawParameterValue ("depth")->load() - 333.0f) < 1.0f
                   && (int) q.apvts.getRawParameterValue ("src")->load() == 3,
               "パラメーターが復元される");
    }


    // ==================================================================
    // フィル
    // ==================================================================
    auto validPhrase = [] (const ScratchProcessor::Phrase& ph) -> juce::String
    {
        const int steps = ph.bars * ScratchProcessor::ticksPerBar;
        int prevEnd = 0;
        if (ph.events.empty())
            return "イベントが無い";
        for (auto& e : ph.events)
        {
            if (e.len <= 0 || e.start < 0 || e.start + e.len > steps)
                return "範囲外 (" + juce::String (e.start) + "+" + juce::String (e.len) + ")";
            if (e.start < prevEnd)
                return "重なり (" + juce::String (e.start) + ")";
            if (e.pattern < 0 || e.pattern >= ScratchProcessor::numPatterns || e.rate < 0 || e.rate >= ScratchProcessor::numRates)
                return "パターン/速さが不正";
            prevEnd = e.start + e.len;
        }
        return {};
    };
    auto sameEvents = [] (const ScratchProcessor::Phrase& a, const ScratchProcessor::Phrase& b)
    {
        if (a.bars != b.bars || a.events.size() != b.events.size())
            return false;
        for (size_t i = 0; i < a.events.size(); ++i)
        {
            const auto& x = a.events[i];
            const auto& y = b.events[i];
            if (x.start != y.start || x.len != y.len || x.pattern != y.pattern || x.rate != y.rate || x.pitch != y.pitch
                || std::abs (x.depth - y.depth) > 1.0e-4f || std::abs (x.cue - y.cue) > 1.0e-4f || std::abs (x.gate - y.gate) > 1.0e-4f)
                return false;
        }
        return true;
    };

    // ------------------------------------------------------------------
    std::printf ("F1) フィル: ノート1つで、テンプレート「ベイビー基本」が鳴る。休みは無音、長いノートはループ (120BPM)\n");
    {
        ScratchProcessor p;
        prepare (p);
        setParam (p, "feel", 1.0f);
        setParam (p, "human", 0.0f);
        // ベイビー基本: 0〜8ステップ(0〜1秒、鳴らす長さ90%=0.9秒まで) 1/8、休み、10〜16(1.25〜2秒、75%=1.7秒まで) 1/16
        auto out = run (p, blocksFor (2.3), { { 0, on (60) } });
        const double a = rms (out, (size_t) (0.05 * fs), (size_t) (0.85 * fs));
        const double rest = rms (out, (size_t) (0.95 * fs), (size_t) (1.23 * fs));
        const double b = rms (out, (size_t) (1.28 * fs), (size_t) (1.65 * fs));
        const double loop = rms (out, (size_t) (2.03 * fs), (size_t) (2.25 * fs));
        check (a > 0.05, "最初のイベントで鳴る (rms " + juce::String (a, 3) + ")");
        check (rest < 0.005, "休みの間は無音 (rms " + juce::String (rest, 5) + ")");
        check (b > 0.05, "2つ目のイベントで鳴る (rms " + juce::String (b, 3) + ")");
        check (loop > 0.05, "1小節を過ぎると、頭からループ (rms " + juce::String (loop, 3) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("F2) 全テンプレート x 1/2/4小節: 範囲内・重なり無し。2小節以上は、最後の小節に締めのロール\n");
    {
        juce::String err;
        bool turnaround = true;
        for (int t = 0; t < ScratchProcessor::numTemplates; ++t)
            for (int bars : { 1, 2, 4 })
            {
                const auto ph = ScratchProcessor::makeTemplate (t, bars);
                const auto v = validPhrase (ph);
                if (v.isNotEmpty() && err.isEmpty())
                    err = "テンプレート " + juce::String (t) + " / " + juce::String (bars) + "小節: " + v;
                if (bars > 1)
                {
                    const int last = (bars - 1) * ScratchProcessor::ticksPerBar;
                    bool found = false;
                    for (auto& e : ph.events)
                        if (e.start >= last + 12 * T && e.pitch >= 2)
                            found = true;
                    turnaround = turnaround && found;
                }
            }
        check (err.isEmpty(), err.isEmpty() ? juce::String (ScratchProcessor::numTemplates * 3) + "通り、すべて正しい" : err);
        check (turnaround, "2/4小節では、最後の拍に締めのロールがある");
    }

    // ------------------------------------------------------------------
    std::printf ("F3) ランダム: 200通りすべて正しく、1拍目から入る。同じシードなら同じ結果\n");
    {
        juce::String err;
        bool downbeat = true, deterministic = true, diverse = false;
        ScratchProcessor::Phrase firstOne;
        for (juce::uint32 seed = 1; seed <= 200; ++seed)
        {
            const int bars = (int) (seed % 3) == 0 ? 1 : ((int) (seed % 3) == 1 ? 2 : 4);
            const auto ph = ScratchProcessor::makeRandom (seed, bars);
            const auto v = validPhrase (ph);
            if (v.isNotEmpty() && err.isEmpty())
                err = "シード " + juce::String ((int) seed) + ": " + v;
            if (ph.events.empty() || ph.events.front().start != 0)
                downbeat = false;
            if (! sameEvents (ph, ScratchProcessor::makeRandom (seed, bars)))
                deterministic = false;
            if (seed == 3)
                firstOne = ph;
            else if (seed > 3 && ph.bars == firstOne.bars && ! sameEvents (ph, firstOne))
                diverse = true;
        }
        check (err.isEmpty(), err.isEmpty() ? juce::String ("200通り、すべて正しい") : err);
        check (downbeat, "すべて1拍目から入る");
        check (deterministic, "同じシードなら、同じフレーズ");
        check (diverse, "シードが違えば、違うフレーズ");
    }

    // ------------------------------------------------------------------
    std::printf ("F4) バリエーション: リズム(開始・長さ)はそのまま、中身が少し変わる\n");
    {
        const auto src = ScratchProcessor::makeTemplate (1, 2);
        const auto var = ScratchProcessor::makeVariation (src, 99);
        bool sameRhythm = src.events.size() == var.events.size();
        for (size_t i = 0; sameRhythm && i < src.events.size(); ++i)
            sameRhythm = src.events[i].start == var.events[i].start && src.events[i].len == var.events[i].len;
        check (sameRhythm, "開始と長さは同じ");
        check (! sameEvents (src, var), "パターン・速さ・高さのどれかが変わる");
        check (var.edited, "編集済みになる");
    }

    // ------------------------------------------------------------------
    std::printf ("F5) 戻る: ひとつ前のフレーズに戻れる\n");
    {
        ScratchProcessor p;
        check (! p.canGoBack(), "最初は戻れない");
        p.loadTemplate (3);
        p.randomizeWithSeed (42);
        check (p.getPhrase().isRandom, "ランダムになっている");
        p.back();
        check (! p.getPhrase().isRandom && p.getPhrase().templateIndex == 3, "戻ると、テンプレート4番目");
        p.back();
        check (p.getPhrase().templateIndex == ScratchProcessor::emptyTemplate, "もう一度戻ると、最初の「空」");
        check (! p.back(), "それ以上は戻れない");
    }

    // ------------------------------------------------------------------
    std::printf ("F6) 長さの変更: 作ったままなら作り直し、手を加えたなら、くり返して伸ばす\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (2);
        p.setBars (4);
        check (p.getPhrase().bars == 4 && sameEvents (p.getPhrase(), ScratchProcessor::makeTemplate (2, 4)), "テンプレートを4小節で作り直す");

        p.setBars (1);
        p.cycleEventPattern (0); // 手を加える
        const auto edited = p.getPhrase();
        p.setBars (2);
        check (p.getPhrase().bars == 2 && p.getPhrase().events.size() == edited.events.size() * 2 && validPhrase (p.getPhrase()).isEmpty(),
               "手を加えたフレーズは、2回くり返して2小節に");
        check (p.getPhrase().events[0].pattern == edited.events[0].pattern, "手を加えた内容は残る");
    }

    // ------------------------------------------------------------------
    std::printf ("F7) 試聴: ノートが無くても鳴り、止めると無音\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.previewOn.store (true);
        auto out = run (p, blocksFor (0.6));
        check (rms (out, (size_t) (0.05 * fs), out.size()) > 0.05, "試聴中は鳴る");
        p.previewOn.store (false);
        auto stop = run (p, blocksFor (0.2));
        check (peak (stop, stop.size() / 2, stop.size()) < 1.0e-3, "止めると無音");
    }

    // ------------------------------------------------------------------
    std::printf ("F8) フレーズは、プロジェクトに保存され、復元される\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.setBars (2);
        p.randomizeWithSeed (7);
        p.vary();
        const auto saved = p.getPhrase();
        juce::MemoryBlock mb;
        p.getStateInformation (mb);

        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (sameEvents (q.getPhrase(), saved) && q.getPhrase().edited == saved.edited && q.getPhrase().seed == saved.seed,
               "イベント・長さ・種類が、そのまま戻る");
    }


    // ------------------------------------------------------------------
    std::printf ("G1) ブロックごとの「鳴らす長さ」: 短くすると、そこで切れる (ベイビー基本: 最初のブロックは0〜1秒)\n");
    {
        auto levelAfter = [] (float gate, double from, double to)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "feel", 1.0f);
            setParam (p, "human", 0.0f);
            p.loadTemplate (0);
            p.setEventGate (0, gate);
            auto out = run (p, blocksFor (1.0), { { 0, on (60) } });
            return rms (out, (size_t) (from * fs), (size_t) (to * fs));
        };
        const double full = levelAfter (1.0f, 0.55, 0.95);
        const double cut = levelAfter (0.5f, 0.55, 0.95);   // 0.5秒で切る -> 0.55秒以降は無音
        const double before = levelAfter (0.5f, 0.05, 0.45); // 切る前は、普通に鳴っている
        check (full > 0.05, "切らないときは、後半も鳴っている (rms " + juce::String (full, 3) + ")");
        check (cut < 0.005, "半分(0.5秒)で切ると、その後は無音 (rms " + juce::String (cut, 5) + ")");
        check (before > 0.05, "切る前までは、普通に鳴っている (rms " + juce::String (before, 3) + ")");

        ScratchProcessor q;
        prepare (q);
        q.loadTemplate (0);
        q.setEventGate (0, 0.5f);
        check (std::abs (q.getPhrase().events[0].gate - 0.5f) < 1.0e-6f && q.getPhrase().edited, "値が保存され、編集済みになる");
    }

    // ------------------------------------------------------------------
    std::printf ("G2) ブロックごとの振り幅: 大きいほど、レコードが遠くまで動く (サンプル、人間味0)\n");
    {
        auto reach = [] (float depth)
        {
            ScratchProcessor p;
            prepare (p);
            loadRampSample (p);
            setParam (p, "src", (float) ScratchProcessor::srcSample);
            setParam (p, "feel", 1.0f);
            setParam (p, "human", 0.0f);
            setParam (p, "depth", 250.0f);
            setParam (p, "start", 0.25f);
            setParam (p, "level", 0.0f);
            p.loadTemplate (14);              // ミニマル: 最初のブロック = チャープ 1/8 (0.25秒で1往復)
            p.setEventDepth (0, depth);
            p.setEventGate (0, 1.0f);
            auto out = run (p, blocksFor (0.25), { { 0, on (60) } });
            // 押し出す一番速いところ (位相 0.25 付近) の位置
            return (double) out[(size_t) (0.25 * 0.25 * fs)] * 4.0 - 1.0; // 頭出し(1.0秒)からの移動量
        };
        const double a = reach (0.5f), b = reach (1.0f);
        check (b > a * 1.7 && b < a * 2.3, "振り幅を2倍にすると、動く量もほぼ2倍 (0.5: " + juce::String (a, 3) + " 秒 / 1.0: " + juce::String (b, 3) + " 秒)");
    }

    // ------------------------------------------------------------------
    std::printf ("G3) ランダム: 振り幅と長さが、ブロックごとにばらつく\n");
    {
        int phrases = 0, variedDepth = 0, variedGate = 0, accentOk = 0, inRange = 0;
        for (juce::uint32 seed = 1; seed <= 200; ++seed)
        {
            const auto ph = ScratchProcessor::makeRandom (seed, 2);
            if (ph.events.size() < 4)
                continue;
            ++phrases;
            float dMin = 9.0f, dMax = 0.0f;
            bool ok = true, hasCut = false, hasFull = false;
            for (auto& e : ph.events)
            {
                dMin = std::min (dMin, e.depth);
                dMax = std::max (dMax, e.depth);
                hasCut = hasCut || e.gate < 0.95f;
                hasFull = hasFull || e.gate >= 0.95f;
                if (e.depth < 0.2f || e.depth > 1.5f || e.gate < 0.1f || e.gate > 1.0f)
                    ok = false;
            }
            if (ok)
                ++inRange;
            if (dMax - dMin >= 0.3f)
                ++variedDepth;
            if (hasCut && hasFull)
                ++variedGate;
            if (ph.events.front().depth >= 1.1f) // 1拍目は、アクセント(大きめ)
                ++accentOk;
        }
        check (inRange == phrases, "すべて範囲内 (振り幅 0.2〜1.5 / 長さ 0.1〜1)");
        check (variedDepth > phrases * 0.9, "振り幅が、ブロックごとに違う (" + juce::String (variedDepth) + " / " + juce::String (phrases) + ")");
        check (variedGate > phrases * 0.6, "途中で切れるブロックと、最後まで鳴るブロックが混ざる (" + juce::String (variedGate) + " / " + juce::String (phrases) + ")");
        check (accentOk == phrases, "1拍目のブロックは、大きめ (アクセント)");
    }

    // ------------------------------------------------------------------
    std::printf ("G4) テンプレートも、ブロックごとに強弱がある\n");
    {
        bool ok = true;
        for (int t = 0; t < ScratchProcessor::numTemplates; ++t)
        {
            const auto ph = ScratchProcessor::makeTemplate (t, 1);
            float dMin = 9.0f, dMax = 0.0f, gMin = 9.0f, gMax = 0.0f;
            for (auto& e : ph.events)
            {
                dMin = std::min (dMin, e.depth);
                dMax = std::max (dMax, e.depth);
                gMin = std::min (gMin, e.gate);
                gMax = std::max (gMax, e.gate);
            }
            if (ph.events.size() >= 2 && dMax - dMin < 0.1f && gMax - gMin < 0.1f)
                ok = false;
        }
        check (ok, "全" + juce::String (ScratchProcessor::numTemplates) + "種で、振り幅か長さが、ブロックごとに違う");
    }

    // ------------------------------------------------------------------
    std::printf ("G5) 人間味: 0%%は正確、上げると揺れる。ただし、同じ操作なら、いつも同じ結果\n");
    {
        auto render = [] (float human)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "feel", 1.0f);
            setParam (p, "human", human);
            return run (p, blocksFor (1.0), { { 0, on (60) } });
        };
        const auto exact1 = render (0.0f), exact2 = render (0.0f);
        const auto human1 = render (0.8f), human2 = render (0.8f);
        double dSame = 0.0, dExact = 0.0;
        for (size_t i = 0; i < exact1.size(); ++i)
        {
            dSame = std::max (dSame, (double) std::abs (human1[i] - human2[i]));
            dExact = std::max (dExact, (double) std::abs (human1[i] - exact1[i]));
        }
        bool zeroSame = true;
        for (size_t i = 0; i < exact1.size(); ++i)
            zeroSame = zeroSame && exact1[i] == exact2[i];
        check (zeroSame, "0%: 何度鳴らしても、同じ");
        check (dSame == 0.0, "80%: 何度鳴らしても、同じ (書き出しのたびに、結果が変わらない)");
        check (dExact > 0.05, "80%: 0%とは違う音になる (最大差 " + juce::String (dExact, 3) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("G6) ドラッグ編集: 履歴に積まれ、戻れる。値は範囲に収まる\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (0);
        const float d0 = p.getPhrase().events[0].depth, g0 = p.getPhrase().events[0].gate;
        p.beginEdit();
        p.setEventDepth (0, 9.0f);
        p.setEventGate (0, -3.0f);
        check (std::abs (p.getPhrase().events[0].depth - 1.5f) < 1.0e-6f && std::abs (p.getPhrase().events[0].gate - 0.1f) < 1.0e-6f, "範囲外の値は、上限・下限に収まる");
        p.setEventDepth (99, 1.0f); // 存在しないブロックは、無視される
        p.back();
        check (std::abs (p.getPhrase().events[0].depth - d0) < 1.0e-6f && std::abs (p.getPhrase().events[0].gate - g0) < 1.0e-6f, "戻ると、編集前の値");
    }

    // ------------------------------------------------------------------
    std::printf ("G7) 振り幅・長さは、保存と復元で失われない\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (3);
        p.setEventDepth (1, 0.37f);
        p.setEventGate (1, 0.61f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q.getPhrase().events[1].depth - 0.37f) < 1.0e-3f && std::abs (q.getPhrase().events[1].gate - 0.61f) < 1.0e-3f, "ブロックごとの値が復元される");
    }


    // ------------------------------------------------------------------
    std::printf ("H1) 新しいパターン (トランスフォーマー〜ハイドロプレーン): 単発で鳴り、どれも違う形\n");
    {
        bool sounds = true;
        juce::String quiet;
        for (int pat = 6; pat < ScratchProcessor::numPresetPatterns; ++pat)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "mode", 1.0f);
            setParam (p, "pattern", (float) pat);
            setParam (p, "rate", 1.0f); // 1/4
            setParam (p, "feel", 1.0f);
            auto out = run (p, blocksFor (1.0), { { 0, on (60) } });
            const double v = rms (out, (size_t) (0.02 * fs), out.size());
            if (v < 0.01)
            {
                sounds = false;
                quiet << pat << " (" << juce::String (v, 4) << ") ";
            }
        }
        check (sounds, "6種すべて、ノートで鳴る " + quiet);

        // 開け閉めの割合: 刻むパターンは、閉じている時間がある
        auto openRatio = [] (int pat)
        {
            int n = 0;
            for (int i = 0; i < 1000; ++i)
                n += ScratchProcessor::patternGate (pat, (float) i / 1000.0f) ? 1 : 0;
            return (float) n / 1000.0f;
        };
        bool chopped = true;
        for (int pat : { 6, 7, 8, 9 })
            chopped = chopped && openRatio (pat) > 0.05f && openRatio (pat) < 0.9f;
        check (chopped, "トランスフォーマー/クラブ/オービット/スタブは、刻まれる (開いている割合 "
                            + juce::String (openRatio (6), 2) + " / " + juce::String (openRatio (7), 2) + " / "
                            + juce::String (openRatio (8), 2) + " / " + juce::String (openRatio (9), 2) + ")");

        // 12種が、すべて違う (動きか、開け閉めのどちらかが違う)
        bool distinct = true;
        for (int a = 0; a < ScratchProcessor::numPresetPatterns; ++a)
            for (int b = a + 1; b < ScratchProcessor::numPresetPatterns; ++b)
            {
                float dd = 0.0f;
                int gd = 0;
                for (int i = 0; i < 500; ++i)
                {
                    const float phi = (float) i / 500.0f;
                    dd = std::max (dd, std::abs (ScratchProcessor::patternD (a, phi) - ScratchProcessor::patternD (b, phi)));
                    gd += ScratchProcessor::patternGate (a, phi) != ScratchProcessor::patternGate (b, phi) ? 1 : 0;
                }
                if (dd < 0.02f && gd < 10)
                    distinct = false;
            }
        check (distinct, "12種が、すべて違う形");

        int withNew = 0;
        for (juce::uint32 seed = 1; seed <= 100; ++seed)
        {
            bool found = false;
            for (auto& e : ScratchProcessor::makeRandom (seed, 2).events)
                found = found || e.pattern >= 6;
            withNew += found ? 1 : 0;
        }
        check (withNew > 30, "ランダムでも、新しいパターンが使われる (" + juce::String (withNew) + " / 100)");
    }

    // ------------------------------------------------------------------
    std::printf ("H2) グリッドから追加: 空いているマスにブロックが入り、「戻る」で消える\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (14); // ミニマル: 0〜2 と 8〜10
        const int idx = p.addEvent (4 * T, 2 * T, 7, 3);
        const auto& ph = p.getPhrase();
        const bool ok = idx == 1 && ph.events.size() == 3 && ph.events[1].start == 4 * T && ph.events[1].len == 2 * T
                        && ph.events[1].pattern == 7 && ph.events[1].rate == 3 && ph.events[1].depth == 1.0f && ph.events[1].gate == 1.0f;
        check (ok, "4マス目から2マス、クラブ 1/16 が入る (番号 " + juce::String (idx) + ")");
        check (validPhrase (ph).isEmpty() && ph.edited, "並び順・重なり無し、編集済みになる");
        p.back();
        check (p.getPhrase().events.size() == 2, "「戻る」で、追加前に戻る");
        check (ScratchProcessor::autoRate (T) == 3 && ScratchProcessor::autoRate (2 * T) == 2 && ScratchProcessor::autoRate (8 * T) == 2
                   && ScratchProcessor::autoRate (8) == 6 && ScratchProcessor::autoRate (4) == 7,
               "速さの自動: 16分 = 1/16、8分以上 = 1/8、3連8分 = 1/8T、3連16分 = 1/16T");
    }

    // ------------------------------------------------------------------
    std::printf ("H3) 重なる所に追加すると、前後のブロックを詰める。範囲外は追加しない\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (14);                 // 0〜2, 8〜10
        p.addEvent (1 * T, 8 * T, 6, 2);     // 1〜9
        const auto& ph = p.getPhrase();
        const bool ok = ph.events.size() == 3
                        && ph.events[0].start == 0 && ph.events[0].len == 1 * T
                        && ph.events[1].start == 1 * T && ph.events[1].len == 8 * T && ph.events[1].pattern == 6
                        && ph.events[2].start == 9 * T && ph.events[2].len == 1 * T;
        check (ok && validPhrase (ph).isEmpty(), "前のブロックは手前で、後ろのブロックは後ろで切れる");

        p.addEvent (0, 16 * T, 9, 2);
        check (p.getPhrase().events.size() == 1 && p.getPhrase().events[0].len == 16 * T, "全体に追加すると、ほかのブロックは消える");
        const auto before = p.getPhrase().events.size();
        check (p.addEvent (-1, 2 * T, 0, 2) == -1 && p.addEvent (16 * T, 2 * T, 0, 2) == -1 && p.getPhrase().events.size() == before, "範囲外は、追加しない");
        p.addEvent (12 * T, 99 * T, 0, 2);
        check (validPhrase (p.getPhrase()).isEmpty() && p.getPhrase().events.back().len == 4 * T, "はみ出す長さは、フレーズの終わりまでに収まる");
    }

    // ------------------------------------------------------------------
    std::printf ("H4) 追加したブロックが、その位置で鳴る (120BPM、人間味0)\n");
    {
        ScratchProcessor p;
        prepare (p);
        setParam (p, "feel", 1.0f);
        setParam (p, "human", 0.0f);
        p.loadTemplate (14);
        p.removeEvent (1);
        p.removeEvent (0);
        check (p.getPhrase().events.empty(), "全部消すと、空のフレーズ");
        p.addEvent (4 * T, 4 * T, 0, 2); // 2拍目 = 0.5〜1.0秒
        auto out = run (p, blocksFor (1.2), { { 0, on (60) } });
        const double before = rms (out, (size_t) (0.02 * fs), (size_t) (0.45 * fs));
        const double during = rms (out, (size_t) (0.55 * fs), (size_t) (0.95 * fs));
        check (before < 0.005, "ブロックの前は無音 (rms " + juce::String (before, 5) + ")");
        check (during > 0.05, "ブロックの所で鳴る (rms " + juce::String (during, 3) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("H5) 右クリックのメニュー: パターン・速さ・高さ・リセット・削除。範囲に収まり、保存される\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (0); // ベイビー基本: 2ブロック
        p.setEventPattern (0, 99);
        p.setEventRate (0, -5);
        p.setEventPitch (0, 40);
        const auto& e = p.getPhrase().events[0];
        check (e.pattern == ScratchProcessor::numPatterns - 1 && e.rate == 0 && e.pitch == 12, "範囲外の値は、上限・下限に収まる");
        p.setEventDepth (1, 0.4f);
        p.setEventGate (1, 0.3f);
        p.resetEventShape (1);
        check (p.getPhrase().events[1].depth == 1.0f && p.getPhrase().events[1].gate == 1.0f, "振り幅・長さのリセット");
        p.removeEvent (99); // 存在しないブロックは、無視
        check (p.getPhrase().events.size() == 2, "存在しないブロックの削除は、何もしない");

        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getPhrase().events[0].pattern == ScratchProcessor::numPatterns - 1 && q.getPhrase().events[0].pitch == 12, "新しいパターン(ハイドロプレーン)も、保存と復元で失われない");

        int backs = 0;
        while (backs < 4 && p.back())
            ++backs;
        check (backs == 4 && p.getPhrase().events[0].pattern == 0, "どの操作も、ひとつずつ「戻る」で取り消せる (" + juce::String (backs) + " 回)");
    }


    // ------------------------------------------------------------------
    std::printf ("I1) 右端のドラッグ: ブロック自体が縮み、後ろのマスが空く。空きマスへは伸ばせる\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (0); // ベイビー基本: 0〜8 と 10〜
        p.beginEdit();
        p.setEventEnd (0, 3.0f * T);
        auto e = p.getPhrase().events[0];
        check (e.len == 3 * T && e.gate == 1.0f, "3マスにすると、ブロックも3マス (長さ " + juce::String (e.len) + ", 鳴らす " + juce::String (e.gate, 2) + ")");
        check (p.addEvent (4 * T, 2 * T, 9, 3) >= 0 && validPhrase (p.getPhrase()).isEmpty(), "空いたマスに、別のブロックを追加できる");
        p.back();
        p.setEventEnd (0, 2.5f * T);
        e = p.getPhrase().events[0];
        check (e.len == 15 && e.gate == 1.0f, "2.5マスで止めると、ブロックも2.5マス (15ティック)");
        p.setEventEnd (0, 14.5f);
        e = p.getPhrase().events[0];
        check (e.len == 15 && std::abs (e.gate - 14.5f / 15.0f) < 1.0e-4f, "ティックの途中で止めると、そこまで鳴らす");
        p.setEventEnd (0, 99.0f * T);
        e = p.getPhrase().events[0];
        check (e.len == 10 * T && e.gate == 1.0f, "伸ばすのは、次のブロックの手前まで (長さ " + juce::String (e.len) + ")");
        p.setEventEnd (0, 0.0f);
        e = p.getPhrase().events[0];
        check (e.len == 2 && std::abs (e.gate - 0.75f) < 1.0e-4f, "一番短くて、1.5ティック (1/4マス)");
    }

    // ------------------------------------------------------------------
    std::printf ("I2) テンプレート・ランダム・バリエーションに、1マス以上のゴースト(鳴らない部分)が残らない\n");
    {
        auto maxGhost = [] (const ScratchProcessor::Phrase& ph)
        {
            float g = 0.0f;
            for (auto& e : ph.events)
                g = std::max (g, (float) e.len * (1.0f - e.gate));
            return g;
        };
        float worst = 0.0f;
        for (int t = 0; t < ScratchProcessor::numTemplates; ++t)
            for (int bars : { 1, 2, 4 })
                worst = std::max (worst, maxGhost (ScratchProcessor::makeTemplate (t, bars)));
        for (juce::uint32 seed = 1; seed <= 200; ++seed)
        {
            const auto ph = ScratchProcessor::makeRandom (seed, 2);
            worst = std::max (worst, maxGhost (ph));
            worst = std::max (worst, maxGhost (ScratchProcessor::makeVariation (ph, seed + 7)));
        }
        check (worst < (float) T, "一番大きいゴーストでも 1マス未満 (" + juce::String (worst / (float) T, 2) + " マス)");
    }

    // ------------------------------------------------------------------
    std::printf ("I3) 縮めても、鳴り方は同じ (「鳴らす長さ」で切ったときと比べる)\n");
    {
        auto render = [] (bool shrink)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "feel", 1.0f);
            setParam (p, "human", 0.0f);
            p.loadTemplate (0);
            if (shrink)
                p.setEventEnd (0, 4.0f * T); // ブロックを4マスに縮める
            else
                p.setEventGate (0, 0.5f);  // 8マスのまま、半分で切る
            return run (p, blocksFor (1.2), { { 0, on (60) } });
        };
        const auto a = render (false), b = render (true);
        double d = 0.0;
        for (size_t i = 0; i < (size_t) (0.49 * fs); ++i)
            d = std::max (d, (double) std::abs (a[i] - b[i]));
        check (d < 1.0e-6, "鳴っている間は、同じ音 (最大差 " + juce::String (d, 8) + ")");
        check (rms (b, (size_t) (0.56 * fs), (size_t) (1.2 * fs)) < 0.005, "空いたマスは、無音");
    }


    // ------------------------------------------------------------------
    std::printf ("J1) 左端のドラッグ: 鳴り終わりはそのままで、頭が動く。前の空きマスへも伸ばせる\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (0); // ベイビー基本: 0〜8 (7.2マスまで鳴らす) と 10〜15 (14.5まで)
        auto audEnd = [&p] (int i) { const auto& e = p.getPhrase().events[(size_t) i]; return (float) e.start + (float) e.len * e.gate; };
        const float end0 = audEnd (0), end1 = audEnd (1);
        p.beginEdit();
        p.setEventStart (0, 3 * T);
        const auto& a = p.getPhrase().events[0];
        check (a.start == 3 * T && a.len == 26 && std::abs (audEnd (0) - end0) < 1.0e-4f,
               "頭を3マス目へ: 3〜8、鳴り終わりは " + juce::String (audEnd (0), 2) + " のまま");
        p.setEventStart (1, 0);
        const auto& b = p.getPhrase().events[1];
        check (b.start == 44 && std::abs (audEnd (1) - end1) < 1.0e-4f && validPhrase (p.getPhrase()).isEmpty(),
               "前へ伸ばすのは、前のブロックの終わりまで (頭 " + juce::String (b.start) + ")");
        p.setEventStart (0, 99 * T);
        check (p.getPhrase().events[0].start == 41 && audEnd (0) - 41.0f >= 1.5f, "一番短くても 1.5ティックは残る (頭 " + juce::String (p.getPhrase().events[0].start) + ")");
        p.back();
        check (p.getPhrase().events[0].start == 0, "「戻る」で元に戻る");
    }

    // ------------------------------------------------------------------
    std::printf ("J2) ブロックの移動: 重なる位置には置かない。空いていれば、ほかのブロックを飛び越せる\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (14); // ミニマル: 0〜2 と 8〜10
        int i = p.moveEvent (0, 4 * T);
        check (i == 0 && p.getPhrase().events[0].start == 4 * T, "4マス目へ移動");
        i = p.moveEvent (0, 7 * T);
        check (i == 0 && p.getPhrase().events[0].start == 4 * T, "次のブロックと重なる位置 (7〜9) には動かない");
        i = p.moveEvent (0, 12 * T);
        const auto& ph = p.getPhrase();
        check (i == 1 && ph.events[0].start == 8 * T && ph.events[1].start == 12 * T && validPhrase (ph).isEmpty(),
               "飛び越して 12マス目へ。並び順も入れ替わる (番号 " + juce::String (i) + ")");
        check (! p.canPlace (1, 15 * T) && ! p.canPlace (1, -1) && p.canPlace (1, 14 * T), "フレーズの外には置けない");
        check (ph.events[1].pattern == 3 && ph.events[1].len == 2 * T && ph.edited, "パターン・長さはそのまま、編集済みになる");
    }

    // ------------------------------------------------------------------
    std::printf ("J3) 移動したブロックが、新しい位置で鳴る (120BPM、人間味0)\n");
    {
        ScratchProcessor p;
        prepare (p);
        setParam (p, "feel", 1.0f);
        setParam (p, "human", 0.0f);
        p.loadTemplate (14);
        p.removeEvent (1);
        p.setEventPattern (0, 0); // ベイビー
        p.setEventEnd (0, 2.0f * T);
        p.moveEvent (0, 4 * T);   // 2拍目 = 0.5秒〜0.75秒
        auto out = run (p, blocksFor (0.9), { { 0, on (60) } });
        const double before = rms (out, (size_t) (0.02 * fs), (size_t) (0.45 * fs));
        const double during = rms (out, (size_t) (0.52 * fs), (size_t) (0.72 * fs));
        check (before < 0.005, "元の位置は無音 (rms " + juce::String (before, 5) + ")");
        check (during > 0.05, "新しい位置で鳴る (rms " + juce::String (during, 3) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("J4) ジャンル別テンプレート: 14種。どれも1小節を埋め、ブロックごとに強弱がある\n");
    {
        const int genres = ScratchProcessor::numTemplates - ScratchProcessor::firstGenreTemplate;
        bool ok = true;
        for (int t = ScratchProcessor::firstGenreTemplate; t < ScratchProcessor::numTemplates; ++t)
        {
            const auto ph = ScratchProcessor::makeTemplate (t, 1);
            float dMin = 9.0f, dMax = 0.0f;
            for (auto& e : ph.events)
            {
                dMin = std::min (dMin, e.depth);
                dMax = std::max (dMax, e.depth);
            }
            if (ph.events.size() < 4 || dMax - dMin < 0.25f || ph.events.back().start < 12 * T)
                ok = false;
        }
        check (genres == 14 && ok, "14種すべて OK");
    }


    // ------------------------------------------------------------------
    std::printf ("K1) 3連符: 速さ 1/4T〜1/32T と、3連のマス目に置いたブロック (120BPM)\n");
    {
        check (std::abs (ScratchProcessor::rateBeats (6) - 1.0 / 3.0) < 1.0e-9 && std::abs (ScratchProcessor::rateBeats (8) - 1.0 / 12.0) < 1.0e-9,
               "1/8T = 1/3拍、1/32T = 1/12拍");
        ScratchProcessor p;
        prepare (p);
        setParam (p, "feel", 1.0f);
        setParam (p, "human", 0.0f);
        p.loadTemplate (14);
        p.removeEvent (1);
        p.removeEvent (0);
        // 3連8分の2つ目 = 8〜16ティック = 1/3拍〜2/3拍 = 0.167〜0.333秒
        const int idx = p.addEvent (8, 8, 0, ScratchProcessor::autoRate (8));
        check (idx == 0 && p.getPhrase().events[0].rate == 6, "3連8分のマスに追加すると、速さは 1/8T");
        auto out = run (p, blocksFor (0.5), { { 0, on (60) } });
        const double before = rms (out, (size_t) (0.01 * fs), (size_t) (0.15 * fs));
        const double during = rms (out, (size_t) (0.19 * fs), (size_t) (0.31 * fs));
        const double after = rms (out, (size_t) (0.36 * fs), (size_t) (0.48 * fs));
        check (before < 0.005 && after < 0.005 && during > 0.05,
               "3連のマスの位置でだけ鳴る (前 " + juce::String (before, 4) + " / 中 " + juce::String (during, 3) + " / 後 " + juce::String (after, 4) + ")");

        const auto trap = ScratchProcessor::makeTemplate (ScratchProcessor::firstGenreTemplate + 9, 1);
        bool triplet = false;
        for (auto& e : trap.events)
            triplet = triplet || e.start % T != 0 || e.rate >= 5;
        check (triplet && validPhrase (trap).isEmpty(), "テンプレート「トラップ (3連)」は、3連符を使う");

        // 単発でも 3連の速さ: 1/8T の 1往復 = 1/6秒。チャープは1往復に1回鳴るので、1秒で6回
        ScratchProcessor q;
        prepare (q);
        setParam (q, "mode", 1.0f);
        setParam (q, "pattern", 3.0f);
        setParam (q, "rate", 6.0f);
        setParam (q, "feel", 1.0f);
        auto o2 = run (q, blocksFor (1.0), { { 0, on (60) } });
        int bursts = 0;
        bool loud = false;
        const size_t win = (size_t) (0.008 * fs);
        for (size_t i = 0; i + win < o2.size(); i += win)
        {
            const bool l = rms (o2, i, i + win) > 0.03;
            if (l && ! loud)
                ++bursts;
            loud = l;
        }
        check (bursts == 6, "単発 1/8T のチャープ: 1秒で6回 (" + juce::String (bursts) + " 回)");
    }

    // ------------------------------------------------------------------
    std::printf ("K2) ミュート: ブロックの一部だけ消せる。レコードの動きは変わらない。保存・移動でも保たれる\n");
    {
        auto render = [] (std::function<void (ScratchProcessor&)> setup)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "feel", 1.0f);
            setParam (p, "human", 0.0f);
            p.loadTemplate (0); // ベイビー基本: 0〜48ティック (0〜1秒) に 1/8 のベイビー (1往復 0.25秒)
            if (setup)
                setup (p);
            return run (p, blocksFor (0.9), { { 0, on (60) } });
        };
        auto muteRange = [] (ScratchProcessor& p, int a, int b)
        {
            auto m = p.getPhrase().events[0].mute;
            for (int i = a; i < b; ++i)
                m.set ((size_t) i);
            p.beginEdit();
            p.setEventMuteMask (0, m);
        };
        const auto plain = render (nullptr);
        const auto cut = render ([&] (ScratchProcessor& p) { muteRange (p, 12, 24); }); // 2往復目 (0.25〜0.5秒) を消す
        const double mid = rms (cut, (size_t) (0.27 * fs), (size_t) (0.48 * fs));
        double same = 0.0;
        for (size_t i = 0; i < (size_t) (0.24 * fs); ++i)
            same = std::max (same, (double) std::abs (plain[i] - cut[i]));
        for (size_t i = (size_t) (0.52 * fs); i < (size_t) (0.85 * fs); ++i)
            same = std::max (same, (double) std::abs (plain[i] - cut[i]));
        check (mid < 0.005, "消した所は無音 (rms " + juce::String (mid, 5) + ")");
        check (same < 1.0e-4, "ほかの所は、消す前と同じ音 (最大差 " + juce::String (same, 8) + ")");

        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (14); // ミニマル: 0〜12, 48〜60
        muteRange (p, 3, 6);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto& qm = q.getPhrase().events[0].mute;
        check (qm.count() == 3 && qm[3] && qm[5] && ! qm[6], "保存と復元で失われない");
        p.moveEvent (0, 24);
        check (p.getPhrase().events[0].mute[3] && p.getPhrase().events[0].mute.count() == 3, "移動しても、ブロックと一緒に動く");
        p.setEventStart (0, 18);
        check (p.getPhrase().events[0].mute[9] && p.getPhrase().events[0].mute.count() == 3, "左端を伸ばしても、タイムライン上の位置は同じ");
        p.clearEventMutes (0);
        check (p.getPhrase().events[0].mute.none(), "全部戻す");
        p.back();
        check (p.getPhrase().events[0].mute.count() == 3, "「戻る」で取り消せる");
    }

    // ------------------------------------------------------------------
    std::printf ("K3) ユーザー波形: プリセットを写すと同じ形。点を動かすと形が変わる。保存され、戻せる\n");
    {
        ScratchProcessor p;
        prepare (p);
        const int U = ScratchProcessor::numPresetPatterns;
        float worst = 0.0f;
        int gateDiff = 0;
        for (int preset : { 0, 1, 2, 3, 4, 9 })
        {
            p.copyPresetToUser (1, preset);
            for (int i = 0; i < 1000; ++i)
            {
                const float phi = (float) i / 1000.0f;
                worst = std::max (worst, std::abs (p.shapeD (U + 1, phi) - ScratchProcessor::patternD (preset, phi)));
                gateDiff += p.shapeGate (U + 1, phi) != ScratchProcessor::patternGate (preset, phi) ? 1 : 0;
            }
        }
        check (worst < 0.04f, "写した形は、元とほぼ同じ (最大差 " + juce::String (worst, 3) + ")");
        check (gateDiff < 6 * 1000 / 50, "フェーダーも、ほぼ同じ (" + juce::String (gateDiff) + " / 6000)");

        // 形のつながり: 0 から始まって 0 に戻る。途中に飛びが無い
        bool cont = true;
        for (int u = 0; u < ScratchProcessor::numUserPatterns; ++u)
        {
            float prev = p.shapeD (U + u, 0.0f);
            cont = cont && std::abs (prev) < 1.0e-4f && std::abs (p.shapeD (U + u, 0.99999f)) < 0.02f;
            for (int i = 1; i <= 1000; ++i)
            {
                const float d = p.shapeD (U + u, (float) i / 1000.0f);
                if (std::abs (d - prev) > 0.08f || d < 0.0f || d > 1.0f)
                    cont = false;
                prev = d;
            }
        }
        check (cont, "4つとも、0 から始まり 0 に戻る。範囲内で、飛びが無い");

        // 編集: 真ん中に点を足して高くすると、そこが上がる。戻ると元どおり
        p.copyPresetToUser (0, 1); // フォワード
        const float before = p.shapeD (U, 0.6f);
        p.beginUserEdit();
        float nx = 0.7f; // 0.7 の両隣の点の、真ん中に足す
        {
            const auto& ps = p.getUserPattern (0).points;
            for (size_t i = 0; i + 1 < ps.size(); ++i)
                if (ps[i].x < 0.7f && ps[i + 1].x >= 0.7f)
                    nx = 0.5f * (ps[i].x + ps[i + 1].x);
        }
        const int idx = p.addUserPoint (0, { nx, 0.95f });
        check (idx > 0 && std::abs (p.shapeD (U, nx) - 0.95f) < 0.01f, "点を足すと、その高さを通る (" + juce::String (p.shapeD (U, nx), 3) + ")");
        p.setUserPoint (0, idx, { 2.0f, -1.0f }); // 範囲外は、隣の点の間・0〜1 に収まる
        const auto& pts = p.getUserPattern (0).points;
        check (pts[(size_t) idx].x < pts[(size_t) idx + 1].x && pts[(size_t) idx].y == 0.0f, "点は、隣の点を越えない");
        p.setUserPoint (0, 0, { 0.5f, 0.5f }); // 両端は動かない
        check (pts.front().x == 0.0f && pts.front().y == 0.0f, "両端は動かせない");
        p.backUser();
        check (std::abs (p.shapeD (U, 0.6f) - before) < 1.0e-5f, "「戻る」で元の形");

        // フェーダー: 全部閉じると無音、開くと鳴る (単発、ユーザー1)
        auto play = [] (ScratchProcessor& s)
        {
            return run (s, blocksFor (0.5), { { 0, on (60) } });
        };
        ScratchProcessor q;
        prepare (q);
        setParam (q, "mode", 1.0f);
        setParam (q, "pattern", (float) U);
        setParam (q, "feel", 1.0f);
        const double openRms = rms (play (q), (size_t) (0.02 * fs), (size_t) (0.5 * fs));
        q.beginUserEdit();
        for (int k = 0; k < ScratchProcessor::userGateSegments; ++k)
            q.setUserGate (0, k, false);
        const double closedRms = rms (play (q), (size_t) (0.02 * fs), (size_t) (0.5 * fs));
        check (openRms > 0.05 && closedRms < 0.005, "フェーダーを全部閉じると無音 (開 " + juce::String (openRms, 3) + " / 閉 " + juce::String (closedRms, 5) + ")");

        // 保存と復元
        q.setUserGate (0, 10, true);
        q.addUserPoint (0, { 0.31f, 0.42f });
        juce::MemoryBlock mb;
        q.getStateInformation (mb);
        ScratchProcessor r;
        r.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = r.getUserPattern (0).points.size() == q.getUserPattern (0).points.size() && r.getUserPattern (0).gate == q.getUserPattern (0).gate;
        for (int i = 0; i < 100 && same; ++i)
            same = std::abs (r.shapeD (U, (float) i / 100.0f) - q.shapeD (U, (float) i / 100.0f)) < 1.0e-3f;
        check (same, "保存と復元で失われない");
    }

    // ------------------------------------------------------------------
    std::printf ("K4) 波形エディタの試聴: フィルのモードのままでも、選んだユーザー波形を単発で鳴らす\n");
    {
        ScratchProcessor p;
        prepare (p);
        p.loadTemplate (14);
        p.previewPattern.store (ScratchProcessor::numPresetPatterns + 2);
        p.previewOn.store (true);
        auto out = run (p, blocksFor (0.6));
        check (rms (out, (size_t) (0.02 * fs), out.size()) > 0.02 && p.uiFillBeat.load() < 0.0f, "鳴る (フィルの再生位置は出ない)");
    }

    // ------------------------------------------------------------------
    std::printf ("K5) 古い保存データ (16分音符単位) も、正しい位置で読める\n");
    {
        ScratchProcessor p;
        juce::ValueTree state ("STATE");
        juce::ValueTree ph ("PHRASE");
        ph.setProperty ("bars", 1, nullptr);
        ph.setProperty ("edited", true, nullptr);
        juce::ValueTree ev ("EV");
        ev.setProperty ("s", 4, nullptr);
        ev.setProperty ("l", 2, nullptr);
        ev.setProperty ("p", 3, nullptr);
        ph.appendChild (ev, nullptr);
        state.appendChild (ph, nullptr);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), mb);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        check (p.getPhrase().events.size() == 1 && p.getPhrase().events[0].start == 4 * T && p.getPhrase().events[0].len == 2 * T,
               "4マス目から2マス = 24ティックから12ティック");
    }


    // ------------------------------------------------------------------
    std::printf ("L1) ブレーキ・ザップ: 鳴っている間、速さ(=音の高さ)は下がる一方。出だしが一番速い\n");
    {
        for (int pat : { 12, 13 })
        {
            float prevV = 1.0e9f, firstV = -1.0f;
            bool falling = true;
            int openSteps = 0;
            for (int i = 0; i < 1000; ++i)
            {
                const float phi = (float) i / 1000.0f;
                if (! ScratchProcessor::patternGate (pat, phi))
                    continue;
                ++openSteps;
                const float v = ScratchProcessor::patternD (pat, phi + 0.001f) - ScratchProcessor::patternD (pat, phi);
                if (firstV < 0.0f)
                    firstV = v;
                if (v > prevV + 1.0e-6f || v <= 0.0f)
                    falling = false;
                prevV = v;
            }
            check (falling && openSteps > 300 && firstV > prevV * 1.8f,
                   juce::String (pat == 12 ? "ブレーキ" : "ザップ") + ": 下がる一方 (出だし/終わりの速さ " + juce::String (firstV / prevV, 2) + " 倍)");
        }
    }

    // ------------------------------------------------------------------
    std::printf ("L2) 古い保存データのユーザー波形 (12〜15番) は、新しい番号 (14〜17番) に読み替える\n");
    {
        ScratchProcessor p;
        juce::ValueTree state ("STATE");
        juce::ValueTree prm ("PARAM");
        prm.setProperty ("id", "pattern", nullptr);
        prm.setProperty ("value", 13.0f, nullptr);
        state.appendChild (prm, nullptr);
        juce::ValueTree ph ("PHRASE");
        ph.setProperty ("bars", 1, nullptr);
        ph.setProperty ("tpb", 96, nullptr);
        for (int pat : { 3, 12, 15 })
        {
            juce::ValueTree ev ("EV");
            ev.setProperty ("s", pat * 6, nullptr);
            ev.setProperty ("l", 6, nullptr);
            ev.setProperty ("p", pat, nullptr);
            ph.appendChild (ev, nullptr);
        }
        state.appendChild (ph, nullptr);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), mb);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto& ev = p.getPhrase().events;
        check (ev.size() == 3 && ev[0].pattern == 3 && ev[1].pattern == 14 && ev[2].pattern == 17, "フレーズ: 3 → 3、12 → 14、15 → 17");
        check ((int) p.apvts.getRawParameterValue ("pattern")->load() == 15, "単発のパターン: 13 → 15");

        // 新しい形式で保存したものは、読み替えない
        juce::MemoryBlock mb2;
        p.getStateInformation (mb2);
        ScratchProcessor q;
        q.setStateInformation (mb2.getData(), (int) mb2.getSize());
        check (q.getPhrase().events[1].pattern == 14 && (int) q.apvts.getRawParameterValue ("pattern")->load() == 15, "新しい保存データは、そのまま");
    }


    // ------------------------------------------------------------------
    std::printf ("M1) ノイズの音源: 速く回すほど、音が高くなる (ホワイトノイズだと変わらなかった)\n");
    {
        // ゼロ交差の数で、音の高さ(明るさ)を比べる。1/4 のベイビーで、振り幅だけを変える (=回す速さが変わる)
        auto brightness = [] (float depthMs)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "src", (float) ScratchProcessor::srcNoise);
            setParam (p, "mode", 1.0f);
            setParam (p, "pattern", 0.0f);
            setParam (p, "rate", 1.0f);
            setParam (p, "depth", depthMs);
            setParam (p, "feel", 1.0f);
            auto out = run (p, blocksFor (0.5), { { 0, on (60) } });
            // 一番速いところ (位相 0.25 = 0.125秒) の前後 20ms
            const size_t a = (size_t) (0.105 * fs), b = (size_t) (0.145 * fs);
            return (double) zeroCrossings (out, a, b) / ((double) (b - a) / fs) * 0.5; // おおよその中心周波数 (Hz)
        };
        const double slow = brightness (60.0f), fast = brightness (240.0f);
        check (fast > slow * 2.5, "4倍速く回すと、中心の高さも上がる (" + juce::String (slow, 0) + " Hz → " + juce::String (fast, 0) + " Hz)");
    }


    // ------------------------------------------------------------------
    std::printf ("N1) ベイビー: 押す・引くは別々の動き。合間はレコードが止まる (音が途切れる)\n");
    {
        // 1往復の中で、速さが 0 の区間が2つ (押しの後、引きの後) あり、押しは引きより速い
        float maxPush = 0.0f, maxPull = 0.0f;
        int stillAfterPush = 0, stillAfterPull = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const float phi = (float) i / 1000.0f;
            const float v = (ScratchProcessor::patternD (0, phi + 0.001f) - ScratchProcessor::patternD (0, phi)) * 1000.0f;
            if (phi < 0.5f)
            {
                maxPush = std::max (maxPush, v);
                stillAfterPush += (phi > 0.25f && std::abs (v) < 1.0e-3f) ? 1 : 0;
            }
            else
            {
                maxPull = std::max (maxPull, -v);
                stillAfterPull += (phi > 0.92f && std::abs (v) < 1.0e-3f) ? 1 : 0;
            }
        }
        check (stillAfterPush > 200 && stillAfterPull > 60, "押しの後と引きの後に、止まる区間がある (" + juce::String (stillAfterPush) + " / " + juce::String (stillAfterPull) + ")");
        check (maxPush > maxPull * 1.5f, "押しは引きより速い (" + juce::String (maxPush / maxPull, 2) + " 倍)");

        // 音: 1/8 のベイビー (120BPM、1往復 0.25秒)。押しの後 (0.06〜0.12秒) は無音
        ScratchProcessor p;
        prepare (p);
        setParam (p, "mode", 1.0f);
        setParam (p, "pattern", 0.0f);
        setParam (p, "rate", 2.0f);
        setParam (p, "feel", 1.0f);
        auto out = run (p, blocksFor (0.3), { { 0, on (60) } });
        const double push = rms (out, (size_t) (0.01 * fs), (size_t) (0.05 * fs));
        const double hold = rms (out, (size_t) (0.065 * fs), (size_t) (0.12 * fs));
        const double pull = rms (out, (size_t) (0.14 * fs), (size_t) (0.21 * fs));
        check (push > 0.05 && pull > 0.05 && hold < 0.005,
               "押し・止まり・引き (rms " + juce::String (push, 3) + " / " + juce::String (hold, 5) + " / " + juce::String (pull, 3) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("V1) レコード感: 動いている間だけ、擦れとパチパチが足される。止まっていれば無音。いつも同じ音\n");
    {
        auto render = [] (float vinyl)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "vinyl", vinyl);
            setParam (p, "mode", 1.0f);
            setParam (p, "pattern", 0.0f);
            setParam (p, "rate", 2.0f);
            setParam (p, "feel", 1.0f);
            return run (p, blocksFor (0.5), { { 0, on (60) }, { blocksFor (0.3), off (60) } });
        };
        const auto dry = render (0.0f), wet = render (1.0f), wet2 = render (1.0f);
        double diff = 0.0, same = 0.0;
        for (size_t i = 0; i < dry.size(); ++i)
        {
            diff = std::max (diff, (double) std::abs (wet[i] - dry[i]));
            same = std::max (same, (double) std::abs (wet[i] - wet2[i]));
        }
        check (diff > 0.01, "音が変わる (最大差 " + juce::String (diff, 3) + ")");
        check (same == 0.0, "何度鳴らしても、同じ音 (書き出しが安定)");
        check (rms (wet, (size_t) (0.07 * fs), (size_t) (0.12 * fs)) < 0.005, "レコードが止まっている間は、無音");
        check (peak (wet, (size_t) (0.42 * fs), wet.size()) < 1.0e-3, "ノートを離した後は、無音");
        bool finite = true;
        for (float v : wet)
            finite = finite && std::isfinite (v) && std::abs (v) < 1.2f;
        check (finite, "有限値で、範囲内");
    }


    // ------------------------------------------------------------------
    std::printf ("N2) テンプレート・ランダム・バリエーションに、スクリブルと 32分のベイビーは出てこない\n");
    {
        int bad = 0;
        auto scan = [&bad] (const ScratchProcessor::Phrase& ph)
        {
            for (auto& e : ph.events)
                if (e.pattern == 5 || (e.pattern == 0 && e.rate == 4) || (e.pattern == 0 && e.rate == 8))
                    ++bad;
        };
        for (int t = 0; t < ScratchProcessor::numTemplates; ++t)
            for (int bars : { 1, 2, 4 })
                scan (ScratchProcessor::makeTemplate (t, bars));
        for (juce::uint32 seed = 1; seed <= 300; ++seed)
        {
            const auto ph = ScratchProcessor::makeRandom (seed, 2);
            scan (ph);
            scan (ScratchProcessor::makeVariation (ph, seed * 3));
        }
        check (bad == 0, "見つかった数 " + juce::String (bad));
    }


    // ------------------------------------------------------------------
    std::printf ("X1) カット: 遅い所だけフェーダーで切る。速い所は、カット無しと同じ音\n");
    {
        auto render = [] (float cut)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "mode", 1.0f);
            setParam (p, "pattern", 0.0f);
            setParam (p, "rate", 2.0f);
            setParam (p, "feel", 1.0f);
            setParam (p, "cut", cut);
            return run (p, blocksFor (0.25), { { 0, on (60) } });
        };
        const auto a = render (0.0f), b = render (0.7f);
        // 押しの一番速い所 (0.028秒) の前後は同じ、押しの出だし (0.004〜0.009秒) は切れる
        double fastDiff = 0.0;
        for (size_t i = (size_t) (0.028 * fs); i < (size_t) (0.036 * fs); ++i)
            fastDiff = std::max (fastDiff, (double) std::abs (a[i] - b[i]));
        const double slowA = rms (a, (size_t) (0.004 * fs), (size_t) (0.009 * fs));
        const double slowB = rms (b, (size_t) (0.004 * fs), (size_t) (0.009 * fs));
        check (fastDiff < 0.002, "速い所はほぼ同じ音 (フェーダーが開き切った後。最大差 " + juce::String (fastDiff, 5) + ")");
        check (slowA > 0.01 && slowB < slowA * 0.1, "遅い所は切れる (rms " + juce::String (slowA, 4) + " → " + juce::String (slowB, 5) + ")");
        check (rms (b, 0, b.size()) > 0.05, "全体としては鳴っている");
    }


    // ------------------------------------------------------------------
    std::printf ("P1) プリセットサンプル: 全部読み込めて、短く、保存・復元で戻る。初めの音源は声のプリセット\n");
    {
        ScratchProcessor p;
        check ((int) p.apvts.getRawParameterValue ("src")->load() == ScratchProcessor::srcSample && p.getPresetIndex() == 0,
               "初めは、サンプル (プリセット 0 番 = 声)");
        bool ok = true;
        juce::String bad;
        for (int i = 0; i < ScratchProcessor::getNumPresetSamples(); ++i)
        {
            const bool loaded = p.loadPresetSample (i);
            const double sec = p.getSampleSeconds();
            if (! loaded || sec < 0.2 || sec > 1.6 || p.getPresetIndex() != i)
            {
                ok = false;
                bad << i << " ";
            }
        }
        check (ok && ScratchProcessor::getNumPresetSamples() == 27, juce::String (ScratchProcessor::getNumPresetSamples()) + " 本すべて読み込める " + bad);
        p.loadPresetSample (14);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getPresetIndex() == 14, "保存・復元で、同じプリセットに戻る (" + juce::String (q.getPresetIndex()) + ")");

        // 鳴る
        ScratchProcessor r;
        prepare (r);
        setParam (r, "src", (float) ScratchProcessor::srcSample);
        auto out = run (r, blocksFor (0.5), { { 0, on (60) } });
        check (rms (out, 0, out.size()) > 0.02, "プリセットの声で鳴る");
    }

    // ------------------------------------------------------------------
    std::printf ("P2) テンプレートの ◀▶: 編集・ランダムの後でも、今のテンプレートから前後へ\n");
    {
        ScratchProcessor p;
        p.loadTemplate (5);
        p.beginEdit();
        p.setEventDepth (0, 0.4f);   // ブロックを編集
        p.stepTemplate (1);
        check (p.getPhrase().templateIndex == 6 && ! p.getPhrase().edited, "編集した後の ▶: 5 → 6 (" + juce::String (p.getPhrase().templateIndex) + ")");
        p.randomizeWithSeed (99);
        p.stepTemplate (-1);
        check (p.getPhrase().templateIndex == 5, "ランダムの後の ◀: 6 → 5 (" + juce::String (p.getPhrase().templateIndex) + ")");
        p.setBars (2);
        p.addEvent (0, 6, 3, 3);
        p.setBars (4);
        p.stepTemplate (1);
        check (p.getPhrase().templateIndex == 6 && p.getPhrase().bars == 4, "小節数を変えても、位置と小節数は保つ");
        p.setMyPatternDir (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ScratchNoPatternsHere"));
        p.loadTemplate (ScratchProcessor::numTemplates - 1);
        p.stepTemplate (1);
        check (p.getPhrase().templateIndex == ScratchProcessor::emptyTemplate, "最後の次は、先頭の「空」へ");
        p.stepTemplate (1 + ScratchProcessor::numFactoryPatterns);
        check (p.getPhrase().templateIndex == 0, "Sound Artz の次は、最初のテンプレートへ");
    }


    // ------------------------------------------------------------------
    std::printf ("S1) カーブの式: 端は 0 と 1。片側の 0 は直線、S字は真ん中で 0.5。どれも増える一方\n");
    {
        using SP = ScratchProcessor;
        bool ends = true, mono = true;
        for (int type : { (int) SP::curveSingle, (int) SP::curveS })
            for (float t : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            {
                ends = ends && std::abs (SP::curveValue (type, t, 0.0f)) < 1.0e-6f && std::abs (SP::curveValue (type, t, 1.0f) - 1.0f) < 1.0e-6f;
                float prev = -1.0f;
                for (int i = 0; i <= 100; ++i)
                {
                    const float v = SP::curveValue (type, t, (float) i / 100.0f);
                    mono = mono && v >= prev - 1.0e-6f;
                    prev = v;
                }
            }
        check (ends && mono, "端と、増える一方");
        check (std::abs (SP::curveValue (SP::curveSingle, 0.0f, 0.3f) - 0.3f) < 1.0e-5f, "片側の曲がり具合 0 = 直線");
        check (std::abs (SP::curveValue (SP::curveS, 0.7f, 0.5f) - 0.5f) < 1.0e-5f, "S字の真ん中 = 0.5");
        check (SP::curveValue (SP::curveSingle, -0.8f, 0.5f) > 0.6f && SP::curveValue (SP::curveSingle, 0.8f, 0.5f) < 0.4f, "片側: マイナスで早く、プラスで遅れて動く");
    }

    // ------------------------------------------------------------------
    std::printf ("S2) 決まった形のパターンを点とカーブにすると、元の形とほぼ同じ (14種)\n");
    {
        ScratchProcessor p;
        float worst = 0.0f;
        int worstP = -1, gateDiff = 0, maxPts = 0;
        for (int pat = 0; pat < ScratchProcessor::numPresetPatterns; ++pat)
        {
            const auto& sh = p.getShape (pat); // 編集していないので、数式から作った形
            maxPts = std::max (maxPts, (int) sh.points.size());
            for (int i = 0; i < 1000; ++i)
            {
                const float phi = (float) i / 1000.0f;
                const float tv = sh.table[(size_t) std::min (ScratchProcessor::userTableSize, (int) (phi * ScratchProcessor::userTableSize))];
                const float e = std::abs (tv - ScratchProcessor::patternD (pat, (float) ((int) (phi * ScratchProcessor::userTableSize)) / ScratchProcessor::userTableSize));
                if (e > worst)
                {
                    worst = e;
                    worstP = pat;
                }
            }
            for (int k = 0; k < ScratchProcessor::userGateSegments; ++k)
                gateDiff += sh.gate[(size_t) k] != ScratchProcessor::patternGate (pat, ((float) k + 0.5f) / ScratchProcessor::userGateSegments) ? 1 : 0;
        }
        check (worst < 0.05f, "形の差 最大 " + juce::String (worst, 3) + " (パターン " + juce::String (worstP) + ")");
        check (gateDiff == 0 && maxPts <= ScratchProcessor::maxUserPoints, "フェーダーは同じ、点の数は " + juce::String (maxPts) + " 以下");
        check (! p.isShapeEdited (9), "見ただけでは、編集済みにならない");
    }

    // ------------------------------------------------------------------
    std::printf ("S3) 決まった形のパターン (スタブ) を編集すると音が変わり、「初期に戻す」で元の音に戻る\n");
    {
        auto render = [] (std::function<void (ScratchProcessor&)> edit)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "mode", 1.0f);
            setParam (p, "pattern", 9.0f); // スタブ
            setParam (p, "rate", 2.0f);
            setParam (p, "feel", 1.0f);
            if (edit)
                edit (p);
            return run (p, blocksFor (0.5), { { 0, on (60) } });
        };
        auto maxDiff = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            double d = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                d = std::max (d, (double) std::abs (a[i] - b[i]));
            return d;
        };
        const auto orig = render (nullptr);
        const auto bent = render ([] (ScratchProcessor& p)
        {
            p.beginUserEdit();
            p.setShapeTension (9, 0, 0.9f); // 押しの区間を曲げる
        });
        const auto reset = render ([] (ScratchProcessor& p)
        {
            p.beginUserEdit();
            p.setShapeTension (9, 0, 0.9f);
            p.resetShape (9);
        });
        check (maxDiff (orig, bent) > 0.05, "曲げると、音が変わる (最大差 " + juce::String (maxDiff (orig, bent), 3) + ")");
        check (maxDiff (orig, reset) == 0.0, "初期に戻すと、まったく同じ音");

        ScratchProcessor q;
        q.beginUserEdit();
        q.setShapeTension (9, 0, 0.5f);
        check (q.isShapeEdited (9) && q.canGoBackUser(), "編集済みになる");
        q.backUser();
        check (! q.isShapeEdited (9), "「戻る」で、数式のまま (未編集) に戻る");
    }

    // ------------------------------------------------------------------
    std::printf ("S4) 鳴る区間の終わりを伸ばすと、長く鳴る (スタブ)\n");
    {
        auto audible = [] (int extra)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "mode", 1.0f);
            setParam (p, "pattern", 9.0f);
            setParam (p, "rate", 1.0f); // 1/4 = 0.5秒で1往復
            setParam (p, "feel", 1.0f);
            if (extra > 0)
            {
                p.beginUserEdit();
                int last = 0;
                for (int k = 0; k < ScratchProcessor::userGateSegments; ++k)
                    if (p.getShape (9).gate[(size_t) k])
                        last = k;
                for (int k = last + 1; k <= last + extra; ++k)
                    p.setShapeGate (9, k, true);
            }
            auto out = run (p, blocksFor (0.5), { { 0, on (60) } });
            int n = 0;
            const size_t w = (size_t) (0.002 * fs);
            for (size_t i = 0; i + w < out.size(); i += w)
                n += rms (out, i, i + w) > 0.01 ? 1 : 0;
            return n * 2.0; // ms
        };
        const double a = audible (0), b = audible (6);
        check (b > a + 20.0, "鳴っている時間 " + juce::String (a, 0) + " ms → " + juce::String (b, 0) + " ms");
    }

    // ------------------------------------------------------------------
    std::printf ("S5) 編集した波形 (カーブの種類・曲がり具合) は、保存・復元で失われない\n");
    {
        ScratchProcessor p;
        p.beginUserEdit();
        p.setShapeTension (1, 0, -0.6f);
        p.setShapeCurve (1, 1, ScratchProcessor::curveSingle);
        const int added = p.addShapePoint (14, { 0.33f, 0.6f });
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool flags = q.isShapeEdited (1) && ! q.isShapeEdited (2) && added > 0;
        float d1 = 0.0f, d14 = 0.0f;
        for (int i = 0; i < 200; ++i)
        {
            const float phi = (float) i / 200.0f;
            d1 = std::max (d1, std::abs (q.shapeD (1, phi) - p.shapeD (1, phi)));
            d14 = std::max (d14, std::abs (q.shapeD (14, phi) - p.shapeD (14, phi)));
        }
        check (flags && d1 < 2.0e-3f && d14 < 2.0e-3f, "フォワード (編集) とユーザー1 (点を追加) が同じ形で戻る。ほかは未編集のまま (差 "
                                                         + juce::String (d1, 5) + " / " + juce::String (d14, 5) + ", 追加 " + juce::String (added)
                                                         + (flags ? "" : ", 状態がちがう") + ")");
        check (q.getShape (1).curve[1] == ScratchProcessor::curveSingle && std::abs (q.getShape (1).tension[0] + 0.6f) < 2.0e-3f, "カーブの種類と曲がり具合も戻る");
    }

    // ------------------------------------------------------------------
    std::printf ("S6) ブロック: クリックで次のパターン、Shift+クリックで前のパターン\n");
    {
        ScratchProcessor p;
        p.loadTemplate (14);
        const int a = p.getPhrase().events[0].pattern;
        p.cycleEventPattern (0, 1);
        p.cycleEventPattern (0, -1);
        p.cycleEventPattern (0, -1);
        check (p.getPhrase().events[0].pattern == (a - 1 + ScratchProcessor::numPatterns) % ScratchProcessor::numPatterns, "次 → 前 → 前 で、1つ前");
        p.setEventPattern (0, 0);
        p.cycleEventPattern (0, -1);
        check (p.getPhrase().events[0].pattern == ScratchProcessor::numPatterns - 1, "最初の前は、最後へ");
    }


    // ------------------------------------------------------------------
    std::printf ("B1) ブロックだけのカーブ: 曲げても、始点・頂点・終わりの位置は変わらない\n");
    {
        using SP = ScratchProcessor;
        const float apex = 0.3f;
        bool fixed = true, mono = true;
        for (float r : { -0.9f, 0.0f, 0.7f })
            for (float f : { -0.6f, 0.0f, 0.9f })
            {
                fixed = fixed && std::abs (SP::warpPhase (0.0f, apex, r, f)) < 1.0e-6f
                        && std::abs (SP::warpPhase (apex, apex, r, f) - apex) < 1.0e-5f
                        && std::abs (SP::warpPhase (1.0f, apex, r, f) - 1.0f) < 1.0e-5f;
                float prev = -1.0f;
                for (int i = 0; i <= 200; ++i)
                {
                    const float w = SP::warpPhase ((float) i / 200.0f, apex, r, f);
                    mono = mono && w >= prev - 1.0e-6f;
                    prev = w;
                }
            }
        check (fixed && mono, "始点・頂点・終わりは同じ位置、順番も入れ替わらない");
        check (SP::warpPhase (0.15f, apex, -0.8f, 0.0f) > 0.15f && SP::warpPhase (0.15f, apex, 0.8f, 0.0f) < 0.15f,
               "上り: マイナスで早く上がる、プラスで遅れて上がる");
        check (std::abs (SP::warpPhase (0.42f, 0.3f, 0.0f, 0.0f) - 0.42f) < 1.0e-6f, "曲げなければ、そのまま");

        ScratchProcessor p;
        check (std::abs (p.patternApex (1) - 0.42f) < 0.02f && std::abs (p.patternApex (9) - 0.15f) < 0.02f,
               "頂点: フォワード " + juce::String (p.patternApex (1), 3) + " / スタブ " + juce::String (p.patternApex (9), 3));
    }

    // ------------------------------------------------------------------
    std::printf ("B2) ブロックだけのカーブ・鳴らす終わり: 音が変わる。ほかのブロックは変わらない (120BPM)\n");
    {
        auto render = [] (float rise, float fall, float tail)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "feel", 1.0f);
            setParam (p, "human", 0.0f);
            p.loadTemplate (14);              // ミニマル: 0〜12 と 48〜60 (2つ目は 1.0秒から)
            p.setEventPattern (0, 1);         // フォワード (押す 0〜0.42 が鳴る)
            p.setEventPattern (1, 1);
            p.setEventEnd (0, 12.0f);
            p.setEventEnd (1, 12.0f);
            p.setEventRate (0, 2);
            p.setEventRate (1, 2);            // 1/8 = 0.25秒で1往復
            p.beginEdit();
            p.setEventBend (0, rise, fall);
            p.setEventTail (0, tail);
            return run (p, blocksFor (1.3), { { 0, on (60) } });
        };
        const auto plain = render (0.0f, 0.0f, 1.0f);
        const auto bent = render (-0.8f, 0.0f, 1.0f);
        const auto cut = render (0.0f, 0.0f, 0.2f);
        double dBent = 0.0, dOther = 0.0;
        for (size_t i = 0; i < (size_t) (0.25 * fs); ++i)
            dBent = std::max (dBent, (double) std::abs (plain[i] - bent[i]));
        for (size_t i = (size_t) (1.0 * fs); i < (size_t) (1.25 * fs); ++i)
            dOther = std::max (dOther, (double) std::abs (plain[i] - bent[i]) + std::abs (plain[i] - cut[i]));
        check (dBent > 0.05, "曲げたブロックは、音が変わる (最大差 " + juce::String (dBent, 3) + ")");
        check (dOther < 1.0e-6, "ほかのブロックは、そのまま");
        // フォワードは押す間 (0〜0.42 = 0〜105ms) 鳴る。終わりを 0.2 (50ms) にすると、60〜100ms は無音
        const double a = rms (plain, (size_t) (0.06 * fs), (size_t) (0.1 * fs));
        const double b = rms (cut, (size_t) (0.06 * fs), (size_t) (0.1 * fs));
        check (a > 0.05 && b < 0.005, "鳴らす終わりを短くすると、そこで切れる (rms " + juce::String (a, 3) + " → " + juce::String (b, 5) + ")");
    }

    // ------------------------------------------------------------------
    std::printf ("B3) ブロックだけのカーブは、保存・復元で戻り、「元に戻す」と「戻る」で取り消せる\n");
    {
        ScratchProcessor p;
        p.loadTemplate (0);
        p.beginEdit();
        p.setEventBend (1, 0.4f, -0.3f);
        p.setEventTail (1, 0.55f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto& e = q.getPhrase().events[1];
        check (std::abs (e.bendRise - 0.4f) < 1.0e-4f && std::abs (e.bendFall + 0.3f) < 1.0e-4f && std::abs (e.tail - 0.55f) < 1.0e-4f
                   && q.getPhrase().events[0].tail == 1.0f, "保存・復元で戻る (ほかのブロックは元のまま)");
        q.resetEventCurve (1);
        check (q.getPhrase().events[1].bendRise == 0.0f && q.getPhrase().events[1].tail == 1.0f, "元に戻す");
        q.back();
        check (std::abs (q.getPhrase().events[1].tail - 0.55f) < 1.0e-4f, "「戻る」で、元に戻す前へ");
        p.back();
        check (p.getPhrase().events[1].bendRise == 0.0f && p.getPhrase().events[1].tail == 1.0f, "ドラッグの編集も「戻る」で取り消せる");
    }


    // ------------------------------------------------------------------
    std::printf ("B4) 鳴らす終わりを伸ばす: 元の終わりの後ろも、そこまで鳴る (120BPM)\n");
    {
        ScratchProcessor p0;
        ScratchProcessor::FillEvent fe;
        fe.pattern = 1; // フォワード
        const float nat = p0.naturalEnd (fe);
        check (nat > 0.38f && nat < 0.5f, "フォワードの元の終わり " + juce::String (nat, 3));
        auto render = [] (float tail)
        {
            ScratchProcessor p;
            prepare (p);
            setParam (p, "feel", 1.0f);
            setParam (p, "human", 0.0f);
            p.loadTemplate (14);
            p.setEventPattern (0, 1);
            p.setEventEnd (0, 12.0f);
            p.setEventRate (0, 2);            // 1/8 = 0.25秒で1往復
            p.beginEdit();
            p.setEventTail (0, tail);
            return run (p, blocksFor (0.3), { { 0, on (60) } });
        };
        const auto plain = render (1.0f);
        const auto longer = render (0.8f);
        // 戻りの途中 (0.55〜0.75 = 138〜188ms): 元は無音、伸ばすと鳴る。0.8 (200ms) より後ろは、また無音
        const double a = rms (plain, (size_t) (0.14 * fs), (size_t) (0.185 * fs));
        const double b = rms (longer, (size_t) (0.14 * fs), (size_t) (0.185 * fs));
        const double c = rms (longer, (size_t) (0.21 * fs), (size_t) (0.245 * fs));
        check (a < 0.005 && b > 0.05, "元の終わりの後ろも鳴る (rms " + juce::String (a, 5) + " → " + juce::String (b, 3) + ")");
        check (c < 0.005, "伸ばした終わりの後ろは、鳴らない (rms " + juce::String (c, 5) + ")");
        const double d = rms (plain, (size_t) (0.02 * fs), (size_t) (0.09 * fs)) - rms (longer, (size_t) (0.02 * fs), (size_t) (0.09 * fs));
        check (std::abs (d) < 1.0e-6, "元から鳴っていた所は、そのまま");
    }


    // ------------------------------------------------------------------
    std::printf ("Y1) マイパターン: 保存すると、別の Cue Burn からそのまま呼び出せる (音源・ノブは変わらない)\n");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ScratchMyPatternTest");
        dir.deleteRecursively();

        ScratchProcessor a;
        prepare (a);
        a.setMyPatternDir (dir);
        check (a.getMyPatternNames().isEmpty(), "はじめは空");
        a.setBars (2);
        a.randomizeWithSeed (123);
        a.beginEdit();
        a.setEventBend (0, -0.4f, 0.2f);
        a.setEventTail (0, 0.7f);
        {
            auto m = a.getPhrase().events[0].mute;
            m.set (2);
            m.set (3);
            a.setEventMuteMask (0, m);
        }
        a.setShapeTension (9, 0, 0.6f);          // スタブの形を編集
        a.setEventPattern (1, 9);
        check (! a.saveMyPattern ("  ...  ") && ! a.saveMyPattern (""), "空の名前では保存しない");
        check (a.saveMyPattern ("my: fill/1"), "保存できる");
        const auto savedName = ScratchProcessor::cleanPatternName ("my: fill/1");
        check (a.getMyPatternNames().contains (savedName) && a.getPhrase().myName == savedName && ! a.getPhrase().isRandom && ! a.getPhrase().edited,
               "ファイル名に使えない文字は除いて、一覧に出る: " + savedName);

        ScratchProcessor b;
        prepare (b);
        b.setMyPatternDir (dir);
        b.loadTemplate (3);
        setParam (b, "tone", 0.25f);
        const float toneBefore = b.apvts.getRawParameterValue ("tone")->load();
        check (b.loadMyPattern (savedName), "別の Cue Burn で呼び出せる");
        const auto& ea = a.getPhrase().events;
        const auto& eb = b.getPhrase().events;
        bool same = ea.size() == eb.size() && a.getPhrase().bars == b.getPhrase().bars && b.getPhrase().bars == 2;
        for (size_t i = 0; same && i < ea.size(); ++i)
            same = ea[i].start == eb[i].start && ea[i].len == eb[i].len && ea[i].pattern == eb[i].pattern && ea[i].rate == eb[i].rate
                   && ea[i].pitch == eb[i].pitch && std::abs (ea[i].depth - eb[i].depth) < 1.0e-4f && std::abs (ea[i].gate - eb[i].gate) < 1.0e-4f
                   && std::abs (ea[i].bendRise - eb[i].bendRise) < 1.0e-4f && std::abs (ea[i].bendFall - eb[i].bendFall) < 1.0e-4f
                   && std::abs (ea[i].tail - eb[i].tail) < 1.0e-4f && ea[i].mute == eb[i].mute;
        check (same, "ブロック・カーブ・鳴らす終わり・ミュート・小節数が同じ");
        check (b.isShapeEdited (9) && std::abs (b.getShape (9).tension[0] - a.getShape (9).tension[0]) < 1.0e-3f, "使っている波形の編集も戻る");
        check (b.apvts.getRawParameterValue ("tone")->load() == toneBefore, "ノブは変わらない");
        check (b.getPhrase().myName == savedName && ! b.getPhrase().edited, "今のマイパターンとして覚える");
        b.back();
        check ((int) b.getPhrase().myName.length() == 0 && b.getPhrase().templateIndex == 3, "「戻る」で呼び出す前へ");

        // ◀▶: テンプレートの後ろにマイパターンが続く
        check (b.loadMyPattern (savedName), "もう一度呼び出す");
        b.stepTemplate (-1);
        check (b.getPhrase().myName.isEmpty() && b.getPhrase().templateIndex == ScratchProcessor::numTemplates - 1, "◀: 最後のテンプレートへ");
        b.stepTemplate (1);
        check (b.getPhrase().myName == savedName, "▶: マイパターンへ");
        b.stepTemplate (1);
        check (b.getPhrase().myName.isEmpty() && b.getPhrase().templateIndex == ScratchProcessor::emptyTemplate, "▶: 先頭の「空」へ回る");

        // 編集・ランダムの後も、今の位置から
        b.loadMyPattern (savedName);
        b.randomizeWithSeed (7);
        b.stepTemplate (1);
        check (b.getPhrase().myName.isEmpty() && b.getPhrase().templateIndex == ScratchProcessor::emptyTemplate, "ランダムの後も、マイパターンの位置から");

        // 小節を変えても、テンプレートで作り直さない
        b.loadMyPattern (savedName);
        const int firstLen = b.getPhrase().events[0].len;
        b.setBars (4);
        check (b.getPhrase().bars == 4 && b.getPhrase().myName == savedName && b.getPhrase().edited && b.getPhrase().events[0].len == firstLen,
               "小節数を変えると、くり返して長くする");

        // プロジェクトの保存でも、名前が残る
        juce::MemoryBlock mb;
        b.getStateInformation (mb);
        ScratchProcessor c;
        c.setStateInformation (mb.getData(), (int) mb.getSize());
        check (c.getPhrase().myName == savedName && c.getPhrase().edited, "プロジェクトの保存・復元で、名前も戻る");

        // 上書き・壊れたファイル・削除
        a.loadTemplate (2);
        const int nBlocks = (int) a.getPhrase().events.size();
        check (a.saveMyPattern (savedName) && a.getMyPatternNames().size() == 1, "同じ名前は上書き");
        b.loadMyPattern (savedName);
        check ((int) b.getPhrase().events.size() == nBlocks, "上書きした内容で呼び出せる");
        dir.getChildFile ("broken.cueburn").replaceWithText ("<not valid");
        check (! b.loadMyPattern ("broken") && b.getPhrase().myName == savedName, "壊れたファイルは読まない (今のまま)");
        b.stepTemplate (-1);
        check (b.getPhrase().myName.isEmpty(), "◀▶ は壊れたファイルを飛ばす");
        dir.getChildFile ("broken.cueburn").deleteFile();
        b.loadMyPattern (savedName);
        const auto before = b.getPhrase().events.size();
        check (b.deleteMyPattern (savedName) && b.getMyPatternNames().isEmpty(), "削除できる");
        check (b.getPhrase().myName.isEmpty() && b.getPhrase().events.size() == before, "削除しても、今のブロックは残る");
        dir.deleteRecursively();
    }


    // ------------------------------------------------------------------
    std::printf ("Y2) 空: 何も置いていない状態から始められる。鳴らず、手で置ける\n");
    {
        constexpr int E = ScratchProcessor::emptyTemplate;
        ScratchProcessor fresh;
        check (fresh.getPhrase().events.empty() && fresh.getPhrase().templateIndex == E && ! fresh.canGoBack(), "起動時は空");
        ScratchProcessor p;
        prepare (p);
        p.setMyPatternDir (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ScratchNoPatternsHere"));
        p.setBars (2);
        p.loadTemplate (E);
        check (p.getPhrase().events.empty() && p.getPhrase().bars == 2 && p.getPhrase().templateIndex == E, "ブロックが無く、小節数はそのまま");
        const auto out = run (p, blocksFor (0.5), { { 0, on (60) } });
        check (rms (out, 0, out.size()) < 1.0e-5, "ノートを置いても鳴らない");
        p.setBars (4);
        check (p.getPhrase().events.empty() && p.getPhrase().bars == 4, "小節を変えても、締めのロールは足さない");
        p.vary();
        check (p.getPhrase().events.empty(), "バリエーションを押しても壊れない");

        p.loadTemplate (E);
        p.stepTemplate (1);
        check (p.getPhrase().templateIndex == E + 1 && ! p.getPhrase().events.empty(), "▶: 空 → Sound Artz");
        p.stepTemplate (-1);
        check (p.getPhrase().templateIndex == E, "◀: Sound Artz → 空");

        p.loadTemplate (E);
        p.addEvent (0, 12, 1, 2);
        check (p.getPhrase().events.size() == 1, "マスからブロックを置ける");
        juce::MemoryBlock mb;
        p.loadTemplate (E);
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getPhrase().events.empty() && q.getPhrase().templateIndex == E, "保存・復元で空のまま");
    }


    // ------------------------------------------------------------------
    std::printf ("Y3) Sound Artz の標準パターン: カーブ・鳴らす終わりも入っていて、鳴る\n");
    {
        constexpr int E = ScratchProcessor::emptyTemplate;
        ScratchProcessor p;
        prepare (p);
        p.setMyPatternDir (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ScratchNoPatternsHere"));
        for (int i = 0; i < ScratchProcessor::numFactoryPatterns; ++i)
        {
            p.loadTemplate (E + 1 + i);
            const auto& ph = p.getPhrase();
            const size_t want = i == 0 ? 8u : 7u;
            bool shaped = false;
            for (auto& e : ph.events)
                shaped = shaped || e.tail < 1.0f || e.bendRise != 0.0f;
            check (ph.events.size() == want && shaped && ph.bars == 1 && ! ph.edited && ph.templateIndex == E + 1 + i,
                   "Set" + juce::String (i + 1) + ": ブロック " + juce::String ((int) ph.events.size()) + " 個、カーブ・終わり入り");
            const auto out = run (p, blocksFor (2.0), { { 0, on (60) } });
            check (rms (out, 0, out.size()) > 0.01, "Set" + juce::String (i + 1) + ": 鳴る");
        }
        p.loadTemplate (E + 1);
        const auto n = p.getPhrase().events.size();
        p.setBars (2);
        check (p.getPhrase().events.size() == n * 2 && p.getPhrase().events[n].start == ScratchProcessor::ticksPerBar && ! p.getPhrase().edited,
               "2小節にすると、くり返す");
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScratchProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getPhrase().templateIndex == E + 1 && q.getPhrase().events.size() == n * 2, "保存・復元で戻る");
    }

    std::printf ("W1) 波形の表示用データ: 上下の違いと、短い音が拡大で読める。30秒でも量が増えすぎない\n");
    {
        ScratchProcessor p;
        const int n = (int) (2.0 * fs);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        b.setSample (0, n / 2, 0.8f);          // 真ん中に、上向きのクリック1つ
        b.setSample (1, n / 4, -0.4f);         // 右だけ、下向きのクリック
        p.setSample (std::move (b), fs, "clicks.wav", "");
        const auto w = p.getWaveOverview();
        check (w != nullptr && std::abs (w->peak - 0.8f) < 1.0e-5f, "一番大きい値を覚える");

        auto around = [&] (double at, double width) { return w->column (at - width * 0.5, at + width * 0.5); };
        const auto click = around (0.5, 1.0e-3);
        check (std::abs (click.hi - 1.0f) < 1.0e-5f && click.lo == 0.0f, "上向きの音は、上だけに出る");
        const auto down = around (0.25, 1.0e-3);
        check (std::abs (down.lo + 0.5f) < 1.0e-5f && down.hi == 0.0f, "左右どちらのチャンネルの音も拾う");
        const auto quiet = around (0.75, 1.0e-3);
        check (quiet.lo == 0.0f && quiet.hi == 0.0f && quiet.rms == 0.0f, "無音の所は 0");
        check (around (0.5, 0.2).hi == click.hi, "広い範囲でも、短い音を取りこぼさない");

        ScratchProcessor q;
        const int big = 30 * 192000;
        juce::AudioBuffer<float> c (1, big);
        c.clear();
        q.setSample (std::move (c), 192000.0, "long.wav", "");
        const auto wl = q.getWaveOverview();
        check ((int) wl->lo.size() <= ScratchProcessor::maxOverviewBlocks && (long long) wl->lo.size() * wl->blockSize >= big,
               "最大の長さでも塊は " + juce::String ((int) wl->lo.size()) + " 個で、全体を覆う");
        const auto zero = wl->column (0.0, 1.0);
        check (zero.lo == 0.0f && zero.hi == 0.0f && zero.rms == 0.0f, "全部無音でも、壊れない");
    }

    std::printf ("\n%s (%d 件の失敗)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
