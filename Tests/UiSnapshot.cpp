// エディタを画面外で描画して、状態ごとにPNGへ保存する (見た目の確認用)
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
constexpr double fs = 44100.0;
constexpr int block = 512;

void setParam (ScratchProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

void process (ScratchProcessor& p, double seconds, int noteOnAtStart = -1)
{
    juce::AudioBuffer<float> buf (2, block);
    for (int b = 0; b < (int) std::ceil (seconds * fs / block); ++b)
    {
        juce::MidiBuffer m;
        if (b == 0 && noteOnAtStart >= 0)
            m.addEvent (juce::MidiMessage::noteOn (1, noteOnAtStart, (juce::uint8) 110), 0);
        p.processBlock (buf, m);
    }
}

// 声のような音のサンプルを作る (表示の確認用)
void loadVoiceLike (ScratchProcessor& p)
{
    const int n = (int) (1.6 * fs);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double env = t < 0.05 ? t / 0.05 : std::exp (-(t - 0.05) * 2.2);
        const double v = env * (0.6 * std::sin (6.283185307 * 220.0 * t) + 0.3 * std::sin (6.283185307 * 660.0 * t) + 0.1 * std::sin (6.283185307 * 1320.0 * t));
        b.setSample (0, i, (float) v);
        b.setSample (1, i, (float) v);
    }
    p.setSample (std::move (b), fs, "ahh_vocal.wav", "");
}

void snapshot (ScratchProcessor& p, const juce::String& path, std::function<void (ScratchEditor&)> setup = nullptr)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
    if (setup != nullptr)
        if (auto* se = dynamic_cast<ScratchEditor*> (ed.get()))
            setup (*se);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::printf ("wrote %s (%dx%d)\n", path.toRawUTF8(), img.getWidth(), img.getHeight());
}

}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String dir = argc > 1 ? juce::String (argv[1]) : juce::String (".");

    auto make = [&] (ScratchProcessor& p, const char* lang)
    {
        p.setPlayConfigDetails (0, 2, fs, block);
        p.prepareToPlay (fs, block);
        p.apvts.state.setProperty ("lang", lang, nullptr);
    };

    { // 待機中・サイン (日本語)
        ScratchProcessor p;
        make (p, "ja");
        snapshot (p, dir + "/ui_idle_ja.png");
    }
    { // ノイズ + ランダムの2小節フィル (英語): ブロックごとに、振り幅と長さが違う。1つは、ホバー表示
        ScratchProcessor p;
        make (p, "en");
        setParam (p, "src", 3.0f);
        p.setBars (2);
        p.randomizeWithSeed (555);
        process (p, 1.1, 60);
        snapshot (p, dir + "/ui_fill_random_en.png");
    }
    { // サンプル + テンプレート「ビルドアップ」4小節 (日本語): 振り幅が少しずつ大きくなる
        ScratchProcessor p;
        make (p, "ja");
        loadVoiceLike (p);
        setParam (p, "src", 4.0f);
        setParam (p, "start", 0.3f);
        p.setBars (4);
        p.loadTemplate (11);
        p.setEventGate (2, 0.55f); // 手で短くした例
        process (p, 3.4, 67);
        snapshot (p, dir + "/ui_fill_sample_ja.png");
    }
    { // 単発モード: 12種のパターン (日本語)
        ScratchProcessor p;
        make (p, "ja");
        setParam (p, "mode", 1.0f);
        setParam (p, "pattern", 7.0f);
        snapshot (p, dir + "/ui_single_ja.png");
    }
    { // グリッドから追加 (英語): ミニマルに、新しいパターンを手で足した例 + 範囲選択中
        ScratchProcessor p;
        make (p, "en");
        p.setBars (2);
        p.loadTemplate (14);
        constexpr int T = ScratchProcessor::ticksPerStep;
        p.addEvent (3 * T, 2 * T, 9, 3);   // スタブ
        p.addEvent (11 * T, 5 * T, 6, 1);  // トランスフォーマー
        p.addEvent (16 * T, 6 * T, 7, 1);  // クラブ
        p.addEvent (24 * T, 4 * T, 8, 2);  // オービット
        p.addEvent (28 * T, 4 * T, 11, 2); // ハイドロプレーン
        snapshot (p, dir + "/ui_grid_en.png", [] (ScratchEditor& ed) { ed.getTimeline().showSelection (5, 7); });
    }
    { // 3連のマス目 + ミュート (日本語): トラップ (3連) に、ベイビーの一部を消した例
        ScratchProcessor p;
        make (p, "ja");
        p.apvts.state.setProperty ("grid", 8, nullptr); // 3連8分のマス目
        p.setBars (2);
        p.loadTemplate (ScratchProcessor::firstGenreTemplate + 9);
        p.addEvent (ScratchProcessor::ticksPerBar + 8, 16, 0, 6);
        auto m = p.getPhrase().events[0].mute;
        for (int i = 6; i < 12; ++i)
            m.set ((size_t) i);
        p.setEventMuteMask (0, m);
        snapshot (p, dir + "/ui_triplet_mute_ja.png");
    }
    { // 波形エディタ (日本語): ユーザー2 に、点を足した例
        ScratchProcessor p;
        make (p, "ja");
        p.addUserPoint (1, { 0.62f, 0.85f });
        p.setUserGate (1, 40, false);
        p.setUserGate (1, 41, false);
        snapshot (p, dir + "/ui_wave_editor_ja.png", [] (ScratchEditor& ed) { ed.showPatternEditor (ScratchProcessor::numPresetPatterns + 1); });
    }
    { // フィル画面: ブロックの上でカーブを曲げ、鳴らす終わりを短くした例 (日本語、マウスを乗せた表示)
        ScratchProcessor p;
        make (p, "ja");
        p.loadTemplate (14);
        p.setEventPattern (0, 1);
        p.setEventEnd (0, 24.0f);
        p.setEventRate (0, 1);
        p.beginEdit();
        p.setEventBend (0, -0.6f, 0.5f);
        p.setEventTail (0, 0.33f);
        snapshot (p, dir + "/ui_block_curve_ja.png", [] (ScratchEditor& ed) { ed.getTimeline().showHover (0); });
    }
    { // フィル画面: 鳴らす終わりを伸ばした例 (日本語、マウスを乗せた表示)
        ScratchProcessor p;
        make (p, "ja");
        p.loadTemplate (14);
        p.setEventPattern (0, 1);
        p.setEventEnd (0, 24.0f);
        p.setEventRate (0, 1);
        p.beginEdit();
        p.setEventTail (0, 0.75f);
        snapshot (p, dir + "/ui_block_tail_long_ja.png", [] (ScratchEditor& ed) { ed.getTimeline().showHover (0); });
    }
    { // マイパターン: 名前を入れて保存する画面と、保存した後の表示 (日本語・英語)
        const auto myDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ScratchMyPatternSnap");
        myDir.deleteRecursively();
        ScratchProcessor p;
        make (p, "ja");
        p.setMyPatternDir (myDir);
        p.randomizeWithSeed (5);
        snapshot (p, dir + "/ui_my_prompt_ja.png", [] (ScratchEditor& ed) { ed.askSavePattern(); });
        p.saveMyPattern (juce::String::fromUTF8 ("サビ前のフィル"));
        snapshot (p, dir + "/ui_my_saved_ja.png");
        ScratchProcessor q;
        make (q, "en");
        q.setMyPatternDir (myDir);
        snapshot (q, dir + "/ui_my_prompt_en.png", [] (ScratchEditor& ed) { ed.askSavePattern(); });
        myDir.deleteRecursively();
    }
    { // 空のテンプレート (日本語)
        ScratchProcessor p;
        make (p, "ja");
        p.loadTemplate (ScratchProcessor::emptyTemplate);
        snapshot (p, dir + "/ui_empty_ja.png");
    }
    { // Sound Artz の標準パターン (日本語)
        ScratchProcessor p;
        make (p, "ja");
        p.loadTemplate (ScratchProcessor::emptyTemplate + 1);
        snapshot (p, dir + "/ui_factory_ja.png");
    }
    { // 波形エディタで、決まった形のパターン (スタブ) を編集した例 (日本語)
        ScratchProcessor p;
        make (p, "ja");
        p.beginUserEdit();
        p.setShapeTension (9, 0, 0.6f);
        for (int k = 9; k < 14; ++k)
            p.setShapeGate (9, k, true); // 鳴る区間を少し伸ばす
        snapshot (p, dir + "/ui_wave_editor_preset_ja.png", [] (ScratchEditor& ed) { ed.showPatternEditor (9); });
    }
    { // ジャンル別テンプレート「ブレイクビーツ」2小節 (日本語)
        ScratchProcessor p;
        make (p, "ja");
        setParam (p, "src", 3.0f);
        p.setBars (2);
        p.loadTemplate (ScratchProcessor::firstGenreTemplate + 1);
        snapshot (p, dir + "/ui_genre_ja.png");
    }
    { // 波形: 本物のプリセットの声の全体表示と、頭出し付近を拡大した表示 (日本語・英語)
        ScratchProcessor p;
        make (p, "ja");
        p.loadPresetSample (6);
        setParam (p, "src", 4.0f);
        setParam (p, "start", 0.12f);
        setParam (p, "depth", 120.0f);
        snapshot (p, dir + "/ui_wave_full_ja.png");
        snapshot (p, dir + "/ui_wave_zoom_ja.png", [] (ScratchEditor& ed) { ed.getSourceView().setView (0.08, 0.12); });
        p.apvts.state.setProperty ("lang", "en", nullptr);
        snapshot (p, dir + "/ui_wave_full_en.png");
    }
    { // 波形: 30秒の長いサンプル (下の帯と、一番拡大した表示)
        ScratchProcessor p;
        make (p, "ja");
        const int n = (int) (30.0 * fs);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            const double beat = std::fmod (t, 0.5);
            const float v = (float) (std::exp (-beat * 7.0) * (0.3 + 0.7 * std::abs (std::sin (0.2 * t))) * std::sin (6.283185307 * (110.0 + 40.0 * std::floor (t / 2.0)) * t));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        p.setSample (std::move (b), fs, "long_track.wav", "");
        setParam (p, "src", 4.0f);
        setParam (p, "start", 0.41f);
        snapshot (p, dir + "/ui_wave_long_ja.png");
        snapshot (p, dir + "/ui_wave_long_zoom_ja.png", [] (ScratchEditor& ed) { ed.getSourceView().setView (0.405, 0.1 / 30.0); });
    }
    { // White スキン: 音を鳴らしている画面、ブロックのホバー、波形エディタ、名前の入力、英語、単発
        auto useWhite = [] (ScratchEditor& ed) { deck::setSkin (deck::Skin::white); ed.applySkin(); };
        ScratchProcessor p;
        make (p, "ja");
        p.loadPresetSample (6);
        setParam (p, "src", 4.0f);
        setParam (p, "start", 0.12f);
        p.loadTemplate (ScratchProcessor::emptyTemplate + 1);
        process (p, 0.6, 60);
        snapshot (p, dir + "/ui_white_ja.png", useWhite);
        snapshot (p, dir + "/ui_white_hover_ja.png", [useWhite] (ScratchEditor& ed) { useWhite (ed); ed.getTimeline().showHover (1); });
        snapshot (p, dir + "/ui_white_editor_ja.png", [useWhite] (ScratchEditor& ed) { useWhite (ed); ed.showPatternEditor (9); });
        snapshot (p, dir + "/ui_white_prompt_ja.png", [useWhite] (ScratchEditor& ed) { useWhite (ed); ed.askSavePattern(); });
        // 頂点の位置: ベイビー (2つ目) の頂点を、元の位置と真ん中で比べる
        snapshot (p, dir + "/ui_white_apex_before_ja.png", [useWhite] (ScratchEditor& ed) { useWhite (ed); ed.getTimeline().showHover (2); });
        p.beginEdit();
        p.setEventApex (2, 0.5f);
        snapshot (p, dir + "/ui_white_apex_mid_ja.png", [useWhite] (ScratchEditor& ed) { useWhite (ed); ed.getTimeline().showHover (2); });
        p.back();
        p.apvts.state.setProperty ("lang", "en", nullptr);
        snapshot (p, dir + "/ui_white_en.png", useWhite);
        p.apvts.state.setProperty ("lang", "ja", nullptr);
        setParam (p, "mode", 1.0f);
        snapshot (p, dir + "/ui_white_single_ja.png", useWhite);
        setParam (p, "mode", 0.0f);
        deck::setSkin (deck::Skin::deck);
    }
    return 0;
}
