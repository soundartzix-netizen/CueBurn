// 聴き比べ用: テンプレートを、指定したテンポで鳴らして WAV に書き出す
// 使い方: RenderWav 出力.wav テンポ テンプレート番号 小節数 音源 振り幅ms ノート 秒数 [オプション…]
// オプション (名前=値):
//   sample=パス.wav  start=0〜1   … サンプルを読み込んで鳴らす (音源はサンプルになる)
//   random=シード                 … テンプレートの代わりに、ランダムで作ったフィル
//   block=パターン,速さ  mute=16分の番号,…  … 小節いっぱいの1ブロックを自分で組む (番号はミュートする16分)
//   vinyl=0〜1  feel=ms  human=0〜1
#include "PluginProcessor.h"
#include <cstdio>

namespace
{
struct FixedHead : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (true);
        return p;
    }
};

void setParam (ScratchProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 9)
    {
        std::printf ("usage: RenderWav out.wav bpm template bars src depthMs note seconds [key=value ...]\n");
        return 1;
    }
    const juce::File out { juce::String (argv[1]) };
    const double bpm = juce::String (argv[2]).getDoubleValue();
    const int tmpl = juce::String (argv[3]).getIntValue();
    const int bars = juce::String (argv[4]).getIntValue();
    const int src = juce::String (argv[5]).getIntValue();
    const float depth = juce::String (argv[6]).getFloatValue();
    const int note = juce::String (argv[7]).getIntValue();
    const double seconds = juce::String (argv[8]).getDoubleValue();

    juce::StringPairArray opt;
    for (int i = 9; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        opt.set (a.upToFirstOccurrenceOf ("=", false, false), a.fromFirstOccurrenceOf ("=", false, false));
    }

    constexpr double fs = 44100.0;
    constexpr int block = 256;
    ScratchProcessor p;
    FixedHead head;
    head.bpm = bpm;
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, fs, block);
    p.prepareToPlay (fs, block);
    setParam (p, "src", (float) src);
    setParam (p, "mode", 0.0f);
    setParam (p, "depth", depth);
    setParam (p, "human", opt.containsKey ("human") ? opt["human"].getFloatValue() : 0.0f);
    setParam (p, "level", -6.0f);
    if (opt.containsKey ("vinyl"))
        setParam (p, "vinyl", opt["vinyl"].getFloatValue());
    if (opt.containsKey ("cut"))
        setParam (p, "cut", opt["cut"].getFloatValue());
    if (opt.containsKey ("feel"))
        setParam (p, "feel", opt["feel"].getFloatValue());
    if (opt.containsKey ("sample"))
    {
        if (! p.loadSample (juce::File (opt["sample"])))
        {
            std::printf ("could not load sample\n");
            return 3;
        }
        setParam (p, "src", (float) ScratchProcessor::srcSample);
        setParam (p, "start", opt.containsKey ("start") ? opt["start"].getFloatValue() : 0.0f);
    }

    p.setBars (bars);
    if (opt.containsKey ("random"))
        p.randomizeWithSeed ((juce::uint32) opt["random"].getLargeIntValue());
    else
        p.loadTemplate (juce::jmax (0, tmpl));

    if (opt.containsKey ("block"))
    {
        while (! p.getPhrase().events.empty())
            p.removeEvent (0);
        const int len = bars * ScratchProcessor::ticksPerBar;
        const auto b = juce::StringArray::fromTokens (opt["block"], ",", "");
        p.addEvent (0, len, b[0].getIntValue(), b[1].getIntValue());
        if (opt.containsKey ("mute"))
        {
            std::bitset<ScratchProcessor::maxEventTicks> m;
            for (auto& s : juce::StringArray::fromTokens (opt["mute"], ",", ""))
                if (s.trim().isNotEmpty())
                    for (int t = 0; t < ScratchProcessor::ticksPerStep; ++t)
                        m.set ((size_t) juce::jlimit (0, len - 1, s.getIntValue() * ScratchProcessor::ticksPerStep + t));
            p.setEventMuteMask (0, m);
        }
    }

    const int total = (int) std::ceil (seconds * fs / block);
    juce::AudioBuffer<float> all (2, total * block);
    juce::AudioBuffer<float> buf (2, block);
    for (int b = 0; b < total; ++b)
    {
        juce::MidiBuffer m;
        if (b == 0)
            m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
        p.processBlock (buf, m);
        for (int ch = 0; ch < 2; ++ch)
            all.copyFrom (ch, b * block, buf, ch, 0, block);
    }

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), fs, 2, 24, {}, 0));
    if (w == nullptr)
        return 2;
    w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
