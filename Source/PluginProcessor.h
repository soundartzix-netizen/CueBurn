#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <vector>

// スクラッチ・シンセ: チャンネルに読み込む楽器。ピアノロールのノートで、選んだ音源(サイン波、ノイズ、サンプル…)を
// レコードのように前後に動かして鳴らす。ノートの頭から、頭出し位置より前へ押し出して、引き戻す。
// 画面のレコードをマウスで回せば、ノートが無くても鳴らせる。
class ScratchProcessor : public juce::AudioProcessor
{
public:
    ScratchProcessor();
    ~ScratchProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cue Burn"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    enum Source { srcSine = 0, srcSaw, srcSquare, srcNoise, srcSample };

    static constexpr int numPresetPatterns = 14;       // 決まった形のパターン
    static constexpr int numUserPatterns = 4;          // 波形エディタで描けるパターン (番号は numPresetPatterns から)
    static constexpr int numPatterns = numPresetPatterns + numUserPatterns;
    static constexpr int numRates = 9;                 // 1/2 1/4 1/8 1/16 1/32 1/4T 1/8T 1/16T 1/32T
    static constexpr int numSources = 5;
    static constexpr int numWaveBins = 200;
    static constexpr int maxOverviewBlocks = 200000;   // 波形の表示用データの細かさの上限
    static constexpr double revSeconds = 1.8;          // レコード1周の長さ (33 1/3 rpm)
    static constexpr int rootNote = 60;                // この高さで、元の音程 (FL Studio の表記で C5)
    static constexpr double baseFreq = 261.6255653006; // 波形の音源の、元の音程の周波数
    static constexpr double maxSampleSeconds = 30.0;

    // パターン: 位相 phi(0〜1)で1往復。d は頭出し位置から前へ押し出した量 (0=頭出し位置、1=振り幅いっぱい)
    // (決まった形のパターン用。ユーザー波形も含めるときは shapeD / shapeGate を使う)
    static float patternD (int pattern, float phi) noexcept;
    // パターンの、クロスフェーダー(音が聞こえる/ミュート)
    static bool patternGate (int pattern, float phi) noexcept;
    // 1往復の長さ (拍)
    static double rateBeats (int rate) noexcept;
    // 波形の形 (表示用。cycles は周期単位の位置)
    static float shapeValue (int source, double cycles) noexcept;

    //==========================================================================
    // サンプル (メッセージスレッドから呼ぶ)
    bool loadSample (const juce::File& file);
    void setSample (juce::AudioBuffer<float>&& data, double rate, const juce::String& name, const juce::String& path);
    const juce::String& getSampleName() const noexcept { return sampleName; }
    double getSampleSeconds() const noexcept { return uiSampleSeconds.load(); }

    // 波形の表示用: サンプルを細かい塊に分けて、塊ごとの最小・最大・音量をまとめたもの
    struct WaveOverview
    {
        struct Column { float lo = 0.0f, hi = 0.0f, rms = 0.0f; };

        int numSamples = 0, blockSize = 1;
        float peak = 1.0f;
        std::vector<float> lo, hi, power;

        // サンプルの [from, to) (0〜1) を1本にまとめる。値は一番大きい所が 1 になる
        Column column (double from, double to) const noexcept;
    };
    std::shared_ptr<const WaveOverview> getWaveOverview() const;
    bool canLoad (const juce::String& fileName) const;
    // プリセットサンプル (プラグインに入っている声・ホーンなど)。一覧は PresetSamples.h
    bool loadPresetSample (int index);
    int getPresetIndex() const;          // 今のサンプルがプリセットなら、その番号 (-1 = 自分で読み込んだ音)
    static int getNumPresetSamples() noexcept;

    juce::AudioFormatManager formats;

    //==========================================================================
    // フィル: 1〜4小節の中に「どのパターンを、どの速さ・高さで鳴らすか」を並べたフレーズ
    // 位置と長さの単位は「ティック」(1拍 = 24。16分音符 = 6、3連16分 = 4、32分 = 3)
    static constexpr int ticksPerBeat = 24;
    static constexpr int ticksPerStep = 6;   // 16分音符
    static constexpr int ticksPerBar = 96;
    static constexpr int stepsPerBar = 16;
    static constexpr int maxEventTicks = 4 * ticksPerBar;

    struct FillEvent
    {
        int start = 0;        // 開始 (ティック)
        int len = 24;         // 長さ (ティック)
        int pattern = 0;      // パターン
        int rate = 2;         // 速さ (0=1/2 … 4=1/32、5=1/4T … 8=1/32T)
        float depth = 1.0f;   // 振り幅の倍率
        int pitch = 0;        // 高さ (半音)
        float cue = 0.0f;     // 頭出し位置からのずらし (秒。サンプルで別の部分を鳴らす)
        float gate = 1.0f;    // 鳴らす長さ (ブロックの何割まで鳴らすか。0.1〜1。短いと、そこで切れる)
        float bendRise = 0.0f; // このブロックだけのカーブ: 始点 → 頂点 の曲がり具合 (-1〜1。0 = パターンのまま)
        float bendFall = 0.0f; //                          頂点 → 終わり の曲がり具合
        float tail = 1.0f;     // 1往復の中で、どこまで鳴らすか (0.03〜1。1 = パターンのまま。元の終わりより後ろなら、そこまで伸ばす)
        std::bitset<maxEventTicks> mute; // ミュートしたティック (ブロックの頭から数える)。レコードは動き続け、フェーダーだけ閉じる
    };

    struct Phrase
    {
        int bars = 1;
        std::vector<FillEvent> events;
        int templateIndex = 0;  // テンプレートから作ったとき
        bool isRandom = false;  // ランダムで作ったとき
        juce::uint32 seed = 0;
        bool edited = false;    // バリエーション・手直しをしたか
        juce::String myName;    // マイパターンから呼び出したときの名前 (◀▶ は、ここから前後へ)
    };

    static constexpr int numTemplates = 34;
    static constexpr int emptyTemplate = numTemplates; // 何も置いていない状態 (一覧の先頭。ここから手で置いていく)
    static constexpr int numFactoryPatterns = 2;       // Sound Artz の標準パターン (番号は emptyTemplate の次から)
    static constexpr int firstGenreTemplate = 20; // ここから、ジャンル別のテンプレート
    static constexpr int maxEvents = 128;

    static Phrase makeTemplate (int index, int bars);
    static Phrase makeRandom (juce::uint32 seed, int bars);
    static Phrase makeVariation (const Phrase& source, juce::uint32 seed);

    // フレーズの操作 (メッセージスレッドから呼ぶ)
    void loadTemplate (int index);
    void randomize();
    void randomizeWithSeed (juce::uint32 seed);
    void vary();
    bool back();
    void setBars (int bars);
    void stepTemplate (int delta);       // ◀▶: 今のテンプレートから前後へ (編集・ランダムの後でも、今の位置から。マイパターンも続けて回る)
    // マイパターン: フレーズをファイルに保存して、どのプロジェクトからでも呼び出す (音源・ノブは変えない)
    juce::File getMyPatternDir() const { return myPatternDir; }
    void setMyPatternDir (const juce::File& dir) { myPatternDir = dir; } // テスト用
    juce::StringArray getMyPatternNames() const;
    bool myPatternExists (const juce::String& name) const;
    bool saveMyPattern (const juce::String& name);   // 同じ名前があれば上書き
    bool loadMyPattern (const juce::String& name);
    bool deleteMyPattern (const juce::String& name); // ごみ箱へ
    static juce::String cleanPatternName (const juce::String& name); // ファイル名に使える形に
    void cycleEventPattern (int eventIndex, int delta = 1); // クリック: 次 (+1) / Shift+クリック: 前 (-1)
    void beginEdit();                               // ドラッグ編集の始めに1回呼ぶ (履歴に積む)
    void setEventDepth (int eventIndex, float depth); // ブロックの振り幅 (0.2〜1.5)
    void setEventGate (int eventIndex, float gate);   // ブロックの鳴らす長さ (0.1〜1)
    // ブロックだけのカーブ (フィル画面で、ブロックの上で編集)。ドラッグ中は、その場で書き換える (始めに beginEdit)
    void setEventBend (int eventIndex, float rise, float fall);
    void setEventTail (int eventIndex, float tail);
    void resetEventCurve (int eventIndex);            // カーブと終わりを元に戻す (履歴に積む)
    // 1往復の中の位置を、頂点の位置は変えずに曲げる (始点 → 頂点、頂点 → 終わりを、それぞれ片側カーブで)
    static float warpPhase (float phi, float apex, float rise, float fall) noexcept;
    float patternApex (int pattern) const;            // パターンの頂点 (一番押し出した所) の位置 (0〜1)
    float naturalEnd (const FillEvent&) const;        // そのブロックの、元の (伸ばす前の) 鳴り終わりの位置 (0〜1)
    // ブロックの右端のドラッグ: 鳴らす長さをティック(小数)で指定する。ブロック自体もその長さに縮む/空きへ伸びる
    // (後ろに鳴らない部分=ゴーストを残さない。空いた所には、ほかのブロックを追加できる)
    void setEventEnd (int eventIndex, float audibleTicks);
    // ブロックの左端のドラッグ: 鳴り終わりの位置はそのままで、頭の位置(ティック)を動かす。前の空きへ伸ばすこともできる
    void setEventStart (int eventIndex, int newStart);
    // ブロックの移動 (長さはそのまま)。ほかのブロックと重なる位置には置かない。移動後のブロックの番号を返す
    bool canPlace (int eventIndex, int start) const;
    int moveEvent (int eventIndex, int start);
    // ミュート (消しゴム): ドラッグ中は、その場で書き換える (始めに beginEdit)
    void setEventMuteMask (int eventIndex, const std::bitset<maxEventTicks>& mask);
    void clearEventMutes (int eventIndex);

    // グリッドからの手入力 (どれも履歴に積まれ、「戻る」で取り消せる)
    int addEvent (int start, int len, int pattern, int rate); // ブロックを追加 (重なるブロックは詰める)。追加したブロックの番号、-1 = 追加できない
    void removeEvent (int eventIndex);
    void setEventPattern (int eventIndex, int pattern);
    void setEventRate (int eventIndex, int rate);
    void setEventPitch (int eventIndex, int pitch);   // -12〜+12 半音
    void resetEventShape (int eventIndex);            // 振り幅・鳴らす長さを 100% に戻す
    static int autoRate (int lenTicks);               // ブロックの長さに合う速さ (1往復が収まる)
    const Phrase& getPhrase() const noexcept { return phrase; }
    bool canGoBack() const noexcept { return ! history.empty(); }

    //==========================================================================
    // 波形 (動きのカーブとフェーダー): すべてのパターンを「点 + 区間ごとのカーブ」で編集できる (波形エディタ)
    // 決まった形のパターンは、編集するまでは数式のまま。編集を始めると点とカーブに置き換わり、「初期に戻す」で数式に戻る
    static constexpr int userGateSegments = 64;
    static constexpr int userTableSize = 256;
    static constexpr int maxUserPoints = 48;
    enum CurveType { curveAuto = 0, curveSingle = 1, curveS = 2 }; // なめらか(自動) / 片側カーブ / S字カーブ

    struct Shape
    {
        std::vector<juce::Point<float>> points;  // x: 0〜1 (昇順。両端は 0 と 1)、y: 0〜1 (両端は 0)
        std::vector<float> tension;               // 区間ごとの曲がり具合 (-1〜1)。区間の数 = 点の数 - 1
        std::vector<unsigned char> curve;         // 区間ごとのカーブの種類 (CurveType)
        std::array<bool, userGateSegments> gate {};   // フェーダー (true = 聞こえる)
        std::array<float, userTableSize + 1> table {}; // 点とカーブから計算した動き (表示・再生用)
        float maxSlope = 1.0f;                    // 一番速いところの速さ (カット用)
    };
    using UserPattern = Shape;

    static float curveValue (int type, float tension, float s) noexcept; // 区間の中の位置 s (0〜1) → 0〜1 (自動は使えない)
    const Shape& getShape (int pattern) const;            // 編集していない決まった形のパターンは、数式から作った形
    const Shape& getUserPattern (int slot) const { return getShape (numPresetPatterns + juce::jlimit (0, numUserPatterns - 1, slot)); }
    bool isShapeEdited (int pattern) const noexcept;     // 決まった形のパターンを編集したか (ユーザー波形はいつも true)
    float shapeD (int pattern, float phi) const noexcept;   // すべてのパターンの動き (メッセージスレッド)
    bool shapeGate (int pattern, float phi) const noexcept;
    void beginUserEdit();                                   // 編集の始めに1回呼ぶ (波形エディタの履歴に積む)
    bool backUser();
    bool canGoBackUser() const noexcept { return ! shapeHistory.empty(); }
    void resetShape (int pattern);                       // 初期の形に戻す (履歴に積む)
    void copyShape (int dstPattern, int srcPattern);     // 形を写す (履歴に積む)
    void copyPresetToUser (int slot, int presetPattern) { if (slot >= 0 && slot < numUserPatterns) copyShape (numPresetPatterns + slot, presetPattern); }
    int addShapePoint (int pattern, juce::Point<float> p);  // 追加した点の番号 (-1 = 追加できない)
    void setShapePoint (int pattern, int index, juce::Point<float> p); // 両端は動かせない。x は隣の点の間に収める
    void removeShapePoint (int pattern, int index);
    void setShapeTension (int pattern, int segment, float tension);
    void setShapeCurve (int pattern, int segment, int curveType);
    void setShapeGate (int pattern, int gateSegment, bool open);
    // ユーザー波形 (0〜3) 用の呼び方
    int addUserPoint (int slot, juce::Point<float> p) { return addShapePoint (numPresetPatterns + slot, p); }
    void setUserPoint (int slot, int index, juce::Point<float> p) { setShapePoint (numPresetPatterns + slot, index, p); }
    void removeUserPoint (int slot, int index) { removeShapePoint (numPresetPatterns + slot, index); }
    void setUserGate (int slot, int segment, bool open) { setShapeGate (numPresetPatterns + slot, segment, open); }

    std::atomic<bool> previewOn { false };    // 試聴 (ノートを置かなくても、フレーズを鳴らす)
    std::atomic<int> previewPattern { -1 };   // 試聴で、このパターンを単発で鳴らす (-1 = 今のモードのまま。波形エディタ用)
    std::atomic<float> uiFillBeat { -1.0f };  // フレーズの中の位置 (拍。-1=鳴っていない)
    std::atomic<int> phraseChanged { 0 };     // フレーズ・ユーザー波形が変わるたびに増える (表示の更新用)

    //==========================================================================
    // エディタ → プロセッサー (レコードをマウスで回す)
    std::atomic<bool> platterTouched { false };
    std::atomic<double> platterAngle { 0.0 }; // 積算した回転角 (ラジアン、時計回りが正)

    // プロセッサー → エディタ (表示用)
    std::atomic<double> uiPos { 0.0 };     // レコード上の位置 (秒)
    std::atomic<float> uiAngle { 0.0f };   // レコードの回転角 (ラジアン)
    std::atomic<int> uiNote { -1 };        // 鳴っているノート (-1 = 無し)
    std::atomic<bool> uiActive { false };  // 音が出ているか
    std::atomic<float> uiOutDb { -100.0f };
    std::atomic<double> uiSampleSeconds { 0.0 };
    std::array<std::atomic<float>, numWaveBins> uiWave; // サンプルの波形 (ピーク)

    // 単体テスト用
    int getCurrentNote() const noexcept { return noteActive ? currentNote : -1; }

private:
    struct SampleData
    {
        juce::AudioBuffer<float> data;
        double rate = 44100.0;
    };

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void setPhrase (Phrase newPhrase, bool keepHistory = true);
    void publishPhrase();
    void rebuildShapeTable (int pattern);
    void makeEditable (int pattern); // 決まった形のパターンを、点とカーブに置き換える (まだなら)
    bool validPattern (int pattern) const noexcept { return pattern >= 0 && pattern < numPatterns; }
    juce::ValueTree phraseToTree (const Phrase&) const;
    static Phrase phraseFromTree (const juce::ValueTree&);
    juce::ValueTree shapeToTree (int pattern) const;
    void readShape (const juce::ValueTree&, int pattern);
    juce::File myPatternFile (const juce::String& name) const;
    float rtD (int pattern, float phi) const noexcept;  // オーディオスレッド用 (ユーザー波形は live のコピーを使う)
    // カット: レコードが十分に速い所か (一番速い所の cut 倍以上)。遅い所はフェーダーで切る
    bool rtFast (int pattern, float phi, float cut) const noexcept;
    bool rtFastWarped (int pattern, float phi, float cut, float apex, float rise, float fall) const noexcept;
    static float presetMaxSlope (int pattern) noexcept;
    bool rtGate (int pattern, float phi) const noexcept;

    // フレーズ: メッセージスレッドが持つ本体と、オーディオスレッド用の固定長のコピー (ユーザー波形も一緒に渡す)
    struct PhraseRT
    {
        int bars = 1;
        int n = 0;
        std::array<FillEvent, maxEvents> ev {};
        std::array<std::array<float, userTableSize + 1>, numPatterns> shapeTab {};
        std::array<std::array<bool, userGateSegments>, numPatterns> shapeGateTab {};
        std::array<float, numPatterns> shapeMaxSlope {};
        std::array<bool, numPatterns> useShape {}; // true = 点とカーブの形を使う (false = 数式のまま)
        std::array<float, numPatterns> apex {};    // パターンの頂点の位置
        std::array<float, maxEvents> natEnd {};    // ブロックの元の鳴り終わり (鳴らす終わりを伸ばした時に使う)
    };
    Phrase phrase;
    std::vector<Phrase> history;
    std::vector<Shape> shapes;                       // すべてのパターンの形 (大きいので、ヒープに)
    juce::File myPatternDir;                         // マイパターンの置き場所
    std::array<bool, numPresetPatterns> presetEdited {};
    struct ShapeSnapshot
    {
        std::vector<Shape> shapes;
        std::array<bool, numPresetPatterns> edited {};
    };
    std::vector<ShapeSnapshot> shapeHistory;
    std::unique_ptr<PhraseRT> pending, live; // 大きいので、ヒープに置く
    juce::SpinLock phraseLock;
    std::atomic<int> phraseVersion { 0 };
    int liveVersion = -1;
    double beatsSinceNote = 0.0;
    int lastEvent = -1;
    float lastCue = 0.0f;
    bool previewWasOn = false;

    double fs = 44100.0;

    // 音源
    std::unique_ptr<SampleData> sample;
    juce::SpinLock sampleLock;
    std::shared_ptr<const WaveOverview> waveOverview;
    juce::SpinLock waveLock; // 画面が使う。オーディオスレッドは触らない
    juce::String sampleName, samplePath; // メッセージスレッドだけで使う
    std::vector<float> noiseTable;

    // レコードの状態 (位置の単位は秒)
    double pos = 0.0, pos1 = 0.0, target = 0.0;
    double phase = 0.0;
    float gateGain = 1.0f, gateTarget = 1.0f;
    float speedEnv = 0.0f;
    float env = 0.0f;
    float evGainSm = 1.0f; // フィルの、ブロックごとの音量の揺れ (人間味)
    float toneState[2][2] = {};
    // レコード感
    juce::Random vinylRng { 4242 };
    float hissHp = 0.0f, hissLp = 0.0f, crackleHp = 0.0f, crackleX1 = 0.0f;
    float vinylLp[2][2] = {};
    bool usingMouse = false;
    double mouseBaseAngle = 0.0, mouseBasePos = 0.0;
    double angleSm = 0.0;
    double angle = 0.0;
    double cueNow = 0.0; // ノートを押したときの頭出し位置

    // ノート (単音。後から押したノートが優先。離すと、まだ押しているノートに戻る)
    std::vector<int> heldStack;
    std::array<float, 128> heldVel {};
    int currentNote = rootNote;
    float velocityAmp = 1.0f;
    bool noteActive = false;
    bool pendingTrigger = false;

    float peakOut = 0.0f;

    std::atomic<float> *pSrc, *pMode, *pPattern, *pRate, *pDepth, *pFeel, *pHuman, *pStart, *pTone, *pLevel, *pVinyl, *pCut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScratchProcessor)
};
