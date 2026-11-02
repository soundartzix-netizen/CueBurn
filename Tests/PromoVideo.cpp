// 告知動画の素材づくり: 本物のエディタを画面外で描画し、同じ処理の音と一緒にコマごとに書き出す
// 使い方: PromoVideo 出力フォルダ テンポ  → frames\f%05d.png と scratch.wav
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>

namespace
{
constexpr double fs = 44100.0;
constexpr int block = 256;
constexpr int fps = 30;
constexpr int videoW = 1920;
constexpr int videoH = 1080;
constexpr float uiScale = 1.35f;

struct Head : juce::AudioPlayHead
{
    double bpm = 95.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (true);
        return p;
    }
};

struct Scene
{
    int templateIndex; // -1 = ランダム
    juce::uint32 seed;
    int preset;
    const char* title;
    const char* sub;
};

void setParam (ScratchProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

juce::Font uiFont (float size, bool bold)
{
    return juce::Font (juce::FontOptions ("Yu Gothic UI", size, bold ? juce::Font::bold : juce::Font::plain));
}

void drawBackground (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15181e), 0.0f, 0.0f, juce::Colour (0xff08090c), (float) videoW, (float) videoH, false));
    g.fillAll();
}

void drawScene (juce::Graphics& g, const juce::Image& ui, const Scene& s, float alpha)
{
    drawBackground (g);
    const int x = 36;
    const int y = (videoH - ui.getHeight()) / 2;
    g.drawImageAt (ui, x, y);

    const auto col = juce::Rectangle<int> (x + ui.getWidth() + 40, 0, videoW - (x + ui.getWidth() + 40) - 44, videoH);
    g.setColour (juce::Colour (0xffffb02e));
    g.setFont (uiFont (34.0f, true));
    g.drawText ("CUE BURN", col.withY (y + 10).withHeight (48), juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (alpha));
    g.setFont (uiFont (56.0f, true));
    g.drawFittedText (juce::String::fromUTF8 (s.title), col.withY (380).withHeight (240), juce::Justification::topLeft, 3, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.68f * alpha));
    g.setFont (uiFont (30.0f, false));
    g.drawFittedText (juce::String::fromUTF8 (s.sub), col.withY (630).withHeight (120), juce::Justification::topLeft, 3, 1.0f);
}

void drawEndCard (juce::Graphics& g, float alpha)
{
    drawBackground (g);
    auto text = [&] (const juce::String& t, int y, int h, float size, bool bold, juce::Colour c)
    {
        g.setColour (c.withMultipliedAlpha (alpha));
        g.setFont (uiFont (size, bold));
        g.drawText (t, 0, y, videoW, h, juce::Justification::centred);
    };
    text ("CUE BURN", 330, 170, 150.0f, true, juce::Colour (0xffffb02e));
    text (juce::String::fromUTF8 ("無料のスクラッチ楽器 VST3"), 520, 80, 58.0f, true, juce::Colours::white);
    text ("FL Studio / Windows 64bit", 610, 60, 36.0f, false, juce::Colours::white.withAlpha (0.7f));
    text ("github.com/soundartzix-netizen/CueBurn", 700, 60, 36.0f, false, juce::Colour (0xff29f1e0));
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::printf ("usage: PromoVideo outdir bpm\n");
        return 1;
    }
    const juce::File dir { juce::String (argv[1]) };
    const auto frames = dir.getChildFile ("frames");
    frames.deleteRecursively();
    frames.createDirectory();

    const double bpm = juce::String (argv[2]).getDoubleValue();
    const double barSec = 240.0 / bpm;
    const double sceneSec = 2.0 * barSec;
    const double endSec = 3.5;

    const Scene scenes[] = {
        { ScratchProcessor::emptyTemplate + 1, 0, 9, u8"ノートを置くだけで、スクラッチが鳴る", u8"ピアノロールに1音置くだけ" },
        { ScratchProcessor::emptyTemplate + 2, 0, 12, u8"ブロックを並べて、フレーズを組む", u8"カーブも音の終わりも作り込める" },
        { ScratchProcessor::firstGenreTemplate, 0, 15, u8"自分の曲の音で、スクラッチ", u8"読み込むだけ。プリセットも27種類" },
        { -1, 4711u, 3, u8"ランダムで、一発生成", u8"編集して、マイパターンに保存" },
    };
    const juce::String samplePrefix = argc > 3 ? juce::String (argv[3]) : juce::String(); // 指定すると、シーンごとに「プレフィックス A.wav」〜を読み込む
    const int numScenes = (int) std::size (scenes);

    ScratchProcessor p;
    Head head;
    head.bpm = bpm;
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, fs, block);
    p.prepareToPlay (fs, block);
    p.apvts.state.setProperty ("lang", "ja", nullptr);
    setParam (p, "src", (float) ScratchProcessor::srcSample);
    setParam (p, "mode", 0.0f);
    setParam (p, "level", -6.0f);
    p.setBars (1);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    const double totalSec = numScenes * sceneSec + endSec;
    const int totalFrames = (int) std::ceil (totalSec * fps);
    juce::AudioBuffer<float> all (2, (int) std::ceil ((totalFrames + 1) * fs / fps) + block);
    all.clear();
    juce::AudioBuffer<float> buf (2, block);
    juce::int64 audioPos = 0;
    int current = -1;

    for (int f = 0; f < totalFrames; ++f)
    {
        const double t = (double) f / fps;
        const auto wantSamples = (juce::int64) std::llround ((f + 1) * fs / fps);
        while (audioPos + block <= wantSamples)
        {
            const double blockT = (double) audioPos / fs;
            const int sc = blockT < numScenes * sceneSec ? (int) (blockT / sceneSec) : numScenes;
            juce::MidiBuffer m;
            if (sc != current)
            {
                if (current >= 0)
                    m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                current = sc;
                if (sc < numScenes)
                {
                    const auto& s = scenes[sc];
                    if (samplePrefix.isNotEmpty())
                    {
                        const juce::File sampleFile (samplePrefix + " " + juce::String::charToString ((juce::juce_wchar) ('A' + sc)) + ".wav");
                        if (! p.loadSample (sampleFile))
                        {
                            std::printf ("could not load sample\n");
                            return 3;
                        }
                    }
                    else
                    {
                        p.loadPresetSample (s.preset);
                    }
                    setParam (p, "src", (float) ScratchProcessor::srcSample);
                    if (s.templateIndex < 0)
                        p.randomizeWithSeed (s.seed);
                    else
                        p.loadTemplate (s.templateIndex);
                    m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 1);
                }
            }
            p.processBlock (buf, m);
            for (int ch = 0; ch < 2; ++ch)
                all.copyFrom (ch, (int) audioPos, buf, ch, 0, block);
            audioPos += block;
        }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (36); // エディタのタイマーに状態を拾わせる

        juce::Image frame (juce::Image::RGB, videoW, videoH, true);
        {
            juce::Graphics g (frame);
            const int sc = t < numScenes * sceneSec ? (int) (t / sceneSec) : numScenes;
            if (sc < numScenes)
            {
                const double inScene = t - sc * sceneSec;
                const auto ui = ed->createComponentSnapshot (ed->getLocalBounds(), true, uiScale);
                drawScene (g, ui, scenes[sc], (float) juce::jlimit (0.0, 1.0, inScene / 0.2));
            }
            else
            {
                drawEndCard (g, (float) juce::jlimit (0.0, 1.0, (t - numScenes * sceneSec) / 0.4));
            }
        }
        const auto file = frames.getChildFile (juce::String::formatted ("f%05d.png", f));
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (frame, out);
        if (f % 60 == 0)
            std::printf ("frame %d / %d\n", f, totalFrames);
    }

    const auto wavFile = dir.getChildFile ("scratch.wav");
    wavFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (wavFile), fs, 2, 24, {}, 0));
    if (w == nullptr)
        return 2;
    w->writeFromAudioSampleBuffer (all, 0, (int) audioPos);
    std::printf ("done: %d frames, %.2f s\n", totalFrames, (double) audioPos / fs);
    return 0;
}
